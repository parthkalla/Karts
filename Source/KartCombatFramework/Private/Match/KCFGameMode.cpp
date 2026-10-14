// Original work, MIT licensed.

#include "Match/KCFGameMode.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFDataTableRows.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Match/KCFGameState.h"
#include "Match/KCFMatchStateMachine.h"
#include "Match/KCFPlayerSpawnPoint.h"
#include "Match/KCFRespawnComponent.h"
#include "Player/KCFPlayerController.h"
#include "Player/KCFPlayerState.h"
#include "Powerups/KCFPowerupSpawner.h"
#include "Subsystems/KCFSessionSubsystem.h"
#include "TimerManager.h"
#include "Vehicles/KCFKartPawn.h"

AKCFGameMode::AKCFGameMode()
{
	PrimaryActorTick.bCanEverTick = false;

	GameStateClass = AKCFGameState::StaticClass();
	PlayerStateClass = AKCFPlayerState::StaticClass();
	PlayerControllerClass = AKCFPlayerController::StaticClass();

	// Pawns are spawned by the framework's own flow, not by the engine's automatic restart, because
	// a kart must have its vehicle definition before BeginPlay runs.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
	bUseSeamlessTravel = true;

	KartPawnClass = AKCFKartPawn::StaticClass();

	RespawnComponent = CreateDefaultSubobject<UKCFRespawnComponent>(TEXT("RespawnComponent"));
}

void AKCFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (!GameModeConfig)
	{
		// Soft default from project settings. Loading synchronously here is acceptable: it happens
		// once, before any player is connected, and the asset is small.
		const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
		if (Settings && !Settings->DefaultGameModeConfig.IsNull())
		{
			GameModeConfig = Settings->DefaultGameModeConfig.LoadSynchronous();
		}
	}

#if !UE_BUILD_SHIPPING
	if (GameModeConfig)
	{
		TArray<FText> ConfigErrors;
		if (!GameModeConfig->ValidateConfiguration(ConfigErrors))
		{
			for (const FText& Error : ConfigErrors)
			{
				UE_LOG(LogKCFMatch, Warning, TEXT("Game mode configuration problem: %s"), *Error.ToString());
			}
		}
	}
#endif
}

void AKCFGameMode::BeginPlay()
{
	Super::BeginPlay();

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();

	if (AKCFGameState* KCFGameState = GetKCFGameState())
	{
		KCFGameState->SetGameModeConfig(GameModeConfig);
		KCFGameState->InitialiseTeams(Config->TeamCount);
	}

	// One repeating second-resolution timer drives every phase of the match.
	GetWorldTimerManager().SetTimer(MatchClockHandle, this, &AKCFGameMode::TickMatchClock, 1.f, true);
}

void AKCFGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(MatchClockHandle);
	Super::EndPlay(EndPlayReason);
}

AKCFGameState* AKCFGameMode::GetKCFGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AKCFGameState>() : nullptr;
}

// -------------------------------------------------------------------------------------------------
// Login / logout
// -------------------------------------------------------------------------------------------------

void AKCFGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	AKCFPlayerState* KCFPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AKCFPlayerState>() : nullptr;
	if (!KCFPlayerState)
	{
		return;
	}

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();

	KCFPlayerState->ResetMatchStats(Config->LivesPerPlayer);

	if (Config->IsTeamMode())
	{
		IKCFTeamAgent::Execute_SetKCFTeamId(KCFPlayerState, AssignTeamForPlayer(KCFPlayerState));
	}

	// A player who connects mid-match either joins as a participant or sits out, depending on the
	// rule set. Spectating is the safe default because it never disturbs a match in progress.
	AKCFGameState* KCFGameState = GetKCFGameState();
	const EKCFMatchState CurrentState = KCFGameState ? KCFGameState->GetMatchState() : EKCFMatchState::WaitingForPlayers;

	if (UKCFMatchStateMachine::IsLiveState(CurrentState))
	{
		if (Config->bAllowJoinInProgress)
		{
			KCFPlayerState->SetIsOnlyASpectator(false);
			RespawnComponent->EnqueueRespawn(NewPlayer, Config->RespawnDelaySeconds);
		}
		else
		{
			KCFPlayerState->SetIsOnlyASpectator(true);
		}
	}
	else
	{
		KCFPlayerState->SetIsOnlyASpectator(false);
	}

	if (KCFGameState)
	{
		KCFGameState->RefreshTeamPopulations();
	}

	BP_OnPlayerJoined(KCFPlayerState);
}

