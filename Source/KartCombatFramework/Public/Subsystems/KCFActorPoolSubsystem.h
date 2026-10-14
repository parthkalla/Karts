// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KCFActorPoolSubsystem.generated.h"

/**
 * Implemented by actors that want to be recycled rather than destroyed.
 *
 * An actor that does not implement this can still be pooled; it simply gets no chance to reset its
 * own state, which is almost never what you want for anything with a timer or a velocity.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UKCFPoolableActor : public UInterface
{
	GENERATED_BODY()
};

class KARTCOMBATFRAMEWORK_API IKCFPoolableActor
{
	GENERATED_BODY()

public:
	/** Called when the actor is taken out of the pool, before it is made visible and collidable. */
	UFUNCTION(BlueprintNativeEvent, Category = "KCF|Pool")
	void OnAcquiredFromPool();

	/** Called when the actor is returned, after it is hidden and its collision is disabled. */
	UFUNCTION(BlueprintNativeEvent, Category = "KCF|Pool")
	void OnReturnedToPool();
};

/**
 * Parked instances for one actor class.
 *
 * A struct rather than a bare TArray because a TMap value in a UPROPERTY must be a reflected type,
 * and the instances have to be reflected or the garbage collector will reclaim the actors the pool
 * is holding.
 */
USTRUCT()
struct KARTCOMBATFRAMEWORK_API FKCFActorPoolEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Instances;
};

/**
 * Recycles short-lived actors instead of spawning and destroying them.
 *
 * Projectiles are the motivating case. A busy eight-player match can spawn several per second, and
 * on mobile each spawn means an allocation, component registration, and eventually garbage
 * collection work that shows up as a frame spike rather than as a steady cost.
 *
 * Pooling is deliberately opt-in per call rather than automatic. A pooled actor is reused, which
 * means any state it does not reset leaks into its next life; that is a real correctness hazard and
 * it should be a decision at the call site, not a silent global behaviour. Gameplay correctness
 * never depends on pooling being on: Acquire falls back to a normal spawn and Release falls back to
 * Destroy, so turning the whole system off in Project Settings changes performance and nothing else.
 *
 * NOT USED BY THE FRAMEWORK'S OWN PROJECTILES AND PICKUPS, on purpose. AKCFProjectile and
 * AKCFPowerupPickup replicate their defining data with COND_InitialOnly, which the engine sends once
 * per actor channel. Recycling such an actor keeps its net identity, so a client that already holds
 * the channel would never receive the new weapon or powerup and would render the previous one. The
 * fix is not a smarter pool, it is giving up COND_InitialOnly - and paying that replication cost on
 * every projectile to save a spawn is the wrong trade.
 *
 * So this is the right tool for actors you spawn yourself that are either not replicated at all
 * (cosmetic debris, decals, local audio emitters) or that carry no initial-only state. If you do
 * want pooled projectiles, subclass AKCFProjectile, replicate its configuration unconditionally with
 * a RepNotify that re-applies visuals, and route spawning through AcquireActor/ReleaseActor.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFActorPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	UFUNCTION(BlueprintPure, Category = "KCF|Pool", meta = (WorldContext = "WorldContextObject"))
	static UKCFActorPoolSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Returns a recycled actor of this class, or spawns one when the pool is empty.
	 *
	 * The returned actor is already placed at SpawnTransform, visible and collidable, with
	 * OnAcquiredFromPool already called. Caller still owns setting up gameplay state.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Pool", meta = (DeterminesOutputType = "ActorClass"))
	AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform, AActor* Owner, APawn* InstigatorPawn);

	/**
	 * Returns an actor to the pool, or destroys it when the pool for its class is full.
	 * Safe to call with an actor that was never pooled.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Pool")
	void ReleaseActor(AActor* Actor);

	/** Pre-creates instances so the first shot of a match does not pay the spawn cost. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Pool")
	void PrewarmPool(TSubclassOf<AActor> ActorClass, int32 Count);

	/** Destroys every pooled instance. Called at match end and on world teardown. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Pool")
	void DrainPools();

	/** Number of instances currently parked in the pool for a class. */
	UFUNCTION(BlueprintPure, Category = "KCF|Pool")
	int32 GetPooledCount(TSubclassOf<AActor> ActorClass) const;

protected:
	/** Parked instances per class. */
	UPROPERTY(Transient)
	TMap<TSubclassOf<AActor>, FKCFActorPoolEntry> Pools;

	/** Takes an actor out of circulation without destroying it. */
	void DeactivateActor(AActor* Actor) const;

	/** Puts a parked actor back into circulation at a transform. */
	void ActivateActor(AActor* Actor, const FTransform& SpawnTransform, AActor* Owner, APawn* InstigatorPawn) const;

	/** Whether pooling is enabled at all, from Project Settings. */
	bool IsPoolingEnabled() const;
};
