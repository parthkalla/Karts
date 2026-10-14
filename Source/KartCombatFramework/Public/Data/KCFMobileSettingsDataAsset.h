// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFMobileSettingsDataAsset.generated.h"

/**
 * Tuning for touch control feel.
 *
 * Nothing here describes a widget, a screen position or a resolution. The plugin never places a
 * control on screen; a project Blueprint draws whatever it likes and calls the mobile API on
 * UKCFKartInputComponent, which applies these values to the raw axis it is given. That split is what
 * keeps the C++ free of any assumption about screen size or safe areas.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFMobileSettingsDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	// --- Steering -------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering")
	EKCFSteeringMode SteeringMode = EKCFSteeringMode::Direct;

	/** Input below this magnitude is treated as zero, 0..1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SteeringDeadzone = 0.08f;

	/** Multiplier applied after the deadzone is removed and the remainder rescaled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SteeringSensitivity = 1.f;

	/**
	 * Response curve exponent. 1 is linear; above 1 gives fine control near centre at the cost of
	 * reaching full lock later, which is what most players want on a small touchscreen.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.2", ClampMax = "4.0"))
	float SteeringExponent = 1.6f;

	/** Rate the steering value returns to centre when the finger lifts, units/s. 0 snaps instantly. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0"))
	float SteeringReturnSpeed = 6.f;

	/** Drag distance, in normalised screen width, that produces full lock in Relative mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float RelativeSteeringTravel = 0.18f;

	/** Device roll in degrees that produces full lock in Tilt mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float TiltFullLockDegrees = 30.f;

	/** Roll offset treated as level, so the player can hold the device at a comfortable angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "-45.0", ClampMax = "45.0"))
	float TiltNeutralDegrees = 0.f;

	// --- Throttle --------------------------------------------------------------------------------

	/** Throttle is held at full automatically; the player only steers, brakes and fights. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throttle")
	bool bAutoAccelerate = true;

	/** Throttle applied when auto-acceleration is on, 0..1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throttle", meta = (EditCondition = "bAutoAccelerate", ClampMin = "0.0", ClampMax = "1.0"))
	float AutoAccelerateThrottle = 1.f;

	/** Auto-acceleration pauses while the brake is held rather than fighting it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Throttle", meta = (EditCondition = "bAutoAccelerate"))
	bool bAutoAccelerateYieldsToBrake = true;

	// --- Buttons -----------------------------------------------------------------------------------

	/** Drift button latches on tap instead of requiring the finger to stay down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buttons")
	bool bDriftIsToggle = false;

	/** Boost button latches on tap. Boost ends on its own timer regardless. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buttons")
	bool bBoostIsToggle = true;

	/** Seconds a fire press is remembered when the weapon is still on cooldown. 0 disables buffering. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buttons", meta = (ClampMin = "0.0", ClampMax = "1.0", Units = "s"))
	float FireInputBufferSeconds = 0.25f;

	/** Holding fire repeats at the weapon's fire rate instead of needing one tap per shot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Buttons")
	bool bFireRepeatsWhileHeld = true;

	// --- Camera --------------------------------------------------------------------------------------

	/** Look sensitivity multiplier for touch drag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float LookSensitivity = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	bool bInvertLookY = false;

	/** Camera yaws back behind the kart on its own when the player is not looking around. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera")
	bool bAutoCenterCamera = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera", meta = (EditCondition = "bAutoCenterCamera", ClampMin = "0.0", Units = "s"))
	float AutoCenterDelaySeconds = 1.2f;

	// --- Feedback --------------------------------------------------------------------------------------

	/** Fire a haptic pulse on impacts and pickups. Projects implement the pulse itself. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	bool bHapticsEnabled = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (EditCondition = "bHapticsEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HapticIntensity = 0.6f;

	/**
	 * Applies deadzone, exponent and sensitivity to one raw axis value.
	 * Pure and side-effect free so it can be unit-tested without a world.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Mobile")
	float ProcessSteeringAxis(float RawAxis) const;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFMobileSettings"); }
	//~ End UKCFDataAssetBase
};
