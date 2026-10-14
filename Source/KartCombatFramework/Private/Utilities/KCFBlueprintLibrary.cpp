// Original work, MIT licensed.

#include "Utilities/KCFBlueprintLibrary.h"

#include "Components/KCFBoostComponent.h"
#include "Components/KCFHealthComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Match/KCFGameState.h"
#include "Player/KCFPlayerState.h"
#include "Subsystems/KCFContentRegistrySubsystem.h"
#include "Vehicles/KCFKartPawn.h"

namespace KCFConsole
{
	static TAutoConsoleVariable<int32> CVarForceTouchControls(
		TEXT("KCF.ForceTouchControls"),
		0,
		TEXT("When non-zero, UKCFBlueprintLibrary::IsMobilePlatform reports true on desktop so the touch HUD can be tested in PIE."),
		ECVF_Default);
}

namespace KCFLibrary
{
	static UWorld* ResolveWorld(const UObject* WorldContextObject)
	{
		return GEngine && WorldContextObject
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}
}

// --- Context lookups --------------------------------------------------------------------------------

AKCFGameState* UKCFBlueprintLibrary::GetKCFGameState(const UObject* WorldContextObject)
{
	const UWorld* World = KCFLibrary::ResolveWorld(WorldContextObject);
	return World ? World->GetGameState<AKCFGameState>() : nullptr;
}

AKCFKartPawn* UKCFBlueprintLibrary::GetLocalKartPawn(const UObject* WorldContextObject)
{
	const UWorld* World = KCFLibrary::ResolveWorld(WorldContextObject);
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AKCFKartPawn>(PC->GetPawn()) : nullptr;
}

AKCFPlayerState* UKCFBlueprintLibrary::GetLocalKCFPlayerState(const UObject* WorldContextObject)
{
	const UWorld* World = KCFLibrary::ResolveWorld(WorldContextObject);
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPlayerState<AKCFPlayerState>() : nullptr;
}

float UKCFBlueprintLibrary::GetServerTimeSeconds(const UObject* WorldContextObject)
{
	const UWorld* World = KCFLibrary::ResolveWorld(WorldContextObject);
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
}

// --- Content lookups ---------------------------------------------------------------------------------

UKCFWeaponDataAsset* UKCFBlueprintLibrary::FindWeaponById(const UObject* WorldContextObject, FName WeaponId)
{
	UKCFContentRegistrySubsystem* Registry = UKCFContentRegistrySubsystem::Get(WorldContextObject);
	return Registry ? Registry->FindWeapon(WeaponId) : nullptr;
}

UKCFPowerupDataAsset* UKCFBlueprintLibrary::FindPowerupById(const UObject* WorldContextObject, FName PowerupId)
{
	UKCFContentRegistrySubsystem* Registry = UKCFContentRegistrySubsystem::Get(WorldContextObject);
	return Registry ? Registry->FindPowerup(PowerupId) : nullptr;
}

UKCFVehicleDataAsset* UKCFBlueprintLibrary::FindVehicleById(const UObject* WorldContextObject, FName VehicleId)
{
	UKCFContentRegistrySubsystem* Registry = UKCFContentRegistrySubsystem::Get(WorldContextObject);
	return Registry ? Registry->FindVehicle(VehicleId) : nullptr;
}

// --- Match queries ------------------------------------------------------------------------------------

EKCFMatchState UKCFBlueprintLibrary::GetMatchState(const UObject* WorldContextObject)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	return GameState ? GameState->GetMatchState() : EKCFMatchState::WaitingForPlayers;
}

bool UKCFBlueprintLibrary::IsMatchActive(const UObject* WorldContextObject)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	return GameState && GameState->IsMatchActive();
}

float UKCFBlueprintLibrary::GetRemainingMatchTime(const UObject* WorldContextObject)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	return GameState ? GameState->GetRemainingMatchTime() : 0.f;
}

TArray<AKCFPlayerState*> UKCFBlueprintLibrary::GetScoreboard(const UObject* WorldContextObject)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	return GameState ? GameState->GetSortedPlayerStates() : TArray<AKCFPlayerState*>();
}

int32 UKCFBlueprintLibrary::GetPlayerPosition(const UObject* WorldContextObject, AKCFPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return 0;
	}

	const TArray<AKCFPlayerState*> Sorted = GetScoreboard(WorldContextObject);
	const int32 Index = Sorted.IndexOfByKey(PlayerState);
	return Index == INDEX_NONE ? 0 : Index + 1;
}

// --- Team helpers ----------------------------------------------------------------------------------------

EKCFTeamAttitude UKCFBlueprintLibrary::GetTeamAttitude(const AActor* A, const AActor* B)
{
	return IKCFTeamAgent::GetAttitude(A, B);
}

int32 UKCFBlueprintLibrary::GetTeamId(const AActor* Actor)
{
	return IKCFTeamAgent::ResolveTeamId(Actor);
}

FLinearColor UKCFBlueprintLibrary::GetTeamColor(const UObject* WorldContextObject, int32 TeamId)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;
	return Config ? Config->GetTeamDefinition(TeamId).TeamColor : FLinearColor::White;
}

