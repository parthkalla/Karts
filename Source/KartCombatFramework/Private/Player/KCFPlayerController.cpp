// Original work, MIT licensed.

#include "Player/KCFPlayerController.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Engine/World.h"
#include "Input/KCFKartInputComponent.h"
#include "Match/KCFGameMode.h"
#include "Match/KCFGameState.h"
#include "Match/KCFMatchStateMachine.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"
#include "Vehicles/KCFKartPawn.h"

AKCFPlayerController::AKCFPlayerController()
{
	PrimaryActorTick.bCanEverTick = false;

	// Created unconditionally, including on a dedicated server. The component itself only ticks and
	// binds hardware input on a machine that owns a local player; on the server it exists purely so
	// that the replicated input RPCs have a consistent home.
	KartInput = CreateDefaultSubobject<UKCFKartInputComponent>(TEXT("KartInput"));
}

void AKCFPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		// Enhanced Input mapping contexts are added by the input component, which owns that concern.
		KartInput->InitialiseLocalInput();
	}
}

void AKCFPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AKCFPlayerController, RespawnAvailableServerTime, COND_OwnerOnly);
}

void AKCFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	KartInput->BindInputActions(InputComponent);
}

void AKCFPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AKCFKartPawn* Kart = Cast<AKCFKartPawn>(InPawn);
	KartInput->SetControlledKart(Kart);
	OnPossessedKartChanged.Broadcast(Kart);
}

void AKCFPlayerController::OnUnPossess()
{
	KartInput->SetControlledKart(nullptr);
	OnPossessedKartChanged.Broadcast(nullptr);

	Super::OnUnPossess();
}

void AKCFPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);

	// Runs on the owning client, where OnPossess does not. Without this the client's input component
	// would have no kart to drive until something else told it.
	AKCFKartPawn* Kart = Cast<AKCFKartPawn>(InPawn);
	KartInput->SetControlledKart(Kart);
	OnPossessedKartChanged.Broadcast(Kart);
}

// -------------------------------------------------------------------------------------------------
// Accessors
// -------------------------------------------------------------------------------------------------

AKCFPlayerState* AKCFPlayerController::GetKCFPlayerState() const
{
	return GetPlayerState<AKCFPlayerState>();
}

AKCFKartPawn* AKCFPlayerController::GetKartPawn() const
{
	return Cast<AKCFKartPawn>(GetPawn());
}

AKCFGameState* AKCFPlayerController::GetKCFGameState() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<AKCFGameState>() : nullptr;
}

float AKCFPlayerController::GetRespawnCountdown() const
{
	if (RespawnAvailableServerTime < 0.f)
	{
		return 0.f;
	}

	const AKCFGameState* GameState = GetKCFGameState();
	const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : 0.f;
	return FMath::Max(0.f, RespawnAvailableServerTime - Now);
}

bool AKCFPlayerController::CanRequestRespawn() const
{
	if (GetPawn() != nullptr)
	{
		return false;
	}

	const AKCFPlayerState* KCFPlayerState = GetKCFPlayerState();
	if (!KCFPlayerState || KCFPlayerState->IsEliminated())
	{
		return false;
	}

	const AKCFGameState* GameState = GetKCFGameState();
	if (!GameState || !UKCFMatchStateMachine::ShouldPlayersBeSpawned(GameState->GetMatchState()))
	{
		return false;
	}

	return GetRespawnCountdown() <= 0.f;
}

// -------------------------------------------------------------------------------------------------
// Client-callable requests
// -------------------------------------------------------------------------------------------------

void AKCFPlayerController::RequestReady(bool bNewReady)
{
	ServerSetReady(bNewReady);
}

void AKCFPlayerController::RequestTeamChange(int32 DesiredTeamId)
{
	ServerRequestTeamChange(DesiredTeamId);
}

void AKCFPlayerController::RequestRespawn()
{
	ServerRequestRespawn();
}

void AKCFPlayerController::RequestVehicleSelection(FName VehicleId)
{
	ServerRequestVehicleSelection(VehicleId);
}

void AKCFPlayerController::RequestPause(bool bPaused)
{
	// Deliberately local-only. Pausing a networked match would either stop the world for everyone or
	// do nothing at all; the framework raises an event and lets the project decide (usually: open a
	// menu while the kart keeps driving).
	OnPauseRequested.Broadcast(bPaused);
}

void AKCFPlayerController::RequestKartReset()
{
	ServerRequestKartReset();
}

// -------------------------------------------------------------------------------------------------
// Server RPCs
// -------------------------------------------------------------------------------------------------

bool AKCFPlayerController::ServerSetReady_Validate(bool /*bNewReady*/)
{
	// A bool has no invalid representation; all real checking belongs in the implementation.
	return true;
}

void AKCFPlayerController::ServerSetReady_Implementation(bool bNewReady)
{
	AKCFPlayerState* KCFPlayerState = GetKCFPlayerState();
	if (!KCFPlayerState)
	{
		LogRejectedRequest(TEXT("SetReady"), TEXT("no player state"));
		return;
	}

	const AKCFGameState* GameState = GetKCFGameState();
	if (!GameState || GameState->GetMatchState() != EKCFMatchState::WaitingForPlayers)
	{
		LogRejectedRequest(TEXT("SetReady"), TEXT("match is not in the lobby state"));
		return;
	}

	KCFPlayerState->SetReady(bNewReady);
}

