// Original work, MIT licensed.

#include "Match/KCFRaceGameMode.h"

#include "Core/KCFLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Match/KCFGameState.h"
#include "Match/KCFRaceCheckpoint.h"
#include "Match/KCFRaceProgressComponent.h"
#include "Player/KCFPlayerState.h"
#include "Vehicles/KCFKartPawn.h"

AKCFRaceGameMode::AKCFRaceGameMode()
{
	// A race is decided by laps, not by a score limit, so the base class's score-limit check must
	// not be able to end it early. CheckWinCondition below replaces it outright.
}

void AKCFRaceGameMode::BeginPlay()
{
	Super::BeginPlay();

	BuildCircuit();

	// Players who connected before BeginPlay (the host on a listen server) still need progress.
	if (AKCFGameState* KCFGameState = GetKCFGameState())
	{
		for (APlayerState* Entry : KCFGameState->PlayerArray)
		{
			if (AKCFPlayerState* KCFPlayerState = Cast<AKCFPlayerState>(Entry))
			{
				EnsureRaceProgress(KCFPlayerState);
			}
		}
	}
}

void AKCFRaceGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (AKCFPlayerState* KCFPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AKCFPlayerState>() : nullptr)
	{
		EnsureRaceProgress(KCFPlayerState);
	}
}

void AKCFRaceGameMode::BuildCircuit()
{
	Checkpoints.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AKCFRaceCheckpoint> It(World); It; ++It)
	{
		if (AKCFRaceCheckpoint* Checkpoint = *It)
		{
			Checkpoints.Add(Checkpoint);
		}
	}

	Checkpoints.Sort([](const AKCFRaceCheckpoint& A, const AKCFRaceCheckpoint& B)
	{
		return A.CheckpointIndex < B.CheckpointIndex;
	});

	if (Checkpoints.Num() == 0)
	{
		UE_LOG(LogKCFMatch, Error,
			TEXT("Race mode is running on a level with no AKCFRaceCheckpoint actors. No lap can ever be completed; ")
			TEXT("place checkpoints with increasing CheckpointIndex and mark the last one as the finish line."));
		return;
	}

	// The finish line has to be the final gate, otherwise a lap would be counted somewhere in the
	// middle of the circuit and the lap times would be meaningless.
	const AKCFRaceCheckpoint* Last = Checkpoints.Last();
	if (!Last->bIsFinishLine)
	{
		UE_LOG(LogKCFMatch, Warning,
			TEXT("The highest-indexed checkpoint ('%s') is not marked bIsFinishLine. Laps are counted there regardless; ")
			TEXT("mark it to make the circuit read the way it behaves."),
			*GetNameSafe(Last));
	}

	int32 FinishLineCount = 0;
	for (const AKCFRaceCheckpoint* Checkpoint : Checkpoints)
	{
		FinishLineCount += Checkpoint->bIsFinishLine ? 1 : 0;
	}

	if (FinishLineCount > 1)
	{
		UE_LOG(LogKCFMatch, Warning,
			TEXT("%d checkpoints are marked as the finish line. Only the highest-indexed gate completes a lap."),
			FinishLineCount);
	}

	UE_LOG(LogKCFMatch, Log, TEXT("Race circuit built: %d checkpoints, %d laps."), Checkpoints.Num(), LapsToWin);
}

UKCFRaceProgressComponent* AKCFRaceGameMode::EnsureRaceProgress(AKCFPlayerState* PlayerState)
{
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return nullptr;
	}

	if (UKCFRaceProgressComponent* Existing = PlayerState->FindComponentByClass<UKCFRaceProgressComponent>())
	{
		return Existing;
	}

	UKCFRaceProgressComponent* Progress = NewObject<UKCFRaceProgressComponent>(PlayerState);
	PlayerState->AddInstanceComponent(Progress);
	Progress->RegisterComponent();

	const AKCFGameState* KCFGameState = GetKCFGameState();
	Progress->BeginRace(KCFGameState ? KCFGameState->GetServerWorldTimeSeconds() : 0.f);

	return Progress;
}

UKCFRaceProgressComponent* AKCFRaceGameMode::GetRaceProgress(AKCFPlayerState* PlayerState)
{
	return PlayerState ? PlayerState->FindComponentByClass<UKCFRaceProgressComponent>() : nullptr;
}

