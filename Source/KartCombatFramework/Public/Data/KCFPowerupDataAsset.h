// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFPowerupDataAsset.generated.h"

class UKCFPowerupEffect;
class UKCFWeaponDataAsset;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;

/**
 * A collectable powerup definition.
 *
 * The behaviour itself lives in a UKCFPowerupEffect subclass; this asset supplies the numbers, the
 * presentation and the scheduling rules. A "grant a weapon" powerup needs no effect class at all -
 * it sets Type to Weapon and points GrantedWeapon at a weapon asset.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFPowerupDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	// --- Behaviour ----------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour")
	EKCFPowerupType Type = EKCFPowerupType::Weapon;

	/**
	 * Weapon placed into the inventory for Type == Weapon.
	 * Soft because a spawner references every powerup in its table and must not drag all of their
	 * weapons, meshes and projectiles into memory at level load.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour", meta = (EditCondition = "Type == EKCFPowerupType::Weapon", EditConditionHides))
	TSoftObjectPtr<UKCFWeaponDataAsset> GrantedWeapon;

	/** Effect applied for every non-Weapon type. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour", meta = (EditCondition = "Type != EKCFPowerupType::Weapon", EditConditionHides))
	TSoftClassPtr<UKCFPowerupEffect> EffectClass;

	/** Seconds the effect lasts, for timed types. Ignored by Instant and Passive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 6.f;

	/** Seconds before the same player can benefit from this powerup again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0", Units = "s"))
	float Cooldown = 0.f;

	/**
	 * Whether a second copy stacks a second instance of the effect.
	 * When false, re-collecting refreshes the remaining duration instead, which is almost always the
	 * behaviour players expect from a speed or shield pickup.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour")
	bool bStackable = false;

	/** Maximum simultaneous stacks when bStackable is true. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour", meta = (EditCondition = "bStackable", ClampMin = "1", ClampMax = "16"))
	int32 MaxStacks = 3;

	/** Applied the instant it is collected rather than occupying an inventory slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour")
	bool bAutoActivateOnPickup = false;

	/**
	 * Generic magnitude, interpreted by the effect class: health for a heal, multiplier for a speed
	 * boost, absorption for a shield. Keeping this generic is what lets one effect class serve many
	 * tuned powerups.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour")
	float Magnitude = 1.f;

	/** Tags granted to the holder while the effect is active, e.g. KCF.Immunity.All. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behaviour")
	FGameplayTagContainer GrantedTags;

	// --- Presentation ---------------------------------------------------------------------------

	/** Mesh used by the world pickup actor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> PickupEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> ActiveEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> ActivateSound;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFPowerup"); }
	//~ End UKCFDataAssetBase
};
