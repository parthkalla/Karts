// Original work, MIT licensed.

#include "Match/KCFRespawnComponent.h"

#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Match/KCFGameMode.h"
#include "Match/KCFGameState.h"
#include "Match/KCFPlayerSpawnPoint.h"
#include "Player/KCFPlayerController.h"
#include "Player/KCFPlayerState.h"
#include "TimerManager.h"

UKCFRespawnComponent::UKCFRespawnComponent()
{
	// No tick: the queue is serviced by a timer that only runs while something is queued.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UKCFRespawnComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningGameMode = Cast<AKCFGameMode>(GetOwner());

	if (!OwningGameMode)
	{
		// This is a hard configuration error rather than a recoverable one: without a game mode the
		// component cannot spawn anything, and placing it elsewhere means a misunderstanding of the
		// architecture. It still refuses to crash a shipping build.
		UE_LOG(LogKCFMatch, Error,
			TEXT("UKCFRespawnComponent must be owned by an AKCFGameMode; it is on '%s' and will do nothing."),
			*GetNameSafe(GetOwner()));
	}
}

void UKCFRespawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueueTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UKCFRespawnComponent::EnqueueRespawn(AController* Controller, float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!Controller || !World || !OwningGameMode)
	{
		return;
	}

	const float ReadyTime = World->GetTimeSeconds() + FMath::Max(0.f, DelaySeconds);

	// Updating in place rather than appending: a player who dies twice in quick succession (possible
	// with a delayed explosion) must not end up with two pawns.
	for (FKCFRespawnRequest& Request : Queue)
	{
		if (Request.Controller.Get() == Controller)
		{
			Request.EarliestRespawnTime = ReadyTime;
			UpdateTimerState();
			return;
		}
	}

	FKCFRespawnRequest NewRequest;
	NewRequest.Controller = Controller;
	NewRequest.EarliestRespawnTime = ReadyTime;
	Queue.Add(NewRequest);

	if (AKCFPlayerController* KCFController = Cast<AKCFPlayerController>(Controller))
	{
		// Replicated to the owner so their HUD can run a countdown, and sent as a one-shot RPC so a
		// respawn prompt appears without the client polling.
		const AKCFGameState* GameState = KCFController->GetKCFGameState();
		const float ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : 0.f;
		KCFController->SetRespawnAvailableTime(ServerNow + FMath::Max(0.f, DelaySeconds));
		KCFController->ClientNotifyRespawnAvailable(FMath::Max(0.f, DelaySeconds));
	}

	UpdateTimerState();
}

void UKCFRespawnComponent::CancelRespawn(AController* Controller)
{
	const int32 Removed = Queue.RemoveAll([Controller](const FKCFRespawnRequest& Request)
	{
		return !Request.Controller.IsValid() || Request.Controller.Get() == Controller;
	});

	if (Removed > 0)
	{
		UpdateTimerState();
	}
}

bool UKCFRespawnComponent::RespawnNow(AController* Controller)
{
	if (!Controller || !OwningGameMode)
	{
		return false;
	}

	// A controller that already has a pawn is not respawning; silently succeeding here would mask a
	// double-respawn bug.
	if (Controller->GetPawn() != nullptr)
	{
		KCF_REPORT_RECOVERABLE(LogKCFMatch, TEXT("RespawnNow ignored for '%s': controller already possesses a pawn."), *GetNameSafe(Controller));
		return false;
	}

	// A mode-specific placement wins over the generic selector: a race returns a kart to its last
	// gate, which no amount of spawn-point scoring could ever arrive at.
	FTransform OverrideTransform;
	if (OwningGameMode->GetRespawnTransformOverride(Controller, OverrideTransform))
	{
		CancelRespawn(Controller);

		if (OwningGameMode->SpawnKartForController(Controller, OverrideTransform))
		{
			if (AKCFPlayerController* KCFController = Cast<AKCFPlayerController>(Controller))
			{
				KCFController->SetRespawnAvailableTime(-1.f);
			}
			return true;
		}

		return false;
	}

	AKCFPlayerSpawnPoint* SpawnPoint = SelectSpawnPoint(Controller);
	if (!SpawnPoint)
	{
		UE_LOG(LogKCFMatch, Error, TEXT("No usable spawn point found for '%s'. Place at least one AKCFPlayerSpawnPoint or APlayerStart in the level."),
			*GetNameSafe(Controller));
		return false;
	}

	if (const UWorld* World = GetWorld())
	{
		SpawnPoint->MarkUsed(World->GetTimeSeconds());
	}

	CancelRespawn(Controller);

	const bool bSpawned = OwningGameMode->SpawnKartForController(Controller, SpawnPoint->GetActorTransform());

	if (bSpawned)
	{
		if (AKCFPlayerController* KCFController = Cast<AKCFPlayerController>(Controller))
		{
			KCFController->SetRespawnAvailableTime(-1.f);
		}
	}

	return bSpawned;
}

