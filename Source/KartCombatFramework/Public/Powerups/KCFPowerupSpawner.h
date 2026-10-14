// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/Actor.h"
#include "KCFPowerupSpawner.generated.h"

class AKCFPowerupPickup;
class UBillboardComponent;
class UDataTable;
class UKCFPowerupDataAsset;
class USceneComponent;

/**
 * Places powerup crates in the arena on a timer.
 *
 * Entirely server-driven: the spawner itself does not replicate, because nothing about it is
 * observable. What clients see is the AKCFPowerupPickup actors it creates, which replicate normally.
 * Keeping the spawner invisible to the network means a map with forty spawn points costs forty
 * server-side timers and zero bandwidth.
 *
 * Selection is weighted. Weights can come from an inline list or from a powerup catalogue table, so
 * a designer can rebalance drop rates in a spreadsheet without touching a level.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPowerupSpawner : public AActor
{
	GENERATED_BODY()

public:
	AKCFPowerupSpawner();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	/** Starts spawning. Called by the game mode when the match goes live. Server only. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Powerup")
	void StartSpawning();

	/** Stops spawning and optionally removes crates that are already out. Server only. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Powerup")
	void StopSpawning(bool bRemoveExisting = false);

	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	bool IsSpawningEnabled() const { return bSpawningEnabled; }

	/** True when AKCFGameMode should start and stop this spawner along with the match. */
	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	bool ShouldAutoStart() const { return bAutoStart; }

	/** Called by a pickup when it is taken, so the respawn timer can start. */
	void NotifyPickupCollected(AKCFPowerupPickup* Pickup);

	/** Spawns one crate immediately if there is room. Server only. Returns the crate or null. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Powerup")
	AKCFPowerupPickup* SpawnPickupNow();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USceneComponent> SpawnRoot;

	// --- Configuration -------------------------------------------------------------------------

	/** Pickup actor class placed in the world. A project subclasses this for its own crate visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	TSubclassOf<AKCFPowerupPickup> PickupClass;

	/**
	 * Inline weighted table. Takes priority over PowerupCatalogue when it is non-empty, which lets
	 * one spawner in a level deliberately drop only a specific subset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	TArray<FKCFWeightedPowerupEntry> PowerupPool;

	/**
	 * FKCFPowerupRow table used when PowerupPool is empty.
	 * Soft so that a level full of spawners does not load the whole catalogue at level load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	TSoftObjectPtr<UDataTable> PowerupCatalogue;

	/** Only rows carrying every one of these tags are eligible. Empty means no filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	FGameplayTagContainer RequiredPowerupTags;

	/** Seconds between spawns while below MaxActivePickups. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner", meta = (ClampMin = "0.1", Units = "s"))
	float SpawnInterval = 12.f;

	/** Seconds after a crate is taken before this spawner tries again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 8.f;

	/** Seconds before the first spawn once the match goes live. Staggers a map full of spawners. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner", meta = (ClampMin = "0.0", Units = "s"))
	float InitialSpawnDelay = 2.f;

	/** Crates this spawner may have out simultaneously. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxActivePickups = 1;

	/**
	 * Extra spawn offsets relative to the spawner, used when MaxActivePickups is above one.
	 * Empty means every crate spawns at the spawner's own transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	TArray<FVector> AdditionalSpawnOffsets;

	/**
	 * Spawning starts automatically when the match goes live, and stops when it finishes.
	 * Clear it for spawners driven by level script or a Blueprint; the match flow then leaves this
	 * spawner alone entirely.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Spawner")
	bool bAutoStart = true;

	// --- Runtime state ----------------------------------------------------------------------------

	UPROPERTY(Transient)
	TArray<TObjectPtr<AKCFPowerupPickup>> ActivePickups;

	bool bSpawningEnabled = false;

	FTimerHandle SpawnTimerHandle;

	/** Resolved pool, built once on first use from either the inline list or the catalogue. */
	TArray<FKCFWeightedPowerupEntry> ResolvedPool;
	bool bPoolResolved = false;

	/** Builds ResolvedPool. Loads the catalogue table but not the powerup assets themselves. */
	void ResolvePool();

	/** Weighted random choice from ResolvedPool. Loads only the chosen definition. */
	UKCFPowerupDataAsset* SelectPowerup();

	/** Timer callback: spawns a crate if there is room, then reschedules. */
	void SpawnTick();

	/** Drops destroyed or collected entries from ActivePickups. */
	void PruneActivePickups();

	/** Transform for the Nth simultaneous crate. */
	FTransform GetSpawnTransformForIndex(int32 Index) const;
};