void AKCFGameMode::Logout(AController* Exiting)
{
	AKCFPlayerState* KCFPlayerState = Exiting ? Exiting->GetPlayerState<AKCFPlayerState>() : nullptr;

	RespawnComponent->CancelRespawn(Exiting);

	if (KCFPlayerState)
	{
		BP_OnPlayerLeft(KCFPlayerState);
	}

	Super::Logout(Exiting);

	if (AKCFGameState* KCFGameState = GetKCFGameState())
	{
		KCFGameState->RefreshTeamPopulations();
	}

	// A match that drops below the abort threshold ends rather than leaving one player driving alone.
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	AKCFGameState* KCFGameStateCheck = GetKCFGameState();
	if (KCFGameStateCheck
		&& Config->AbortBelowPlayerCount > 0
		&& UKCFMatchStateMachine::IsLiveState(KCFGameStateCheck->GetMatchState())
		&& KCFGameStateCheck->GetParticipantCount() < Config->AbortBelowPlayerCount)
	{
		EndMatch(EKCFMatchEndReason::NotEnoughPlayers);
	}
}

void AKCFGameMode::RestartPlayer(AController* NewPlayer)
{
	// Deliberately bypasses AGameModeBase::RestartPlayer. The framework needs the vehicle definition
	// applied before BeginPlay, which the engine's deferred-free spawn path does not allow.
	if (!NewPlayer)
	{
		return;
	}

	RespawnComponent->RespawnNow(NewPlayer);
}

AActor* AKCFGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (AKCFPlayerSpawnPoint* Point = RespawnComponent->SelectSpawnPoint(Player))
	{
		return Point;
	}

	// Falls back to the engine's search, which finds plain APlayerStart actors. A level built before
	// the plugin was added still works.
	return Super::ChoosePlayerStart_Implementation(Player);
}

// -------------------------------------------------------------------------------------------------
// Match flow
// -------------------------------------------------------------------------------------------------

void AKCFGameMode::TickMatchClock()
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return;
	}

	const EKCFMatchState State = KCFGameState->GetMatchState();
	const float Remaining = KCFGameState->GetRemainingStateTime();
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();

	switch (State)
	{
	case EKCFMatchState::WaitingForPlayers:
		UpdateWaitingForPlayers();
		break;

	case EKCFMatchState::Preparing:
		if (Remaining <= 0.f)
		{
			EnterMatchState(EKCFMatchState::Countdown);
		}
		break;

	case EKCFMatchState::Countdown:
		if (Remaining <= 0.f)
		{
			EnterMatchState(EKCFMatchState::InProgress);
		}
		break;

	case EKCFMatchState::InProgress:
	{
		EKCFMatchEndReason Reason = EKCFMatchEndReason::Unknown;
		int32 WinningTeam = KCF_NO_TEAM;
		AKCFPlayerState* WinningPlayer = nullptr;

		if (CheckWinCondition(Reason, WinningTeam, WinningPlayer))
		{
			KCFGameState->SetMatchResult(Reason, WinningTeam, WinningPlayer);
			EnterMatchState(EKCFMatchState::Finished);
		}
		else if (Config->MatchDurationSeconds > 0.f && Remaining <= 0.f)
		{
			// Time expired. Sudden death only makes sense when the result is genuinely tied.
			EKCFMatchEndReason TimeReason = EKCFMatchEndReason::TimeLimit;
			int32 TimeWinningTeam = KCF_NO_TEAM;
			AKCFPlayerState* TimeWinningPlayer = nullptr;
			const bool bDecided = CheckWinCondition(TimeReason, TimeWinningTeam, TimeWinningPlayer);

			if (!bDecided && Config->bUseSuddenDeath)
			{
				EnterMatchState(EKCFMatchState::SuddenDeath);
			}
			else
			{
				KCFGameState->SetMatchResult(EKCFMatchEndReason::TimeLimit, TimeWinningTeam, TimeWinningPlayer);
				EnterMatchState(EKCFMatchState::Finished);
			}
		}
		break;
	}

	case EKCFMatchState::SuddenDeath:
	{
		EKCFMatchEndReason Reason = EKCFMatchEndReason::Unknown;
		int32 WinningTeam = KCF_NO_TEAM;
		AKCFPlayerState* WinningPlayer = nullptr;

		if (CheckWinCondition(Reason, WinningTeam, WinningPlayer))
		{
			KCFGameState->SetMatchResult(Reason, WinningTeam, WinningPlayer);
			EnterMatchState(EKCFMatchState::Finished);
		}
		else if (Config->SuddenDeathSeconds > 0.f && Remaining <= 0.f)
		{
			// Hard stop so a stalemate cannot run forever; the leader on score takes it.
			KCFGameState->SetMatchResult(EKCFMatchEndReason::TimeLimit, WinningTeam, WinningPlayer);
			EnterMatchState(EKCFMatchState::Finished);
		}
		break;
	}

	case EKCFMatchState::Finished:
		if (Remaining <= 0.f)
		{
			EnterMatchState(EKCFMatchState::ReturningToLobby);
		}
		break;

	default:
		break;
	}
}

