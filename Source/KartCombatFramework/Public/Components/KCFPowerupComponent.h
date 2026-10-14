// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "GameplayTagContainer.h"
#include "KCFPowerupComponent.generated.h"

class AKCFKartPawn;
class UKCFPowerupDataAsset;
class UKCFPowerupEffect;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPowerupCollectedSignature, UKCFPowerupDataAsset*, Powerup);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPowerupUsedSignature, UKCFPowerupDataAsset*, Powerup);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPowerupExpiredSignature, FName, PowerupId);

/**
 * A replicated summary of one active powerup, for UI.
 *
 * Carries the id and the end time rather than the effect object, because the effect is a
 * server-side UObject that has no business crossing the network. A client reconstructs "2.4 seconds
 * of shield left" from the end time and its own synchronised server clock.
 */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFActivePowerupInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Powerup")
	FName PowerupId;

	/** Server time at which the effect expires. 0 for a passive effect with no end. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Powerup")
	float EndServerTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Powerup")
	int32 Stacks = 1;

	bool operator==(const FKCFActivePowerupInfo& Other) const { return PowerupId == Other.PowerupId; }
};

/**
 * Collects powerups and owns the effects they produce.
 *
 * Server-authoritative end to end: pickups call Collect on the server, effects are instantiated and
 * reverted on the server, and clients receive only a small replicated summary good enough to draw a
 * HUD icon and a countdown.
 *
 * The component does not tick. Expiry is a one-shot timer per active effect, and an effect that
 * wants periodic work gets a repeating timer at its own declared period. An eight-player match with
 * two active powerups each therefore costs sixteen timers rather than sixteen tick functions.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFPowerupComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFPowerupComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	UPROPERTY(BlueprintAssignable, Category = "KCF|Powerup") FKCFPowerupCollectedSignature OnPowerupCollected;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Powerup") FKCFPowerupUsedSignature OnPowerupUsed;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Powerup") FKCFPowerupExpiredSignature OnPowerupExpired;

	// --- Collection ----------------------------------------------------------------------------

	/**
	 * Handles a pickup touching this kart. Server only.
	 *
	 * Depending on the definition this either grants a weapon to the inventory, activates an effect
	 * immediately, or stores the powerup in an inventory slot for later use.
	 *
	 * @return true when the powerup was actually taken, which is what tells the pickup to despawn.
	 */
	bool CollectPowerup(UKCFPowerupDataAsset* Powerup);

	/**
	 * Activates a powerup the player is carrying. Server only.
	 * Called by the combat component when the player presses the use-powerup button.
	 */
	bool ActivatePowerup(UKCFPowerupDataAsset* Powerup);

	// --- Queries -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	const TArray<FKCFActivePowerupInfo>& GetActivePowerups() const { return ActivePowerups; }

	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	bool HasActivePowerup(FName PowerupId) const;

	/** Seconds left on an active powerup, or 0 when it is not active. */
	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	float GetRemainingTime(FName PowerupId) const;

	/** True while the player is still inside this powerup's per-player cooldown. */
	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	bool IsOnCooldown(FName PowerupId) const;

	/** Total powerups collected this life, surfaced for statistics. */
	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	int32 GetCollectedCount() const { return CollectedCount; }

	// --- Lifecycle -----------------------------------------------------------------------------

	/** Reverts and drops every active effect. Called on death and at match end. Server only. */
	void ClearAllEffects();

	/** Clears effects and per-player cooldowns. Called on respawn. Server only. */
	void ResetForRespawn();

	// --- Cosmetic hooks --------------------------------------------------------------------------

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Powerup Activated"))
	void BP_OnPowerupActivated(UKCFPowerupDataAsset* Powerup);

protected:
	/**
	 * Replicated to everyone, not just the owner.
	 *
	 * Unlike inventory contents, an active powerup is visible on the kart anyway - a shield bubble,
	 * a speed trail - so hiding it from other clients would buy nothing and would stop them from
	 * rendering the effect at all. The payload is an id and a float, not the effect object.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ActivePowerups, BlueprintReadOnly, Category = "KCF|Powerup")
	TArray<FKCFActivePowerupInfo> ActivePowerups;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Powerup")
	int32 CollectedCount = 0;

	UFUNCTION() void OnRep_ActivePowerups();

	/** One live effect. Server-only state; never replicated. */
	struct FKCFActiveEffect
	{
		TObjectPtr<UKCFPowerupEffect> Effect;
		TObjectPtr<UKCFPowerupDataAsset> Definition;
		FTimerHandle ExpiryTimer;
		FTimerHandle PeriodicTimer;
		int32 Stacks = 1;
	};

	/**
	 * Active effects keyed by powerup id. Held in a UPROPERTY-visible array via EffectObjects so
	 * the GC does not collect an effect that only this map references.
	 */
	TMap<FName, FKCFActiveEffect> ActiveEffects;

	/** Keeps effect objects referenced for the GC. Mirrors ActiveEffects. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UKCFPowerupEffect>> EffectObjects;

	/** Powerup id -> server time at which the per-player cooldown ends. */
	TMap<FName, float> CooldownEndTimes;

	/** Instantiates and applies an effect. Server only. */
	void StartEffect(UKCFPowerupDataAsset* Powerup);

	/** Reverts and removes an effect by id. Server only. */
	void EndEffect(FName PowerupId);

	/** Timer callback for a periodic effect. */
	void TickEffect(FName PowerupId);

	/** Rebuilds the replicated summary from ActiveEffects. */
	void RefreshReplicatedSummary();

	AKCFKartPawn* GetKartPawn() const;
	float GetServerTime() const;
	bool HasPowerupAuthority() const;
};
