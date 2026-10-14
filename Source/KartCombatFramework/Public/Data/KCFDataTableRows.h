// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Engine/DataTable.h"
#include "KCFDataTableRows.generated.h"

class UKCFArenaDataAsset;
class UKCFGameModeDataAsset;
class UKCFPowerupDataAsset;
class UKCFVehicleDataAsset;
class UKCFWeaponDataAsset;

/**
 * -------------------------------------------------------------------------------------------
 * Data Table rows vs Data Assets
 * -------------------------------------------------------------------------------------------
 * These rows are deliberately *catalogue* entries, not duplicate definitions. Each row carries a
 * soft pointer to the Data Asset that actually defines the thing, plus the lightweight tabular
 * fields a designer wants to see side by side when balancing: costs, unlock rules, playlist flags,
 * and a small number of headline stats.
 *
 * The split exists because the two formats are good at different things. A Data Table is a single
 * asset that diffs cleanly, imports from CSV, and shows two hundred rows in one grid - ideal for a
 * balance pass or a storefront. A Data Asset holds class references, nested structs and asset
 * pointers that a CSV cannot express.
 *
 * The one rule that keeps this from becoming two sources of truth: a row never restates a number
 * that the Data Asset owns. Where a headline stat appears in a row (for a comparison bar in a
 * vehicle-select screen) it is a normalised 0..1 display value, not the simulation value.
 * See Documentation/DataTables.md.
 */

/** Shared columns for every catalogue row. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFCatalogueRowBase : public FTableRowBase
{
	GENERATED_BODY()

	/** Must match the Id on the referenced Data Asset. The editor validator enforces this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue", meta = (MultiLine = "true"))
	FText Description;

	/** Entry is selectable in the front end. Lets content ship disabled ahead of a release. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue")
	bool bEnabled = true;

	/** Sort order in selection UI. Lower appears first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue")
	int32 SortOrder = 0;

	/** Soft-currency cost to unlock. 0 means unlocked from the start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue", meta = (ClampMin = "0"))
	int32 UnlockCost = 0;

	/** Player level required. 0 means no requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue", meta = (ClampMin = "0"))
	int32 UnlockLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalogue")
	FGameplayTagContainer Tags;
};

/** One kart in the vehicle-select catalogue. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFVehicleRow : public FKCFCatalogueRowBase
{
	GENERATED_BODY()

	/** The definition this row describes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TSoftObjectPtr<UKCFVehicleDataAsset> Vehicle;

	/** Normalised 0..1 display bars. These drive UI only; the simulation reads the Data Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpeedRating = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AccelerationRating = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandlingRating = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DurabilityRating = 0.5f;
};

/** One weapon in the balance table. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFWeaponRow : public FKCFCatalogueRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSoftObjectPtr<UKCFWeaponDataAsset> Weapon;

	/** Relative drop weight used by powerup spawners that source their table from this catalogue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0"))
	float DropWeight = 1.f;

	/**
	 * Drop weight multiplier applied to players near the front of the pack. Values below 1 make
	 * strong weapons rarer for the leader, which is the usual catch-up lever in an arcade racer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LeaderDropWeightScale = 1.f;

	/** Drop weight multiplier applied to players near the back of the pack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float TrailerDropWeightScale = 1.f;
};

/** One powerup in the balance table. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFPowerupRow : public FKCFCatalogueRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powerup")
	TSoftObjectPtr<UKCFPowerupDataAsset> Powerup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powerup", meta = (ClampMin = "0.0"))
	float DropWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powerup", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LeaderDropWeightScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Powerup", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float TrailerDropWeightScale = 1.f;
};

/** One playlist entry: a game mode configuration offered in the front end. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFGameModeRow : public FKCFCatalogueRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Mode")
	TSoftObjectPtr<UKCFGameModeDataAsset> GameModeConfig;

	/**
	 * The AKCFGameMode subclass that implements these rules.
	 * Stored as a path string rather than a TSoftClassPtr so the row remains importable from CSV.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Mode", meta = (MetaClass = "/Script/KartCombatFramework.KCFGameMode"))
	FSoftClassPath GameModeClass;

	/** Arenas this playlist entry can roll. Empty means any arena that supports the mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Mode")
	TArray<FName> ArenaIds;

	/** Relative weight when a playlist picks a mode at random. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Mode", meta = (ClampMin = "0.0"))
	float PlaylistWeight = 1.f;

	/** Shown as the headline mode in the front end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Mode")
	bool bFeatured = false;
};

/** One arena in the map catalogue. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFArenaRow : public FKCFCatalogueRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	TSoftObjectPtr<UKCFArenaDataAsset> Arena;

	/** Relative weight when a playlist picks a map at random. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "0.0"))
	float PlaylistWeight = 1.f;

	/** Excluded from random selection but still reachable by direct choice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	bool bExcludeFromRandomRotation = false;
};