void AKCFGameMode::UpdateWaitingForPlayers()
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	if (!KCFGameState)
	{
		return;
	}

	const int32 Participants = KCFGameState->GetParticipantCount();
	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	if (Participants < Config->MinPlayers)
	{
		// Dropping back below the minimum cancels the grace countdown rather than starting a match
		// that would immediately abort.
		LobbyGraceEndTime = -1.f;
		return;
	}

	if (Config->LobbyGraceSeconds <= 0.f)
	{
		EnterMatchState(EKCFMatchState::Preparing);
		return;
	}

	if (LobbyGraceEndTime < 0.f)
	{
		LobbyGraceEndTime = Now + Config->LobbyGraceSeconds;
		UE_LOG(LogKCFMatch, Log, TEXT("Minimum player count reached; starting in %.0f seconds."), Config->LobbyGraceSeconds);
		return;
	}

	if (Now >= LobbyGraceEndTime || Participants >= Config->MaxPlayers)
	{
		EnterMatchState(EKCFMatchState::Preparing);
	}
}

void AKCFGameMode::ForceStartMatch()
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (KCFGameState && KCFGameState->GetMatchState() == EKCFMatchState::WaitingForPlayers)
	{
		EnterMatchState(EKCFMatchState::Preparing);
	}
}

void AKCFGameMode::EndMatch(EKCFMatchEndReason Reason)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState || UKCFMatchStateMachine::IsTerminalState(KCFGameState->GetMatchState()))
	{
		return;
	}

	EKCFMatchEndReason IgnoredReason = Reason;
	int32 WinningTeam = KCF_NO_TEAM;
	AKCFPlayerState* WinningPlayer = nullptr;
	CheckWinCondition(IgnoredReason, WinningTeam, WinningPlayer);

	KCFGameState->SetMatchResult(Reason, WinningTeam, WinningPlayer);
	EnterMatchState(EKCFMatchState::Finished);
}

void AKCFGameMode::RequestMatchState(EKCFMatchState NewState)
{
	EnterMatchState(NewState);
}

