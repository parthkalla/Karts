// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "GameplayTagContainer.h"
#include "KCFHealthComponent.generated.h"

class UKCFVehicleDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FKCFHealthChangedSignature, float, NewHealth, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FKCFShieldChangedSignature, float, NewShield, float, MaxShield, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFDamageTakenSignature, const FKCFDamageInfo&, DamageInfo, const FKCFDamageResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FKCFDeathSignature, AActor*, Victim, AActor*, Killer, const FKCFDamageInfo&, FatalDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFInvulnerabilitySignature, bool, bIsInvulnerable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFReviveSignature);

/**
 * Health, shield, invulnerability and death for one actor.
 *
 * Every mutating path is server-only and every value is replicated. There is no client-callable way
 * to change health, because "set my health" is the single most attractive thing for a modified
 * client to call.
 *
 * Does not tick. Shield regeneration runs on a timer that only exists while the shield is actually
 * regenerating, and invulnerability expires on a one-shot timer.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFHealthComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// --- Delegates ------------------------------------------------------------------------------
	// All of these fire on both the server and the clients that receive the replicated change, so a
	// HUD bound to them behaves identically on a listen server and a remote client.

	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFHealthChangedSignature OnHealthChanged;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFShieldChangedSignature OnShieldChanged;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFDamageTakenSignature OnDamageTaken;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFDeathSignature OnDeath;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFInvulnerabilitySignature OnInvulnerabilityChanged;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Health") FKCFReviveSignature OnRevive;

	// --- Queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetHealth() const { return Health; }
	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetMaxHealth() const { return MaxHealth; }
	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetShield() const { return Shield; }
	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetMaxShield() const { return MaxShield; }
	UFUNCTION(BlueprintPure, Category = "KCF|Health") bool IsDead() const { return bDead; }
	UFUNCTION(BlueprintPure, Category = "KCF|Health") bool IsInvulnerable() const { return InvulnerabilityStacks > 0; }

	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetHealthPercent() const;
	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetShieldPercent() const;

	/** Combined health + shield as a fraction of their combined maxima. Useful for one HUD bar. */
	UFUNCTION(BlueprintPure, Category = "KCF|Health") float GetCombinedDurabilityPercent() const;

	/** Tags granted by active effects, consulted by the damage pipeline for immunity. */
	UFUNCTION(BlueprintPure, Category = "KCF|Health")
	const FGameplayTagContainer& GetActiveTags() const { return ActiveTags; }

	// --- Configuration ---------------------------------------------------------------------------

	/** Applies durability values from a vehicle definition. Server-authoritative. */
	void ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData);

	/** Sets maxima directly, for damageable actors that are not karts. Server only. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void InitialiseHealth(float InMaxHealth, float InMaxShield, bool bFillToMax = true);

	// --- Server-only mutators ----------------------------------------------------------------------

	/**
	 * Applies damage that the pipeline has already validated.
	 * Not Blueprint-callable: all damage must go through UKCFDamageSubsystem::ApplyDamage so that
	 * friendly fire, immunity, assists and kill attribution are handled exactly once.
	 */
	FKCFDamageResult ApplyDamage(const FKCFDamageInfo& DamageInfo);

	/** Restores health, capped at the maximum. Returns the amount actually restored. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	float Heal(float Amount);

	/** Restores shield, capped at the maximum. Returns the amount actually restored. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	float RestoreShield(float Amount);

	/** Temporarily raises the shield ceiling, used by shield powerups. Returns the new maximum. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	float AddTemporaryShield(float Amount);

	/**
	 * Adds or removes one invulnerability stack. Stacked rather than boolean so that spawn
	 * protection and a shield powerup overlapping cannot cancel each other when the first expires.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void PushInvulnerability();

	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void PopInvulnerability();

	/** Adds a stack and removes it automatically after DurationSeconds. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void GrantTimedInvulnerability(float DurationSeconds);

	/** Removes every invulnerability stack. Used when spawn protection is broken by firing. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void ClearAllInvulnerability();

	/** Grants a tag while an effect is active. Server only; replicated to all. */
	void AddActiveTag(const FGameplayTag& Tag);
	void RemoveActiveTag(const FGameplayTag& Tag);

	/** Restores the actor to full and clears the dead flag. Used on respawn. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void Revive();

	/** Kills the actor outright, attributing it to Killer. Used by kill volumes and out-of-bounds. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Health")
	void KillImmediately(AActor* Killer, EKCFDamageCategory Category);

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "KCF|Health")
	float Health = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Health")
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Shield, BlueprintReadOnly, Category = "KCF|Health")
	float Shield = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Health")
	float MaxShield = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "KCF|Health")
	bool bDead = false;

	/**
	 * Replicated as a count rather than a bool so that clients can display "protected" correctly
	 * while two overlapping sources are active.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Invulnerability, BlueprintReadOnly, Category = "KCF|Health")
	int32 InvulnerabilityStacks = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Health")
	FGameplayTagContainer ActiveTags;

	/** Shield regeneration rate from the vehicle definition. */
	float ShieldRegenPerSecond = 0.f;
	float ShieldRegenDelay = 5.f;

	/** Base maximum shield, so temporary shield bonuses can be removed without losing the base. */
	float BaseMaxShield = 0.f;

	FTimerHandle ShieldRegenTimerHandle;
	FTimerHandle InvulnerabilityTimerHandle;

	/** Client-side mirrors used to compute deltas for the change delegates. */
	float LastKnownHealth = 0.f;
	float LastKnownShield = 0.f;

	UFUNCTION() void OnRep_Health();
	UFUNCTION() void OnRep_Shield();
	UFUNCTION() void OnRep_Dead();
	UFUNCTION() void OnRep_Invulnerability();

	/** Schedules the regeneration timer after the configured delay. Server only. */
	void ScheduleShieldRegen();

	/** Runs on a repeating timer while the shield is below maximum. Stops itself when full. */
	void TickShieldRegen();

	/** Common death path for damage and for KillImmediately. */
	void HandleDeath(const FKCFDamageInfo& FatalDamage);
};