void UKCFRespawnComponent::ClearQueue()
{
	Queue.Reset();
	UpdateTimerState();
}

void UKCFRespawnComponent::ServiceQueue()
{
	const UWorld* World = GetWorld();
	if (!World || !OwningGameMode)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Iterate over a copy: RespawnNow mutates the queue through CancelRespawn.
	TArray<FKCFRespawnRequest> Snapshot = Queue;

	for (const FKCFRespawnRequest& Request : Snapshot)
	{
		AController* Controller = Request.Controller.Get();
		if (!Controller)
		{
			// The controller was destroyed (player disconnected) while queued.
			Queue.RemoveAll([](const FKCFRespawnRequest& Entry) { return !Entry.Controller.IsValid(); });
			continue;
		}

		if (Now < Request.EarliestRespawnTime)
		{
			continue;
		}

		if (!OwningGameMode->ShouldRespawnController(Controller))
		{
			// Elimination, match over, or the player became a spectator. Drop the request instead of
			// retrying it forever.
			CancelRespawn(Controller);
			continue;
		}

		RespawnNow(Controller);
	}

	UpdateTimerState();
}

AKCFPlayerSpawnPoint* UKCFRespawnComponent::SelectSpawnPoint(AController* Controller) const
{
	const UWorld* World = GetWorld();
	if (!World || !OwningGameMode)
	{
		return nullptr;
	}

	const int32 TeamId = IKCFTeamAgent::ResolveTeamId(Controller);

	TArray<AKCFPlayerSpawnPoint*> Candidates;
	GatherCandidates(TeamId, Candidates);

	if (Candidates.Num() == 0)
	{
		// Fall back to any point at all, ignoring the team filter, rather than failing to spawn the
		// player. A badly tagged level should produce a worse spawn, not an unplayable match.
		GatherCandidates(KCF_NO_TEAM, Candidates);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	const UKCFGameModeDataAsset* Config = OwningGameMode->GetGameModeConfig();
	const EKCFSpawnSelectionMode Mode = Config ? Config->SpawnSelection : EKCFSpawnSelectionMode::Random;

	switch (Mode)
	{
	case EKCFSpawnSelectionMode::Random:
		return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];

	case EKCFSpawnSelectionMode::Sequential:
	{
		Candidates.Sort([](const AKCFPlayerSpawnPoint& A, const AKCFPlayerSpawnPoint& B)
		{
			return A.SpawnOrder < B.SpawnOrder;
		});

		// SequentialCursor is mutable state on a const method by design: the cursor is bookkeeping,
		// not observable game state, and threading it through every caller would be noise.
		const int32 Index = SequentialCursor % Candidates.Num();
		const_cast<UKCFRespawnComponent*>(this)->SequentialCursor = Index + 1;
		return Candidates[Index];
	}

	case EKCFSpawnSelectionMode::TeamZone:
	case EKCFSpawnSelectionMode::FarthestFromEnemies:
	default:
	{
		AKCFPlayerSpawnPoint* Best = Candidates[0];
		float BestScore = -FLT_MAX;

		for (AKCFPlayerSpawnPoint* Candidate : Candidates)
		{
			const float Score = ScoreSpawnPoint(*Candidate, Controller, TeamId);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}

		return Best;
	}
	}
}