void AKCFGameMode::EnterMatchState(EKCFMatchState NewState)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return;
	}

	const EKCFMatchState OldState = KCFGameState->GetMatchState();
	if (!UKCFMatchStateMachine::IsTransitionAllowed(OldState, NewState))
	{
		return;
	}

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();

	float Duration = -1.f;
	switch (NewState)
	{
	case EKCFMatchState::Preparing:		Duration = Config->PreparingSeconds; break;
	case EKCFMatchState::Countdown:		Duration = Config->CountdownSeconds; break;
	case EKCFMatchState::InProgress:	Duration = Config->MatchDurationSeconds; break;
	case EKCFMatchState::SuddenDeath:	Duration = Config->SuddenDeathSeconds; break;
	case EKCFMatchState::Finished:		Duration = Config->PostMatchSeconds; break;
	default:							break;
	}

	KCFGameState->SetMatchState(NewState, Duration);

	switch (NewState)
	{
	case EKCFMatchState::Preparing:
		bMatchResultDecided = false;
		SpawnAllPlayers();
		// Karts exist but are frozen, so clients can finish streaming and see the arena before the
		// countdown starts.
		SetAllKartsMovementEnabled(false);
		break;

	case EKCFMatchState::Countdown:
		SetAllKartsMovementEnabled(false);
		break;

	case EKCFMatchState::InProgress:
		SetAllKartsMovementEnabled(true);
		SetPowerupSpawnersEnabled(true);
		NotifySessionMatchStarted();
		break;

	case EKCFMatchState::SuddenDeath:
		UE_LOG(LogKCFMatch, Log, TEXT("Entering sudden death."));
		if (Config->bDisableRespawnDuringSuddenDeath)
		{
			RespawnComponent->ClearQueue();
		}
		break;

	case EKCFMatchState::Finished:
		bMatchResultDecided = true;
		RespawnComponent->ClearQueue();
		AssignFinalPlacements();
		SetAllKartsMovementEnabled(false);
		SetPowerupSpawnersEnabled(false, /*bRemoveExistingPickups*/ true);
		NotifySessionMatchEnded();
		break;

	case EKCFMatchState::ReturningToLobby:
		RespawnComponent->ClearQueue();
		break;

	default:
		break;
	}

	OnMatchStateChanged(OldState, NewState);
	BP_OnMatchStateChanged(OldState, NewState);
}

void AKCFGameMode::OnMatchStateChanged(EKCFMatchState /*OldState*/, EKCFMatchState /*NewState*/)
{
}

void AKCFGameMode::SpawnAllPlayers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstControllerIterator It = World->GetControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (!Controller || Controller->GetPawn())
		{
			continue;
		}

		const AKCFPlayerState* KCFPlayerState = Controller->GetPlayerState<AKCFPlayerState>();
		if (!KCFPlayerState || KCFPlayerState->IsOnlyASpectator() || KCFPlayerState->IsEliminated())
		{
			continue;
		}

		RespawnComponent->RespawnNow(Controller);
	}
}

void AKCFGameMode::SetAllKartsMovementEnabled(bool bEnabled)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AKCFKartPawn> It(World); It; ++It)
	{
		if (AKCFKartPawn* Kart = *It)
		{
			Kart->SetDrivingEnabled(bEnabled);
		}
	}
}

void AKCFGameMode::SetPowerupSpawnersEnabled(bool bEnabled, bool bRemoveExistingPickups)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AKCFPowerupSpawner> It(World); It; ++It)
	{
		AKCFPowerupSpawner* Spawner = *It;
		if (!Spawner || !Spawner->ShouldAutoStart())
		{
			// A spawner with auto-start cleared is driven by level script or a Blueprint, and the
			// match flow must not override that decision in either direction.
			continue;
		}

		if (bEnabled)
		{
			Spawner->StartSpawning();
		}
		else
		{
			Spawner->StopSpawning(bRemoveExistingPickups);
		}
	}
}

void AKCFGameMode::NotifySessionMatchStarted()
{
	if (UKCFSessionSubsystem* Sessions = UKCFSessionSubsystem::Get(this))
	{
		Sessions->NotifyMatchStarted();
	}
}

void AKCFGameMode::NotifySessionMatchEnded()
{
	if (UKCFSessionSubsystem* Sessions = UKCFSessionSubsystem::Get(this))
	{
		Sessions->NotifyMatchEnded();
	}
}

// -------------------------------------------------------------------------------------------------
// Spawning
// -------------------------------------------------------------------------------------------------

