// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFWeaponDataAsset.generated.h"

class AKCFDeployable;
class AKCFProjectile;
class UKCFPowerupEffect;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;

/**
 * A complete weapon definition.
 *
 * There is deliberately no C++ subclass per weapon. A rocket, a homing missile, a bomb, a mine, an
 * EMP and a shield are all instances of this asset with different delivery modes and numbers. Adding
 * a weapon is a content task; adding a genuinely new *kind* of delivery is the only thing that needs
 * code, and that is one new case in EKCFWeaponDelivery.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFWeaponDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	// --- Delivery ------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery")
	EKCFWeaponDelivery Delivery = EKCFWeaponDelivery::Projectile;

	/** Projectile spawned for Delivery == Projectile. Soft: only loaded when the weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::Projectile", EditConditionHides))
	TSoftClassPtr<AKCFProjectile> ProjectileClass;

	/** Deployable placed for Delivery == Deployable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::Deployable", EditConditionHides))
	TSoftClassPtr<AKCFDeployable> DeployableClass;

	/** Effect applied to the firing kart for Delivery == Self (shield, nitro, repair). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::Self", EditConditionHides))
	TSoftClassPtr<UKCFPowerupEffect> SelfEffectClass;

	/** Maximum range for Delivery == Hitscan, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::Hitscan", EditConditionHides, ClampMin = "1.0", Units = "cm"))
	float HitscanRange = 8000.f;

	/** Radius for Delivery == AreaEffect, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::AreaEffect", EditConditionHides, ClampMin = "1.0", Units = "cm"))
	float AreaEffectRadius = 1200.f;

	/** Deployables are dropped this far behind the kart, cm. Negative places them ahead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Delivery", meta = (EditCondition = "Delivery == EKCFWeaponDelivery::Deployable", EditConditionHides, Units = "cm"))
	float DeployBehindDistance = 220.f;

	// --- Damage ---------------------------------------------------------------------------------

	/** Damage at the point of impact, before the target's category multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float Damage = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	EKCFDamageCategory DamageCategory = EKCFDamageCategory::Projectile;

	/** Radius of the explosion on impact, cm. 0 means a single-target hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0", Units = "cm"))
	float ExplosionRadius = 450.f;

	/** Damage at the edge of ExplosionRadius as a fraction of Damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumFalloffDamageFraction = 0.25f;

	/** Impulse applied to anything damaged, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float Knockback = 900.f;

	/** Whether this weapon can damage the kart that fired it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	bool bCanDamageInstigator = false;

	/** Tags attached to the resulting FKCFDamageInfo, for project-specific resistances. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	FGameplayTagContainer DamageTags;

	// --- Projectile behaviour ---------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ProjectileSpeed = 4200.f;

	/** Seconds before an un-hit projectile expires. Also bounds how long it costs replication. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.1", Units = "s"))
	float ProjectileLifetime = 6.f;

	/** Gravity scale on the projectile. 0 flies flat, 1 arcs like a grenade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.0"))
	float ProjectileGravityScale = 0.f;

	/** Projectile acquires a target and homes towards it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	bool bHoming = false;

	/** Homing acceleration magnitude. Higher turns tighter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bHoming", ClampMin = "0.0"))
	float HomingStrength = 9000.f;

	/** Maximum distance at which a homing target can be acquired, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bHoming", ClampMin = "0.0", Units = "cm"))
	float HomingAcquisitionRange = 9000.f;

	/** Half-angle of the forward cone searched for a homing target, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bHoming", ClampMin = "1.0", ClampMax = "180.0"))
	float HomingAcquisitionAngle = 45.f;

	/** Bounces off world geometry rather than detonating on it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	bool bBounceOffWorld = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bBounceOffWorld", ClampMin = "0.0", ClampMax = "1.0"))
	float Bounciness = 0.6f;

	// --- Deployable behaviour ------------------------------------------------------------------------

	/** Seconds a deployable stays armed before removing itself. 0 means it never expires. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deployable", meta = (ClampMin = "0.0", Units = "s"))
	float DeployableLifetime = 25.f;

	/** Seconds after placement before a deployable can be triggered, protecting the placer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deployable", meta = (ClampMin = "0.0", Units = "s"))
	float DeployableArmDelay = 0.75f;

	/** Trigger radius for proximity deployables, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Deployable", meta = (ClampMin = "1.0", Units = "cm"))
	float DeployableTriggerRadius = 180.f;

	// --- Usage limits ---------------------------------------------------------------------------------

	/** Shots held when the weapon is granted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "1"))
	int32 Ammo = 1;

	/** Upper bound when ammo is topped up by a second pickup of the same weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "1"))
	int32 MaxAmmo = 1;

	/** Minimum seconds between shots. Enforced on the server; the client only predicts it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "0.0", Units = "s"))
	float FireInterval = 0.45f;

	/** Additional cooldown applied after the last round is spent, before the weapon is dropped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "0.0", Units = "s"))
	float PostFireCooldown = 0.f;

	/** Projectiles fired per shot. Values above 1 spread across SpreadAngle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "1", ClampMax = "16"))
	int32 ProjectilesPerShot = 1;

	/** Total spread cone, degrees, used when ProjectilesPerShot > 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SpreadAngle = 0.f;

	/** The weapon is removed from the inventory once ammo is exhausted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage")
	bool bRemoveWhenEmpty = true;

	/** Firing is permitted while spawn protection is active. Usually false, to prevent safe shooting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Usage")
	bool bUsableDuringSpawnProtection = false;

	// --- Presentation ----------------------------------------------------------------------------------

	/** Optional mesh mounted on the kart while the weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UStaticMesh> EquippedMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> MuzzleEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> ImpactSound;

	/** Shown in the kill feed. Falls back to DisplayName when empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText KillFeedVerb;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFWeapon"); }
	//~ End UKCFDataAssetBase
};
