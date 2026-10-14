// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "UObject/Interface.h"
#include "KCFDamageableInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UKCFDamageable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the damage pipeline can hurt: karts, destructible props, arena objectives.
 *
 * This is Blueprint-implementable on purpose. A destructible crate in a project should not need C++,
 * and the only contract it has to honour is "apply this damage on the server and report what
 * happened". UKCFDamageSubsystem never calls the implementation on a client.
 */
class KARTCOMBATFRAMEWORK_API IKCFDamageable
{
	GENERATED_BODY()

public:
	/**
	 * Applies already-validated damage. Called on the server only.
	 * Implementations must not re-run friendly-fire or invulnerability checks; the pipeline has
	 * already done so and passing the same damage twice through those filters causes double-dipping.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Damage")
	FKCFDamageResult ApplyKCFDamage(const FKCFDamageInfo& DamageInfo);

	/** True when the target can no longer be damaged because it is already dead/destroyed. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Damage")
	bool IsDeadOrDestroyed() const;

	/**
	 * True when incoming damage must be ignored (invulnerability, spawn protection, a shield that
	 * grants immunity rather than absorption). Checked by the pipeline before ApplyKCFDamage.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Damage")
	bool IsImmuneToDamage(const FKCFDamageInfo& DamageInfo) const;

	/** Player state to credit for a kill on this target. Null for unowned world objects. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Damage")
	AKCFPlayerState* GetOwningKCFPlayerState() const;
};