bool AKCFGameMode::SpawnKartForController(AController* Controller, const FTransform& SpawnTransform)
{
	UWorld* World = GetWorld();
	if (!World || !Controller || !KartPawnClass)
	{
		UE_LOG(LogKCFMatch, Error, TEXT("Cannot spawn kart: KartPawnClass is unset on '%s'."), *GetNameSafe(this));
		return false;
	}

	if (Controller->GetPawn())
	{
		return false;
	}

	AKCFPlayerState* KCFPlayerState = Controller->GetPlayerState<AKCFPlayerState>();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = nullptr;
	// Karts are large; refusing to spawn because a spawn point is momentarily occupied would be a
	// worse outcome than briefly overlapping another kart, which physics resolves within a frame.
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// Deferred spawn so the vehicle definition is in place before BeginPlay: components read it to
	// size collision, configure handling and set up health.
	AKCFKartPawn* Kart = World->SpawnActorDeferred<AKCFKartPawn>(
		KartPawnClass,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

	if (!Kart)
	{
		UE_LOG(LogKCFMatch, Error, TEXT("Failed to spawn kart of class '%s'."), *GetNameSafe(KartPawnClass));
		return false;
	}

	if (UKCFVehicleDataAsset* Vehicle = ResolveVehicleForPlayer(KCFPlayerState))
	{
		Kart->SetVehicleData(Vehicle);
	}

	Kart->FinishSpawning(SpawnTransform);

	Controller->Possess(Kart);

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	Kart->ApplySpawnProtection(Config->SpawnProtectionSeconds, Config->bSpawnProtectionBreaksOnFire);

	// Karts spawned before the start whistle stay frozen until InProgress.
	AKCFGameState* KCFGameState = GetKCFGameState();
	const bool bLive = KCFGameState && UKCFMatchStateMachine::IsLiveState(KCFGameState->GetMatchState());
	Kart->SetDrivingEnabled(bLive);

	return true;
}

bool AKCFGameMode::ShouldRespawnController(AController* Controller) const
{
	if (!Controller)
	{
		return false;
	}

	const AKCFPlayerState* KCFPlayerState = Controller->GetPlayerState<AKCFPlayerState>();
	if (!KCFPlayerState || KCFPlayerState->IsOnlyASpectator() || KCFPlayerState->IsEliminated())
	{
		return false;
	}

	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState || !UKCFMatchStateMachine::ShouldPlayersBeSpawned(KCFGameState->GetMatchState()))
	{
		return false;
	}

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();

	if (KCFGameState->GetMatchState() == EKCFMatchState::SuddenDeath && Config->bDisableRespawnDuringSuddenDeath)
	{
		return false;
	}

	// Respawning is allowed during Preparing and Countdown regardless of bRespawnEnabled: that is
	// the initial spawn, not a respawn.
	if (!Config->bRespawnEnabled && UKCFMatchStateMachine::IsLiveState(KCFGameState->GetMatchState()))
	{
		return false;
	}

	return true;
}

void AKCFGameMode::RequestImmediateRespawn(AController* Controller)
{
	if (ShouldRespawnController(Controller))
	{
		RespawnComponent->RespawnNow(Controller);
	}
}

// -------------------------------------------------------------------------------------------------
// Teams and vehicles
// -------------------------------------------------------------------------------------------------

