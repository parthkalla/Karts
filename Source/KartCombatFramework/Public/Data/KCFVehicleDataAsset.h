// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFVehicleDataAsset.generated.h"

class UKCFAudioDataAsset;
class UKCFMovementDataAsset;
class UMaterialInterface;
class UNiagaraSystem;
class USkeletalMesh;
class UStaticMesh;

/** Placement of one visual wheel. Purely cosmetic for the default arcade movement backend. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFWheelVisualConfig
{
	GENERATED_BODY()

	/** Socket or bone on the vehicle mesh this wheel attaches to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel")
	FName AttachSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel")
	TSoftObjectPtr<UStaticMesh> WheelMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 34.f;

	/** Steered wheels rotate with the steering input; driven wheels spin with forward speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel")
	bool bSteered = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel")
	bool bDriven = true;

	/** Maximum visual steering angle for this wheel, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wheel", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSteerAngle = 35.f;
};

/**
 * A complete kart definition: identity, durability, handling profile and presentation.
 *
 * Gameplay classes hold a pointer to one of these and read from it; they never contain tuning
 * numbers of their own. Every heavy reference is soft, so a vehicle-select screen can display fifty
 * karts while only the chosen one is resident.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFVehicleDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	UKCFVehicleDataAsset();

	// --- Handling ---------------------------------------------------------------------------

	/**
	 * Handling profile. Required: a kart with no movement data cannot be driven, and the framework
	 * refuses to silently substitute values that a designer did not author.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	TObjectPtr<UKCFMovementDataAsset> Movement;

	/** Chassis mass, kg. Used for collision damage and knockback resistance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "1.0", Units = "kg"))
	float Mass = 220.f;

	/** Collision half-extents of the kart body, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (Units = "cm"))
	FVector CollisionExtent = FVector(90.f, 55.f, 40.f);

	// --- Durability ---------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	/** Shield pool absorbed before health. 0 means the kart has no shield. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability", meta = (ClampMin = "0.0"))
	float MaxShield = 0.f;

	/** Shield regenerated per second once regeneration starts. 0 disables regeneration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability", meta = (ClampMin = "0.0"))
	float ShieldRegenPerSecond = 0.f;

	/** Seconds without taking damage before shield regeneration begins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability", meta = (ClampMin = "0.0", Units = "s"))
	float ShieldRegenDelay = 5.f;

	/** Per-category damage multipliers. Absent categories use 1.0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Durability")
	TMap<EKCFDamageCategory, float> DamageMultipliers;

	// --- Boost -----------------------------------------------------------------------------------

	/** Top-speed and acceleration multiplier while boosting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float BoostMultiplier = 1.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.1", Units = "s"))
	float BoostDuration = 1.8f;

	/** Seconds after a boost ends before another can be started. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0.0", Units = "s"))
	float BoostCooldown = 6.f;

	/**
	 * Number of boost charges held at once. Charges refill one per BoostCooldown.
	 * More than one charge lets a player bank a boost for an escape, which is a meaningfully
	 * different feel from a single cooldown and is worth exposing as data.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boost", meta = (ClampMin = "0", ClampMax = "9"))
	int32 BoostCharges = 1;

	// --- Combat loadout ----------------------------------------------------------------------------

	/** Number of weapon/powerup slots this kart carries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1", ClampMax = "8"))
	int32 InventorySlots = 1;

	/** Weapon granted on spawn. Leave unset for a kart that starts unarmed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<class UKCFWeaponDataAsset> DefaultWeapon;

	/** Socket on the vehicle mesh that projectiles are spawned from, server-side. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	FName MuzzleSocket = TEXT("Muzzle");

	/** Socket that deployables (mines, traps) are dropped from. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	FName DropSocket = TEXT("Drop");

	// --- Presentation -------------------------------------------------------------------------------

	/** Skeletal body. Used when set; otherwise StaticVehicleMesh is used. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USkeletalMesh> VehicleMesh;

	/** Static body, for simple karts that need no skeleton. Cheaper on mobile. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UStaticMesh> StaticVehicleMesh;

	/** Material overrides applied by element index. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	/**
	 * Index of the material element whose vector parameter is driven with the team colour.
	 * INDEX_NONE disables team tinting.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	int32 TeamColorMaterialIndex = INDEX_NONE;

	/** Scalar/vector parameter name used for team tinting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FName TeamColorParameterName = TEXT("TeamColor");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TArray<FKCFWheelVisualConfig> Wheels;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> BoostEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> DriftEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UNiagaraSystem> DestructionEffect;

	/** Engine, impact and voice audio for this kart. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UKCFAudioDataAsset> Audio;

	// --- Camera ----------------------------------------------------------------------------------------

	/** Default spring-arm length, cm. The camera component reads this at possession time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float CameraDistance = 520.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (Units = "cm"))
	FVector CameraOffset = FVector(0.f, 0.f, 150.f);

	/** Field of view at standstill, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "30.0", ClampMax = "140.0"))
	float BaseFOV = 85.f;

	/** Field of view at top speed, degrees. The difference is the speed-sensation budget. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "30.0", ClampMax = "160.0"))
	float MaxSpeedFOV = 100.f;

	/** Additional FOV added while boosting, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float BoostFOVBonus = 8.f;

	// --- Accessors --------------------------------------------------------------------------------------

	/** Damage multiplier for a category, defaulting to 1 when unauthored. */
	UFUNCTION(BlueprintPure, Category = "KCF|Vehicle")
	float GetDamageMultiplier(EKCFDamageCategory Category) const;

	/** True when a skeletal or static body mesh has been assigned. */
	UFUNCTION(BlueprintPure, Category = "KCF|Vehicle")
	bool HasVisualMesh() const;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFVehicle"); }
	//~ End UKCFDataAssetBase
};