int32 AKCFRaceGameMode::FindCheckpointOrdinal(const AKCFRaceCheckpoint* Checkpoint) const
{
	return Checkpoints.IndexOfByPredicate([Checkpoint](const TObjectPtr<AKCFRaceCheckpoint>& Entry)
	{
		return Entry == Checkpoint;
	});
}

// -------------------------------------------------------------------------------------------------
// Progress
// -------------------------------------------------------------------------------------------------

void AKCFRaceGameMode::NotifyCheckpointReached(AKCFKartPawn* Kart, AKCFRaceCheckpoint* Checkpoint)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!HasAuthority() || !Kart || !Checkpoint || !KCFGameState)
	{
		return;
	}

	// Gates only count once the flag has dropped. Without this a kart nudged over the line during
	// the countdown would start the race a gate ahead.
	if (!KCFGameState->IsMatchActive())
	{
		return;
	}

	AKCFPlayerState* PlayerState = Kart->GetPlayerState<AKCFPlayerState>();
	UKCFRaceProgressComponent* Progress = GetRaceProgress(PlayerState);
	if (!Progress || Progress->HasFinished())
	{
		return;
	}

	const int32 Ordinal = FindCheckpointOrdinal(Checkpoint);
	if (Ordinal == INDEX_NONE)
	{
		// A checkpoint spawned after BeginPlay is not part of the circuit this race was built on.
		// Counting it would make one racer's lap longer than another's.
		UE_LOG(LogKCFMatch, Warning, TEXT("Checkpoint '%s' is not part of the circuit; ignoring."), *GetNameSafe(Checkpoint));
		return;
	}

	const float ServerTime = GameState->GetServerWorldTimeSeconds();
	const bool bCompletedLap = Progress->RecordCheckpoint(Checkpoint, Ordinal, Checkpoints.Num(), ServerTime);

	if (!bCompletedLap)
	{
		return;
	}

	if (ScorePerLap != 0)
	{
		PlayerState->AddScore(ScorePerLap);
	}

	UE_LOG(LogKCFMatch, Verbose, TEXT("'%s' completed lap %d in %.2fs."),
		*PlayerState->GetPlayerName(), Progress->GetCompletedLaps(), Progress->GetLastLapSeconds());

	if (Progress->GetCompletedLaps() >= LapsToWin)
	{
		HandleRacerFinished(PlayerState);
	}
}

void AKCFRaceGameMode::HandleRacerFinished(AKCFPlayerState* PlayerState)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	UKCFRaceProgressComponent* Progress = GetRaceProgress(PlayerState);
	if (!PlayerState || !Progress || !KCFGameState || FinishOrder.Contains(PlayerState))
	{
		return;
	}

	const float ServerTime = KCFGameState->GetServerWorldTimeSeconds();
	Progress->MarkFinished(ServerTime);
	FinishOrder.Add(PlayerState);

	// Finishing position is worth a sliding bonus so the final scoreboard separates second from
	// last even when every racer completed the distance.
	const int32 Position = FinishOrder.Num();
	const int32 Bonus = FMath::Max(0, ScoreForFinishing - (Position - 1) * FMath::Max(1, ScoreForFinishing / 10));
	if (Bonus != 0)
	{
		PlayerState->AddScore(Bonus);
	}

	PlayerState->SetPlacement(Position);

	if (LeaderFinishServerTime < 0.f)
	{
		LeaderFinishServerTime = ServerTime;
		UE_LOG(LogKCFMatch, Log, TEXT("'%s' won the race in %.2fs."), *PlayerState->GetPlayerName(), Progress->GetTotalRaceSeconds());
	}

	// A finished racer stops driving rather than being removed: watching the rest of the field come
	// in is part of the mode, and despawning the winner's kart mid-shot looks like a crash.
	if (AController* Controller = Cast<AController>(PlayerState->GetOwner()))
	{
		if (AKCFKartPawn* Kart = Cast<AKCFKartPawn>(Controller->GetPawn()))
		{
			Kart->SetDrivingEnabled(false);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Rules
// -------------------------------------------------------------------------------------------------

bool AKCFRaceGameMode::CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return false;
	}

	OutWinningTeamId = KCF_NO_TEAM;

	const TArray<AKCFPlayerState*> Order = GetRaceOrder();
	OutWinningPlayer = FinishOrder.Num() > 0 ? FinishOrder[0].Get() : (Order.Num() > 0 ? Order[0] : nullptr);

	if (LeaderFinishServerTime < 0.f)
	{
		return false;
	}

	// Everyone home: no reason to keep the race running.
	int32 StillRacing = 0;
	for (AKCFPlayerState* Entry : Order)
	{
		const UKCFRaceProgressComponent* Progress = GetRaceProgress(Entry);
		if (Progress && !Progress->HasFinished() && !Entry->IsOnlyASpectator())
		{
			++StillRacing;
		}
	}

	if (StillRacing == 0)
	{
		OutReason = EKCFMatchEndReason::ObjectiveComplete;
		return true;
	}

	// Otherwise the stragglers get their grace window, then the race is called.
	const float Elapsed = KCFGameState->GetServerWorldTimeSeconds() - LeaderFinishServerTime;
	if (Elapsed >= GraceSecondsAfterLeaderFinishes)
	{
		OutReason = EKCFMatchEndReason::ObjectiveComplete;
		return true;
	}

	return false;
}

