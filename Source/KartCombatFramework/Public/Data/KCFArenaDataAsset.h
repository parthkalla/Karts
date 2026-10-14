// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "Engine/World.h"
#include "KCFArenaDataAsset.generated.h"

class UKCFGameModeDataAsset;
class UTexture2D;

/**
 * Describes one playable arena: which map it is, which modes it supports, and the playspace bounds.
 *
 * The bounds exist so that a mode can kill or teleport a kart that leaves the arena without every
 * map needing a hand-placed kill volume, and so that a future battle-royale shrinking play area has
 * somewhere authored to shrink towards.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFArenaDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	/** The level to travel to. Soft reference: a map list must not load any maps. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> Map;

	/** Loading-screen / map-select art. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	TSoftObjectPtr<UTexture2D> PreviewImage;

	/** Modes this arena is built for. Empty means the arena accepts any mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena")
	TArray<TSoftObjectPtr<UKCFGameModeDataAsset>> SupportedGameModes;

	/** Player count the arena is designed around, used to filter a map vote. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "1", ClampMax = "64"))
	int32 RecommendedMinPlayers = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arena", meta = (ClampMin = "1", ClampMax = "64"))
	int32 RecommendedMaxPlayers = 8;

	// --- Playspace -------------------------------------------------------------------------------

	/** World-space centre of the playable volume. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playspace")
	FVector PlayspaceOrigin = FVector::ZeroVector;

	/** Half-extents of the playable volume, cm. Zero on an axis disables bounds checking there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playspace", meta = (Units = "cm"))
	FVector PlayspaceExtent = FVector(25000.f, 25000.f, 10000.f);

	/** Karts below this Z are destroyed, catching falls off the arena. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playspace", meta = (Units = "cm"))
	float KillZ = -5000.f;

	/** Damage per second applied while a kart is outside the playspace. 0 means no penalty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playspace", meta = (ClampMin = "0.0"))
	float OutOfBoundsDamagePerSecond = 25.f;

	/** Seconds outside the playspace before damage starts, allowing a brief jump over the edge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playspace", meta = (ClampMin = "0.0", Units = "s"))
	float OutOfBoundsGraceSeconds = 2.f;

	/** True when the given world location is inside the authored playspace. */
	UFUNCTION(BlueprintPure, Category = "KCF|Arena")
	bool IsInsidePlayspace(const FVector& WorldLocation) const;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFArena"); }
	//~ End UKCFDataAssetBase
};