FText UKCFBlueprintLibrary::GetTeamDisplayName(const UObject* WorldContextObject, int32 TeamId)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;
	return Config ? Config->GetTeamDefinition(TeamId).DisplayName : FText::GetEmpty();
}

// --- Formatting ---------------------------------------------------------------------------------------------

FText UKCFBlueprintLibrary::FormatTimeMinutesSeconds(float Seconds)
{
	const int32 Clamped = FMath::Max(0, FMath::FloorToInt(Seconds));
	const int32 Minutes = Clamped / 60;
	const int32 RemainingSeconds = Clamped % 60;

	FNumberFormattingOptions TwoDigits;
	TwoDigits.MinimumIntegralDigits = 2;

	return FText::Format(
		NSLOCTEXT("KCF", "TimeFormat", "{0}:{1}"),
		FText::AsNumber(Minutes),
		FText::AsNumber(RemainingSeconds, &TwoDigits));
}

int32 UKCFBlueprintLibrary::SpeedToKilometresPerHour(float SpeedCentimetresPerSecond)
{
	// cm/s -> km/h is a factor of 0.036. Rounded rather than truncated so a speedometer reads 60
	// rather than 59 at exactly 60 km/h.
	return FMath::RoundToInt(FMath::Abs(SpeedCentimetresPerSecond) * 0.036f);
}

int32 UKCFBlueprintLibrary::SpeedToMilesPerHour(float SpeedCentimetresPerSecond)
{
	return FMath::RoundToInt(FMath::Abs(SpeedCentimetresPerSecond) * 0.0223694f);
}

// --- Platform -------------------------------------------------------------------------------------------------

bool UKCFBlueprintLibrary::IsMobilePlatform()
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	return true;
#else
	// Desktop builds are not mobile, but the touch layout still has to be testable in the editor
	// without deploying to a device, so the console variable forces it on.
	return KCFConsole::CVarForceTouchControls.GetValueOnAnyThread() != 0;
#endif
}

bool UKCFBlueprintLibrary::IsDedicatedServer(const UObject* WorldContextObject)
{
	const UWorld* World = KCFLibrary::ResolveWorld(WorldContextObject);
	return World && World->GetNetMode() == NM_DedicatedServer;
}

// --- Debug -----------------------------------------------------------------------------------------------------

void UKCFBlueprintLibrary::DebugLogKartState(AKCFKartPawn* Kart)
{
	if (!Kart)
	{
		return;
	}

	const UKCFHealthComponent* Health = Kart->GetHealthComponent();
	const UKCFBoostComponent* Boost = Kart->GetBoostComponent();

	UE_LOG(LogKartCombatFramework, Log,
		TEXT("[%s] %s: speed=%.0f health=%.0f/%.0f shield=%.0f drifting=%d boosting=%d charges=%d protected=%d"),
		*KCF_NetContext(Kart),
		*GetNameSafe(Kart),
		Kart->GetCurrentSpeed(),
		Health ? Health->GetHealth() : 0.f,
		Health ? Health->GetMaxHealth() : 0.f,
		Health ? Health->GetShield() : 0.f,
		Kart->IsDrifting() ? 1 : 0,
		Kart->IsBoosting() ? 1 : 0,
		Boost ? Boost->GetCharges() : 0,
		Kart->IsSpawnProtected() ? 1 : 0);
}

void UKCFBlueprintLibrary::DebugLogMatchState(const UObject* WorldContextObject)
{
	const AKCFGameState* GameState = GetKCFGameState(WorldContextObject);
	if (!GameState)
	{
		UE_LOG(LogKCFMatch, Warning, TEXT("DebugLogMatchState: no KCF game state in this world."));
		return;
	}

	UE_LOG(LogKCFMatch, Log, TEXT("Match state=%s remaining=%.1fs participants=%d alive=%d"),
		*UEnum::GetValueAsString(GameState->GetMatchState()),
		GameState->GetRemainingMatchTime(),
		GameState->GetParticipantCount(),
		GameState->GetAlivePlayerCount());

	for (const FKCFTeamScore& Team : GameState->GetTeamScores())
	{
		UE_LOG(LogKCFMatch, Log, TEXT("  Team %d: score=%d players=%d alive=%d"),
			Team.TeamId, Team.Score, Team.PlayerCount, Team.AlivePlayerCount);
	}

	for (const AKCFPlayerState* PlayerState : GameState->GetSortedPlayerStates())
	{
		if (!PlayerState)
		{
			continue;
		}

		UE_LOG(LogKCFMatch, Log, TEXT("  %s: score=%d K/D/A=%d/%d/%d team=%d eliminated=%d"),
			*PlayerState->GetPlayerName(),
			PlayerState->GetKCFScore(),
			PlayerState->GetKills(),
			PlayerState->GetDeaths(),
			PlayerState->GetAssists(),
			PlayerState->GetTeamId(),
			PlayerState->IsEliminated() ? 1 : 0);
	}
}
