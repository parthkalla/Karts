// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "KCFDamageSubsystem.generated.h"

class AKCFPlayerState;

/** One recorded contribution towards a future kill. Server-only; never replicated. */
USTRUCT()
struct KARTCOMBATFRAMEWORK_API FKCFDamageLedgerEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AKCFPlayerState> Instigator;

	UPROPERTY()
	float Amount = 0.f;

	UPROPERTY()
	float ServerTime = 0.f;
};

/**
 * The single entry point for all damage in the framework.
 *
 * Everything that hurts anything - projectiles, explosions, mines, collisions, kill volumes - calls
 * ApplyDamage here. Having exactly one pipeline is what makes it possible to state with confidence
 * that friendly fire, invulnerability, score attribution and assist tracking are each handled once
 * and in the same order every time; a second damage path would be a second place for those rules to
 * drift apart.
 *
 * A world subsystem rather than a static library because it owns per-match state: the assist ledger.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFDamageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UWorldSubsystem
	/** Not created on clients: damage is resolved exclusively on the server. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	/** Convenience accessor. Returns null on a pure client world or before the world is ready. */
	UFUNCTION(BlueprintPure, Category = "KCF|Damage", meta = (WorldContext = "WorldContextObject"))
	static UKCFDamageSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Applies damage to one target, running the full pipeline:
	 * authority -> target is damageable -> alive -> friendly fire -> immunity -> category multiplier
	 * -> shield -> health -> knockback -> ledger -> statistics.
	 *
	 * Server only. Calling this on a client returns a rejection rather than doing anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Damage")
	FKCFDamageResult ApplyDamage(AActor* Target, const FKCFDamageInfo& DamageInfo);

	/**
	 * Applies falloff damage to everything within a radius.
	 * @param Origin         Centre of the explosion.
	 * @param Radius         Outer radius in cm. Damage falls off to MinimumDamageFraction at the edge.
	 * @param MinimumDamageFraction  Damage at the rim as a fraction of DamageInfo.DamageAmount.
	 * @param IgnoredActors  Actors excluded from the query, typically the projectile itself.
	 * @return Number of actors actually damaged.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Damage")
	int32 ApplyRadialDamage(
		const FVector& Origin,
		float Radius,
		float MinimumDamageFraction,
		const FKCFDamageInfo& DamageInfo,
		const TArray<AActor*>& IgnoredActors);

	/**
	 * Players who damaged a victim recently enough and heavily enough to earn an assist.
	 * Excludes the killer and the victim themselves.
	 */
	TArray<AKCFPlayerState*> CollectAssisters(const AActor* Victim, const AKCFPlayerState* Killer) const;

	/** Drops every ledger entry for a victim. Called when they die so credit cannot carry over. */
	void ClearLedgerForVictim(const AActor* Victim);

	/** Drops every ledger entry. Called when a match restarts. */
	void ClearAllLedgers();

protected:
	/**
	 * Per-victim damage history. Bounded in two ways: entries older than the assist window are
	 * pruned on access, and the whole ledger for a victim is dropped when they die. Without both,
	 * a long match would accumulate an entry per hit forever.
	 */
	TMap<TWeakObjectPtr<const AActor>, TArray<FKCFDamageLedgerEntry>> AssistLedger;

	/** Records a contribution towards a possible future assist. */
	void RecordLedgerEntry(const AActor* Victim, AKCFPlayerState* Instigator, float Amount);

	/** Removes entries older than the configured window and entries whose instigator is gone. */
	void PruneLedger(TArray<FKCFDamageLedgerEntry>& Entries, float Now) const;

	/**
	 * Applies the rule-set friendly-fire policy.
	 * @return the damage scale to apply: 0 means the damage is refused entirely.
	 */
	float ResolveFriendlyFireScale(const AActor* Target, const FKCFDamageInfo& DamageInfo, EKCFDamageRejectReason& OutReason) const;

	/** Looks up the per-category multiplier on the target's vehicle definition. */
	float ResolveCategoryMultiplier(const AActor* Target, EKCFDamageCategory Category) const;

	/** Pushes a knockback impulse through the target's movement interface, if it has one. */
	void ApplyKnockbackToTarget(AActor* Target, const FKCFDamageInfo& DamageInfo) const;

	/** Current server time, or 0 when there is no world. */
	float GetServerTime() const;
};