void AKCFGameMode::RequestTeamChange(AController* Controller, int32 DesiredTeamId)
{
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	AKCFPlayerState* KCFPlayerState = Controller ? Controller->GetPlayerState<AKCFPlayerState>() : nullptr;

	if (!KCFPlayerState || !Config->IsTeamMode() || !Config->bAllowTeamSelection)
	{
		return;
	}

	if (DesiredTeamId < 0 || DesiredTeamId >= Config->TeamCount)
	{
		return;
	}

	if (KCFPlayerState->GetTeamId() == DesiredTeamId)
	{
		return;
	}

	// Balance check. Without it a lobby would stack onto one team and the match would be pointless.
	if (Config->MaxTeamSizeImbalance > 0)
	{
		int32 SmallestTeamSize = MAX_int32;
		for (int32 TeamIndex = 0; TeamIndex < Config->TeamCount; ++TeamIndex)
		{
			if (TeamIndex != DesiredTeamId)
			{
				SmallestTeamSize = FMath::Min(SmallestTeamSize, GetTeamPlayerCount(TeamIndex));
			}
		}

		const int32 TargetSizeAfterMove = GetTeamPlayerCount(DesiredTeamId) + 1;
		if (SmallestTeamSize != MAX_int32 && TargetSizeAfterMove - SmallestTeamSize > Config->MaxTeamSizeImbalance)
		{
			UE_LOG(LogKCFMatch, Verbose, TEXT("Team change refused for '%s': would unbalance the teams."), *KCFPlayerState->GetPlayerName());
			return;
		}
	}

	IKCFTeamAgent::Execute_SetKCFTeamId(KCFPlayerState, DesiredTeamId);

	if (AKCFGameState* KCFGameState = GetKCFGameState())
	{
		KCFGameState->RefreshTeamPopulations();
	}
}

int32 AKCFGameMode::AssignTeamForPlayer(AKCFPlayerState* PlayerState) const
{
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	if (!Config->IsTeamMode())
	{
		return KCF_NO_TEAM;
	}

	// Fill the smallest team. With equal sizes the lowest index wins, which keeps assignment
	// deterministic and therefore testable.
	int32 BestTeam = 0;
	int32 BestCount = MAX_int32;

	for (int32 TeamIndex = 0; TeamIndex < Config->TeamCount; ++TeamIndex)
	{
		const int32 Count = GetTeamPlayerCount(TeamIndex);
		if (Count < BestCount)
		{
			BestCount = Count;
			BestTeam = TeamIndex;
		}
	}

	return BestTeam;
}

int32 AKCFGameMode::GetTeamPlayerCount(int32 TeamId) const
{
	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return 0;
	}

	int32 Count = 0;
	for (const APlayerState* Entry : KCFGameState->PlayerArray)
	{
		const AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (KCFPlayer && !KCFPlayer->IsOnlyASpectator() && KCFPlayer->GetTeamId() == TeamId)
		{
			++Count;
		}
	}
	return Count;
}

bool AKCFGameMode::IsVehicleSelectable(FName VehicleId) const
{
	if (VehicleId.IsNone())
	{
		return false;
	}

	if (!VehicleCatalogue)
	{
		// With no catalogue authored there is nothing to validate against. Refusing every selection
		// is the safe answer: the fallback vehicle is used instead, and no client can smuggle in an
		// arbitrary id.
		return false;
	}

	bool bFound = false;
	VehicleCatalogue->ForeachRow<FKCFVehicleRow>(TEXT("IsVehicleSelectable"),
		[&bFound, VehicleId](const FName& /*Key*/, const FKCFVehicleRow& Row)
		{
			if (!bFound && Row.bEnabled && Row.Id == VehicleId)
			{
				bFound = true;
			}
		});

	return bFound;
}