bool AKCFPlayerController::ServerRequestTeamChange_Validate(int32 DesiredTeamId)
{
	// Structural bound only: anything wildly out of range is a malformed or hostile packet.
	return DesiredTeamId >= KCF_NO_TEAM && DesiredTeamId < 64;
}

void AKCFPlayerController::ServerRequestTeamChange_Implementation(int32 DesiredTeamId)
{
	UWorld* World = GetWorld();
	AKCFGameMode* GameMode = World ? World->GetAuthGameMode<AKCFGameMode>() : nullptr;
	if (!GameMode)
	{
		LogRejectedRequest(TEXT("TeamChange"), TEXT("no authoritative game mode"));
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastTeamChangeRequestTime < TeamChangeCooldownSeconds)
	{
		LogRejectedRequest(TEXT("TeamChange"), TEXT("rate limited"));
		return;
	}
	LastTeamChangeRequestTime = Now;

	// The game mode owns team rules (selection allowed, balance limits, mode is actually a team
	// mode). The controller does not re-implement them.
	GameMode->RequestTeamChange(this, DesiredTeamId);
}

bool AKCFPlayerController::ServerRequestRespawn_Validate()
{
	return true;
}

void AKCFPlayerController::ServerRequestRespawn_Implementation()
{
	if (!CanRequestRespawn())
	{
		LogRejectedRequest(TEXT("Respawn"), TEXT("not eligible to respawn yet"));
		return;
	}

	UWorld* World = GetWorld();
	if (AKCFGameMode* GameMode = World ? World->GetAuthGameMode<AKCFGameMode>() : nullptr)
	{
		GameMode->RequestImmediateRespawn(this);
	}
}

bool AKCFPlayerController::ServerRequestVehicleSelection_Validate(FName VehicleId)
{
	// Reject obviously malformed names outright; existence is checked in the implementation.
	return !VehicleId.IsNone();
}

void AKCFPlayerController::ServerRequestVehicleSelection_Implementation(FName VehicleId)
{
	UWorld* World = GetWorld();
	AKCFGameMode* GameMode = World ? World->GetAuthGameMode<AKCFGameMode>() : nullptr;
	if (!GameMode)
	{
		LogRejectedRequest(TEXT("VehicleSelection"), TEXT("no authoritative game mode"));
		return;
	}

	// The id is a request, not an instruction: the game mode checks it against the catalogue before
	// storing it, so a modified client can only ever pick a vehicle that actually exists and is
	// legal for this match.
	if (!GameMode->IsVehicleSelectable(VehicleId))
	{
		LogRejectedRequest(TEXT("VehicleSelection"), TEXT("vehicle id is not in the selectable catalogue"));
		return;
	}

	if (AKCFPlayerState* KCFPlayerState = GetKCFPlayerState())
	{
		KCFPlayerState->SetSelectedVehicleId(VehicleId);
	}
}

bool AKCFPlayerController::ServerRequestKartReset_Validate()
{
	return true;
}

void AKCFPlayerController::ServerRequestKartReset_Implementation()
{
	AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Kart->CanAct())
	{
		LogRejectedRequest(TEXT("KartReset"), TEXT("no controllable kart"));
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Now - LastKartResetRequestTime < KartResetCooldownSeconds)
	{
		LogRejectedRequest(TEXT("KartReset"), TEXT("rate limited"));
		return;
	}
	LastKartResetRequestTime = Now;

	Kart->ResetKartInPlace();
}

// -------------------------------------------------------------------------------------------------
// Client notifications
// -------------------------------------------------------------------------------------------------

void AKCFPlayerController::ClientNotifyKill_Implementation(AKCFPlayerState* Victim, FName WeaponId)
{
	OnKillConfirmed.Broadcast(Victim, WeaponId);
}

void AKCFPlayerController::ClientNotifyHit_Implementation(float DamageDealt, bool bWasLethal)
{
	OnHitConfirmed.Broadcast(DamageDealt, bWasLethal);
}

void AKCFPlayerController::ClientNotifyRespawnAvailable_Implementation(float SecondsUntilRespawn)
{
	OnRespawnAvailable.Broadcast(SecondsUntilRespawn);
}

// -------------------------------------------------------------------------------------------------
// Internals
// -------------------------------------------------------------------------------------------------

void AKCFPlayerController::SetRespawnAvailableTime(float ServerTime)
{
	if (!HasAuthority())
	{
		return;
	}

	RespawnAvailableServerTime = ServerTime;
}

void AKCFPlayerController::LogRejectedRequest(const TCHAR* RequestName, const TCHAR* Reason) const
{
	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	if (Settings && Settings->bLogRejectedRPCs)
	{
		UE_LOG(LogKCFNet, Verbose, TEXT("Rejected '%s' from %s: %s."),
			RequestName,
			*GetNameSafe(GetPlayerState<APlayerState>()),
			Reason);
	}
}
