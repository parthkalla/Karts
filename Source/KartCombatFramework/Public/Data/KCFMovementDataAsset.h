// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFMovementDataAsset.generated.h"

/**
 * A complete arcade handling profile.
 *
 * Split out of UKCFVehicleDataAsset so that several karts can share one feel while differing in
 * mesh, audio and stats, and so that a designer can iterate on handling in a single asset while a
 * match is running (values are read every frame, never cached at BeginPlay).
 *
 * All units are Unreal units: cm, cm/s, cm/s^2, degrees, degrees/s.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFMovementDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	UKCFMovementDataAsset();

	// --- Longitudinal -------------------------------------------------------------------------

	/** Top forward speed with no multipliers applied, cm/s. 1400 is roughly 50 km/h. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "1.0", Units = "cm/s"))
	float MaxSpeed = 1800.f;

	/** Top reverse speed, cm/s. Positive value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxReverseSpeed = 600.f;

	/** Forward acceleration at full throttle, cm/s^2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "1.0"))
	float Acceleration = 2400.f;

	/** Deceleration applied while braking, cm/s^2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "1.0"))
	float BrakingDeceleration = 4000.f;

	/** Deceleration applied when neither throttle nor brake is held, cm/s^2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed", meta = (ClampMin = "0.0"))
	float CoastingDeceleration = 900.f;

	/**
	 * Scales acceleration by current speed fraction, so a kart can pull away hard from a standstill
	 * and taper off near top speed without a second set of tuning values.
	 * X axis: speed as a fraction of MaxSpeed (0..1). Y axis: acceleration multiplier.
	 * An empty curve means a flat multiplier of 1.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Speed")
	FRuntimeFloatCurve AccelerationBySpeedCurve;

	// --- Steering -----------------------------------------------------------------------------

	/** Yaw rate at full lock and low speed, degrees/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "1.0"))
	float TurnRate = 160.f;

	/**
	 * Scales TurnRate by speed so the kart is not twitchy at top speed and is not undrivable when
	 * crawling. X: speed fraction (0..1). Y: turn-rate multiplier. Empty curve means flat 1.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering")
	FRuntimeFloatCurve TurnRateBySpeedCurve;

	/** How fast the steering input ramps towards the requested value, units/s. High = instant. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.1"))
	float SteeringInterpSpeed = 8.f;

	/** Steering authority while airborne, 0..1 of the grounded rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Steering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.25f;

	// --- Grip and drift -------------------------------------------------------------------------

	/**
	 * Fraction of lateral (sideways) velocity removed per second while gripping.
	 * 1 means perfectly on-rails, 0 means a frictionless slide.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Grip = 0.92f;

	/** Lateral grip while drifting. Lower than Grip is what makes a drift slide. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DriftGrip = 0.35f;

	/** Extra steering authority while drifting, multiplies TurnRate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float DriftTurnMultiplier = 1.45f;

	/** Minimum speed fraction required before a drift will engage. Stops standstill spinning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDriftSpeedPercent = 0.35f;

	/** Visual-only chassis yaw offset at full drift, degrees. Gameplay never reads this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float DriftVisualYawOffset = 22.f;

	// --- Airborne -------------------------------------------------------------------------------

	/** Gravity applied to the kart, cm/s^2. Arcade karts usually want heavier-than-real gravity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float Gravity = 2400.f;

	/** Downward velocity is clamped to this, cm/s. Prevents tunnelling on long falls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "1.0"))
	float TerminalVelocity = 4000.f;

	/** Distance below the body probed for ground contact, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "1.0"))
	float GroundTraceDistance = 60.f;

	/** Slopes steeper than this are treated as walls rather than drivable ground, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxDrivableSlopeAngle = 50.f;

	/** How quickly the chassis aligns to the ground normal, degrees/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float GroundAlignSpeed = 360.f;

	// --- Impacts ---------------------------------------------------------------------------------

	/** Fraction of velocity retained when hitting a wall, 0..1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WallRestitution = 0.25f;

	/** Knockback impulses decay at this rate, 1/s. Higher recovers control faster. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.1"))
	float KnockbackDecayRate = 3.5f;

	/** Knockback velocity is clamped to this magnitude, cm/s, so stacked hits cannot launch a kart. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0"))
	float MaxKnockbackSpeed = 2500.f;

	// --- Helpers -----------------------------------------------------------------------------------

	/** Evaluates AccelerationBySpeedCurve, returning 1 when the curve is empty. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetAccelerationMultiplier(float SpeedPercent) const;

	/** Evaluates TurnRateBySpeedCurve, returning 1 when the curve is empty. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetTurnRateMultiplier(float SpeedPercent) const;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFMovement"); }
	//~ End UKCFDataAssetBase
};