UKCFVehicleDataAsset* AKCFGameMode::ResolveVehicleForPlayer(const AKCFPlayerState* PlayerState) const
{
	const FName RequestedId = PlayerState ? PlayerState->GetSelectedVehicleId() : NAME_None;

	if (!RequestedId.IsNone() && VehicleCatalogue)
	{
		UKCFVehicleDataAsset* Resolved = nullptr;

		VehicleCatalogue->ForeachRow<FKCFVehicleRow>(TEXT("ResolveVehicleForPlayer"),
			[&Resolved, RequestedId](const FName& /*Key*/, const FKCFVehicleRow& Row)
			{
				if (!Resolved && Row.bEnabled && Row.Id == RequestedId && !Row.Vehicle.IsNull())
				{
					// Synchronous load at spawn time. The alternative - spawning a kart with no
					// definition and patching it when the load completes - produces a kart that
					// exists but cannot be driven, which is far worse than one blocking load.
					Resolved = Row.Vehicle.LoadSynchronous();
				}
			});

		if (Resolved)
		{
			return Resolved;
		}
	}

	if (FallbackVehicle)
	{
		return FallbackVehicle;
	}

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	if (Settings && !Settings->DefaultVehicle.IsNull())
	{
		return Settings->DefaultVehicle.LoadSynchronous();
	}

	UE_LOG(LogKCFMatch, Error,
		TEXT("No vehicle could be resolved. Set FallbackVehicle on the game mode or DefaultVehicle in Project Settings > Game > Kart Combat Framework."));
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// Death, scoring and win conditions
// -------------------------------------------------------------------------------------------------

void AKCFGameMode::HandleKartDeath(AKCFPlayerState* Victim, AKCFPlayerState* Killer, FName WeaponId, const TArray<AKCFPlayerState*>& Assisters)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!Victim || !KCFGameState)
	{
		return;
	}

	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	const bool bTeamKill = Killer
		&& Killer != Victim
		&& Config->IsTeamMode()
		&& Killer->GetTeamId() == Victim->GetTeamId();

	Victim->RecordDeath();
	Victim->MarkDeathTime(KCFGameState->GetServerWorldTimeSeconds());

	if (Killer && Killer != Victim)
	{
		Killer->RecordKill(bTeamKill);

		if (AKCFPlayerController* KillerController = Cast<AKCFPlayerController>(Killer->GetOwner()))
		{
			KillerController->ClientNotifyKill(Victim, WeaponId);
		}
	}

	for (AKCFPlayerState* Assister : Assisters)
	{
		if (Assister && Assister != Killer && Assister != Victim)
		{
			Assister->RecordAssist();
		}
	}

	ScoreElimination(Victim, Killer, Assisters);

	KCFGameState->AddKillFeedEntry(Killer, Victim, WeaponId, bTeamKill);

	// Elimination bookkeeping: a player with lives enabled who has run out is out for good.
	if (Config->LivesPerPlayer > 0 && Victim->GetLivesRemaining() <= 0)
	{
		Victim->SetEliminated(true);
		OnPlayerEliminated(Victim);
	}

	KCFGameState->RefreshTeamPopulations();

	BP_OnKartDeath(Victim, Killer);

	if (!bMatchResultDecided)
	{
		EKCFMatchEndReason Reason = EKCFMatchEndReason::Unknown;
		int32 WinningTeam = KCF_NO_TEAM;
		AKCFPlayerState* WinningPlayer = nullptr;

		if (CheckWinCondition(Reason, WinningTeam, WinningPlayer))
		{
			KCFGameState->SetMatchResult(Reason, WinningTeam, WinningPlayer);
			EnterMatchState(EKCFMatchState::Finished);
			return;
		}
	}

	if (AController* VictimController = Cast<AController>(Victim->GetOwner()))
	{
		if (ShouldRespawnController(VictimController))
		{
			RespawnComponent->EnqueueRespawn(VictimController, Config->RespawnDelaySeconds);
		}
	}
}

void AKCFGameMode::ScoreElimination(AKCFPlayerState* Victim, AKCFPlayerState* Killer, const TArray<AKCFPlayerState*>& Assisters)
{
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	AKCFGameState* KCFGameState = GetKCFGameState();

	if (Victim && Config->ScorePerDeath != 0)
	{
		Victim->AddScore(Config->ScorePerDeath);
	}

	if (Killer && Killer != Victim)
	{
		const bool bTeamKill = Config->IsTeamMode() && Killer->GetTeamId() == Victim->GetTeamId();
		const int32 Points = bTeamKill ? Config->ScorePerTeamKill : Config->ScorePerKill;

		Killer->AddScore(Points);

		if (KCFGameState && Config->IsTeamMode())
		{
			KCFGameState->AddTeamScore(Killer->GetTeamId(), Points);
		}
	}

	if (Config->ScorePerAssist != 0)
	{
		for (AKCFPlayerState* Assister : Assisters)
		{
			if (Assister && Assister != Killer && Assister != Victim)
			{
				Assister->AddScore(Config->ScorePerAssist);
			}
		}
	}
}

