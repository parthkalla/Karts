// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFInputConfigDataAsset.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * The Enhanced Input bindings the framework knows how to use.
 *
 * The plugin ships no input assets of its own, because an Input Action is content and a project's
 * actions should live in that project. What the plugin owns is the *contract*: these are the named
 * slots gameplay can be driven from. A project authors its own actions and points them at these
 * fields, and every action is optional - a mobile-only build can leave the whole asset unset and
 * drive the kart entirely through the touch API.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFInputConfigDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	/** Added to the local player when the kart is possessed, and removed when it is not. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Context")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	/** Priority the context is added with. Higher wins against a lower-priority context. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Context")
	int32 MappingPriority = 0;

	// --- Driving -----------------------------------------------------------------------------

	/**
	 * 2D movement: X steers, Y drives. A gamepad left stick or WASD binds here.
	 * When set, it supersedes the separate Steer / Throttle / Brake actions.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> MoveAction;

	/** 1D axis, -1..+1. Used when steering is bound separately from throttle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> SteerAction;

	/** 1D axis, 0..1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> ThrottleAction;

	/** 1D axis, 0..1. Becomes reverse once the kart is near a standstill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> BrakeAction;

	/** Held while drifting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> DriftAction;

	/** Pressed to request a boost. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<UInputAction> BoostAction;

	// --- Combat -------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<UInputAction> UsePowerupAction;

	/** Selects the next occupied inventory slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<UInputAction> CycleSlotAction;

	// --- Camera and system --------------------------------------------------------------------

	/** 2D look delta. Cosmetic: it moves the camera and nothing else. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	TSoftObjectPtr<UInputAction> LookAction;

	/** Resets the camera behind the kart. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	TSoftObjectPtr<UInputAction> ResetCameraAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "System")
	TSoftObjectPtr<UInputAction> PauseAction;

	/** Requests a stuck-kart reset. Rate-limited on the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "System")
	TSoftObjectPtr<UInputAction> ResetKartAction;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFInputConfig"); }
	//~ End UKCFDataAssetBase
};
