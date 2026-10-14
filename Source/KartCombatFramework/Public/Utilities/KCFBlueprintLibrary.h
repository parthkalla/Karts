// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KCFBlueprintLibrary.generated.h"

class AKCFGameState;
class AKCFKartPawn;
class AKCFPlayerState;
class UKCFPowerupDataAsset;
class UKCFVehicleDataAsset;
class UKCFWeaponDataAsset;

/**
 * Convenience accessors for Blueprint and UI.
 *
 * Everything here is a read, a formatting helper, or a lookup. There is deliberately no function in
 * this library that changes gameplay state: a static, world-context, Blueprint-callable entry point
 * that could set health or award score would be a way around every authority check in the framework.
 * Mutating operations live on the component that owns the state and validate there.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// --- Context lookups --------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static AKCFGameState* GetKCFGameState(const UObject* WorldContextObject);

	/** The local player's kart, or null when they are dead or spectating. */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static AKCFKartPawn* GetLocalKartPawn(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static AKCFPlayerState* GetLocalKCFPlayerState(const UObject* WorldContextObject);

	/**
	 * Server time as both machines agree on it.
	 * Use this, not Get Game Time In Seconds, whenever comparing against a replicated timestamp.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static float GetServerTimeSeconds(const UObject* WorldContextObject);

	// --- Content lookups ----------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static UKCFWeaponDataAsset* FindWeaponById(const UObject* WorldContextObject, FName WeaponId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static UKCFPowerupDataAsset* FindPowerupById(const UObject* WorldContextObject, FName PowerupId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static UKCFVehicleDataAsset* FindVehicleById(const UObject* WorldContextObject, FName VehicleId);

	// --- Match queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Match", meta = (WorldContext = "WorldContextObject"))
	static EKCFMatchState GetMatchState(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "KCF|Match", meta = (WorldContext = "WorldContextObject"))
	static bool IsMatchActive(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "KCF|Match", meta = (WorldContext = "WorldContextObject"))
	static float GetRemainingMatchTime(const UObject* WorldContextObject);

	/** Scoreboard-ordered player states. Allocates; call it when the scoreboard refreshes, not per frame. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match", meta = (WorldContext = "WorldContextObject"))
	static TArray<AKCFPlayerState*> GetScoreboard(const UObject* WorldContextObject);

	/** 1-based position of a player on the scoreboard. 0 when they are not in the match. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match", meta = (WorldContext = "WorldContextObject"))
	static int32 GetPlayerPosition(const UObject* WorldContextObject, AKCFPlayerState* PlayerState);

	// --- Team helpers -------------------------------------------------------------------------------------

	/** Relationship between two actors under the current rule set. */
	UFUNCTION(BlueprintPure, Category = "KCF|Team")
	static EKCFTeamAttitude GetTeamAttitude(const AActor* A, const AActor* B);

	UFUNCTION(BlueprintPure, Category = "KCF|Team")
	static int32 GetTeamId(const AActor* Actor);

	/** Authored colour for a team, or white when the match has no team definitions. */
	UFUNCTION(BlueprintPure, Category = "KCF|Team", meta = (WorldContext = "WorldContextObject"))
	static FLinearColor GetTeamColor(const UObject* WorldContextObject, int32 TeamId);

	UFUNCTION(BlueprintPure, Category = "KCF|Team", meta = (WorldContext = "WorldContextObject"))
	static FText GetTeamDisplayName(const UObject* WorldContextObject, int32 TeamId);

	// --- Formatting ------------------------------------------------------------------------------------------

	/** Seconds as M:SS, for match timers. */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities")
	static FText FormatTimeMinutesSeconds(float Seconds);

	/** Speed in cm/s rendered as a whole number of km/h, for a speedometer. */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities")
	static int32 SpeedToKilometresPerHour(float SpeedCentimetresPerSecond);

	/** Speed in cm/s rendered as a whole number of mph. */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities")
	static int32 SpeedToMilesPerHour(float SpeedCentimetresPerSecond);

	// --- Platform ---------------------------------------------------------------------------------------------

	/**
	 * True when the current platform is a touch-first device.
	 * Use it to choose which control layout to build, never to change gameplay rules: a match must
	 * play identically regardless of what the players are holding.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities")
	static bool IsMobilePlatform();

	/** True on a dedicated server build. Guard anything cosmetic with this. */
	UFUNCTION(BlueprintPure, Category = "KCF|Utilities", meta = (WorldContext = "WorldContextObject"))
	static bool IsDedicatedServer(const UObject* WorldContextObject);

	// --- Debug ------------------------------------------------------------------------------------------------

	/**
	 * Logs a one-line summary of a kart's authoritative state.
	 * Compiled to nothing in Shipping, so leaving a call in a Blueprint costs nothing in a release.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Debug", meta = (DevelopmentOnly))
	static void DebugLogKartState(AKCFKartPawn* Kart);

	/** Logs the full match state, scores and player list. Development builds only. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Debug", meta = (WorldContext = "WorldContextObject", DevelopmentOnly))
	static void DebugLogMatchState(const UObject* WorldContextObject);
};