bool AKCFGameMode::CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer)
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	const UKCFGameModeDataAsset* Config = GetEffectiveConfig();
	if (!KCFGameState)
	{
		return false;
	}

	// Record the current leader regardless of whether the match ends, so that a time-limit finish
	// still has a winner to display.
	const TArray<AKCFPlayerState*> Sorted = KCFGameState->GetSortedPlayerStates();
	OutWinningPlayer = Sorted.Num() > 0 ? Sorted[0] : nullptr;

	if (Config->IsTeamMode())
	{
		int32 LeadingTeam = KCF_NO_TEAM;
		int32 LeadingScore = MIN_int32;
		bool bTied = false;

		for (const FKCFTeamScore& Entry : KCFGameState->GetTeamScores())
		{
			if (Entry.Score > LeadingScore)
			{
				LeadingScore = Entry.Score;
				LeadingTeam = Entry.TeamId;
				bTied = false;
			}
			else if (Entry.Score == LeadingScore)
			{
				bTied = true;
			}
		}

		OutWinningTeamId = bTied ? KCF_NO_TEAM : LeadingTeam;

		if (Config->ScoreLimit > 0 && LeadingScore >= Config->ScoreLimit && !bTied)
		{
			OutReason = EKCFMatchEndReason::ScoreLimit;
			return true;
		}
	}
	else
	{
		OutWinningTeamId = KCF_NO_TEAM;

		if (Config->ScoreLimit > 0 && OutWinningPlayer && OutWinningPlayer->GetKCFScore() >= Config->ScoreLimit)
		{
			// Guard against declaring a winner on a tie at the limit.
			const bool bTied = Sorted.Num() > 1 && Sorted[1]->GetKCFScore() == OutWinningPlayer->GetKCFScore();
			if (!bTied)
			{
				OutReason = EKCFMatchEndReason::ScoreLimit;
				return true;
			}
		}
	}

	return false;
}

void AKCFGameMode::OnPlayerEliminated(AKCFPlayerState* PlayerState)
{
	// Base behaviour: the eliminated player watches the rest of the match rather than being
	// disconnected. Subclasses that care about last-player-standing override CheckWinCondition.
	if (PlayerState)
	{
		if (AController* Controller = Cast<AController>(PlayerState->GetOwner()))
		{
			RespawnComponent->CancelRespawn(Controller);
		}
	}
}

bool AKCFGameMode::GetRespawnTransformOverride(AController* /*Controller*/, FTransform& /*OutTransform*/) const
{
	return false;
}

void AKCFGameMode::AssignFinalPlacements()
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return;
	}

	const TArray<AKCFPlayerState*> Sorted = KCFGameState->GetSortedPlayerStates();
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		Sorted[Index]->SetPlacement(Index + 1);
	}
}

// -------------------------------------------------------------------------------------------------
// Configuration fallback
// -------------------------------------------------------------------------------------------------

const UKCFGameModeDataAsset* AKCFGameMode::GetEffectiveConfig() const
{
	return GameModeConfig ? GameModeConfig : GetOrCreateFallbackConfig();
}

const UKCFGameModeDataAsset* AKCFGameMode::GetOrCreateFallbackConfig() const
{
	if (!FallbackConfig)
	{
		UE_LOG(LogKCFMatch, Warning,
			TEXT("No UKCFGameModeDataAsset is configured. Running on built-in defaults: 2-8 players, 5 minute free-for-all. ")
			TEXT("Set GameModeConfig on the game mode or DefaultGameModeConfig in Project Settings."));

		// const_cast is confined to this lazy-initialisation site. The alternative is making every
		// rule accessor non-const, which would be a worse trade for one cached fallback.
		AKCFGameMode* MutableThis = const_cast<AKCFGameMode*>(this);
		MutableThis->FallbackConfig = NewObject<UKCFGameModeDataAsset>(MutableThis, TEXT("KCFFallbackGameModeConfig"));
		MutableThis->FallbackConfig->Id = TEXT("Fallback");
	}

	return FallbackConfig;
}