TArray<AKCFPlayerState*> AKCFRaceGameMode::GetRaceOrder() const
{
	TArray<AKCFPlayerState*> Order;

	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return Order;
	}

	for (APlayerState* Entry : KCFGameState->PlayerArray)
	{
		if (AKCFPlayerState* KCFPlayerState = Cast<AKCFPlayerState>(Entry))
		{
			Order.Add(KCFPlayerState);
		}
	}

	Order.Sort([](const AKCFPlayerState& A, const AKCFPlayerState& B)
	{
		const UKCFRaceProgressComponent* ProgressA = A.FindComponentByClass<UKCFRaceProgressComponent>();
		const UKCFRaceProgressComponent* ProgressB = B.FindComponentByClass<UKCFRaceProgressComponent>();

		if (!ProgressA || !ProgressB)
		{
			return ProgressA != nullptr;
		}

		// Finishers always outrank anyone still on the circuit, in the order they crossed.
		if (ProgressA->HasFinished() != ProgressB->HasFinished())
		{
			return ProgressA->HasFinished();
		}

		if (ProgressA->HasFinished() && ProgressB->HasFinished())
		{
			return ProgressA->GetTotalRaceSeconds() < ProgressB->GetTotalRaceSeconds();
		}

		// Gates passed is the right key rather than laps: it separates two racers on the same lap,
		// which laps alone cannot do.
		return ProgressA->GetTotalCheckpointsPassed() > ProgressB->GetTotalCheckpointsPassed();
	});

	return Order;
}

void AKCFRaceGameMode::AssignFinalPlacements()
{
	// Finishers already have their placement from the order they crossed the line. Everyone else is
	// ranked behind them by how far round the circuit they got.
	int32 Position = FinishOrder.Num();

	for (AKCFPlayerState* Entry : GetRaceOrder())
	{
		const UKCFRaceProgressComponent* Progress = GetRaceProgress(Entry);
		if (Progress && Progress->HasFinished())
		{
			continue;
		}

		Entry->SetPlacement(++Position);
	}
}

bool AKCFRaceGameMode::GetRespawnTransformOverride(AController* Controller, FTransform& OutTransform) const
{
	// Coming back at the gate you last passed is what keeps one mistake from costing a whole lap.
	// Only once the race is live: before the flag drops the grid is still the grid.
	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!bRespawnAtLastCheckpoint || !KCFGameState || !KCFGameState->IsMatchActive())
	{
		return false;
	}

	AKCFPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AKCFPlayerState>() : nullptr;
	const UKCFRaceProgressComponent* Progress = GetRaceProgress(PlayerState);
	const AKCFRaceCheckpoint* Checkpoint = Progress ? Progress->GetLastCheckpoint() : nullptr;

	if (!Checkpoint)
	{
		return false;
	}

	OutTransform = Checkpoint->GetRespawnTransform();
	return true;
}