float UKCFRespawnComponent::ScoreSpawnPoint(const AKCFPlayerSpawnPoint& Point, AController* Controller, int32 TeamId) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}

	const FVector PointLocation = Point.GetActorLocation();
	const UKCFGameModeDataAsset* Config = OwningGameMode ? OwningGameMode->GetGameModeConfig() : nullptr;
	const float PreferredDistance = Config ? Config->MinSpawnDistanceFromEnemies : 2500.f;

	float NearestHostileDistSq = FLT_MAX;
	float NearestFriendlyDistSq = FLT_MAX;

	for (TActorIterator<APawn> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const APawn* OtherPawn = *It;
		if (!OtherPawn || OtherPawn->GetController() == Controller)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(PointLocation, OtherPawn->GetActorLocation());
		const EKCFTeamAttitude Attitude = (TeamId == KCF_NO_TEAM)
			? EKCFTeamAttitude::Hostile
			: (IKCFTeamAgent::ResolveTeamId(OtherPawn) == TeamId ? EKCFTeamAttitude::Friendly : EKCFTeamAttitude::Hostile);

		if (Attitude == EKCFTeamAttitude::Hostile)
		{
			NearestHostileDistSq = FMath::Min(NearestHostileDistSq, DistSq);
		}
		else
		{
			NearestFriendlyDistSq = FMath::Min(NearestFriendlyDistSq, DistSq);
		}
	}

	// Distance from the nearest hostile is the dominant term, but saturated at the preferred
	// distance: once a spawn is far enough away, being even farther is not better, and without the
	// cap every player would spawn in the single most remote corner of the map.
	const float HostileDistance = (NearestHostileDistSq == FLT_MAX) ? PreferredDistance : FMath::Sqrt(NearestHostileDistSq);
	float Score = FMath::Min(HostileDistance, PreferredDistance) / FMath::Max(1.f, PreferredDistance) * 100.f;

	// Mild preference for spawning near a team-mate in a team mode.
	if (TeamId != KCF_NO_TEAM && NearestFriendlyDistSq != FLT_MAX)
	{
		const float FriendlyDistance = FMath::Sqrt(NearestFriendlyDistSq);
		Score += FMath::GetMappedRangeValueClamped(FVector2D(0.f, PreferredDistance * 2.f), FVector2D(15.f, 0.f), FriendlyDistance);
	}

	// Penalise a point another player just used, so a simultaneous double respawn separates.
	const float SinceUsed = World->GetTimeSeconds() - Point.GetLastUsedTime();
	if (SinceUsed < RecentUseWindowSeconds)
	{
		Score -= 60.f * (1.f - SinceUsed / FMath::Max(KINDA_SMALL_NUMBER, RecentUseWindowSeconds));
	}

	// A small random term breaks ties so that an empty arena does not always use the same spawn.
	Score += FMath::FRandRange(0.f, 5.f);

	return Score + Point.ScoreBias;
}

void UKCFRespawnComponent::GatherCandidates(int32 TeamId, TArray<AKCFPlayerSpawnPoint*>& OutCandidates) const
{
	OutCandidates.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AKCFPlayerSpawnPoint> It(World); It; ++It)
	{
		AKCFPlayerSpawnPoint* Point = *It;
		if (Point && Point->IsUsableByTeam(TeamId))
		{
			OutCandidates.Add(Point);
		}
	}
}

void UKCFRespawnComponent::UpdateTimerState()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	const bool bShouldRun = Queue.Num() > 0;
	const bool bIsRunning = Timers.IsTimerActive(QueueTimerHandle);

	// Keeping the timer off while the queue is empty means an idle match costs nothing here at all.
	if (bShouldRun && !bIsRunning)
	{
		Timers.SetTimer(QueueTimerHandle, this, &UKCFRespawnComponent::ServiceQueue, QueueServiceInterval, true);
	}
	else if (!bShouldRun && bIsRunning)
	{
		Timers.ClearTimer(QueueTimerHandle);
	}
}
