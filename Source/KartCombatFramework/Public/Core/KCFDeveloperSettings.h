// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "KCFDeveloperSettings.generated.h"

class UDataTable;
class UKCFGameModeDataAsset;
class UKCFMobileSettingsDataAsset;
class UKCFVehicleDataAsset;

/**
 * Project-wide defaults for the framework, edited under Project Settings > Game > Kart Combat Framework
 * and stored in DefaultGame.ini.
 *
 * Everything here is a fallback, never a hard requirement: each value has a sensible default and a
 * runtime consumer that degrades gracefully when the referenced asset is unset. All asset references
 * are soft so that opening Project Settings does not pull content into memory.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Kart Combat Framework"))
class KARTCOMBATFRAMEWORK_API UKCFDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UKCFDeveloperSettings();

	/** Blueprint-friendly accessor. Never returns null. */
	UFUNCTION(BlueprintPure, Category = "KCF|Settings", meta = (DisplayName = "Get Kart Combat Framework Settings"))
	static const UKCFDeveloperSettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings

	// --- Content defaults ---------------------------------------------------------------------

	/** Used when a kart pawn is spawned without an explicit vehicle definition. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UKCFVehicleDataAsset> DefaultVehicle;

	/** Used when a game mode is started without an explicit configuration asset. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UKCFGameModeDataAsset> DefaultGameModeConfig;

	/** Used when a player has no saved mobile control preferences yet. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UKCFMobileSettingsDataAsset> DefaultMobileSettings;

	// --- Catalogues ---------------------------------------------------------------------------
	//
	// These tables are what UKCFContentRegistrySubsystem loads at game-instance start so that an
	// FName carried in an inventory slot or an RPC can be turned back into a definition asset. A
	// project that never uses ids can leave them unset; the registry simply resolves nothing.

	/** FKCFVehicleRow table. Also the default selectable-kart list for AKCFGameMode. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalogues")
	TSoftObjectPtr<UDataTable> VehicleCatalogue;

	/** FKCFWeaponRow table. Required for weapon ids in inventories to resolve. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalogues")
	TSoftObjectPtr<UDataTable> WeaponCatalogue;

	/** FKCFPowerupRow table. Required for powerup ids in inventories to resolve. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalogues")
	TSoftObjectPtr<UDataTable> PowerupCatalogue;

	/** FKCFGameModeRow table, used by front-end playlists. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalogues")
	TSoftObjectPtr<UDataTable> GameModeCatalogue;

	/** FKCFArenaRow table, used by front-end map selection. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalogues")
	TSoftObjectPtr<UDataTable> ArenaCatalogue;

	// --- Networking ---------------------------------------------------------------------------

	/** NetUpdateFrequency for kart pawns. Lower this first when optimising bandwidth for mobile. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Networking", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float KartNetUpdateFrequency = 60.f;

	/** MinNetUpdateFrequency for kart pawns. Used by adaptive net update throttling. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Networking", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float KartMinNetUpdateFrequency = 15.f;

	/** NetUpdateFrequency for the game state. Match timers do not need to be fast. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Networking", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float GameStateNetUpdateFrequency = 10.f;

	/** Beyond this distance (cm) a kart stops being replicated to a client. 0 disables culling. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Networking", meta = (ClampMin = "0.0"))
	float KartNetCullDistance = 60000.f;

	// --- Gameplay defaults ---------------------------------------------------------------------

	/** Seconds after damaging a victim during which the damager still earns an assist on their death. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float AssistWindowSeconds = 10.f;

	/** Minimum fraction of max health a player must deal in the window to earn an assist. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AssistDamageFraction = 0.2f;

	/** Impact speed (cm/s) below which kart-to-kart collisions deal no damage. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0"))
	float CollisionDamageSpeedThreshold = 900.f;

	/** Damage per 1000 cm/s of impact speed above the threshold. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Gameplay", meta = (ClampMin = "0.0"))
	float CollisionDamagePerThousandSpeed = 6.f;

	// --- Performance ---------------------------------------------------------------------------

	/** Projectiles and pickups are recycled through UKCFActorPoolSubsystem when enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableActorPooling = true;

	/** Per-class pool ceiling. Requests beyond this spawn normally and are destroyed normally. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxPooledActorsPerClass = 32;

	/**
	 * Hard cap on simultaneously live projectiles owned by one player. Protects mobile clients from
	 * a pathological fire rate and bounds replication cost.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxLiveProjectilesPerPlayer = 8;

	// --- Debug ----------------------------------------------------------------------------------

	/** Master switch for framework debug drawing. Has no effect in Shipping builds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bEnableDebugDrawing = false;

	/** Logs every rejected server RPC. Useful when diagnosing a desync or a misbehaving client. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bLogRejectedRPCs = true;
};
