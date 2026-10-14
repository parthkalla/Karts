// Original work, MIT licensed.

#pragma once

#include "ChaosWheeledVehicleMovementComponent.h"
#include "CoreMinimal.h"
#include "Interfaces/KCFVehicleMovementInterface.h"
#include "KCFChaosKartMovementComponent.generated.h"

class UKCFMovementDataAsset;

/**
 * Drives a kart with the Chaos wheeled vehicle solver instead of the framework's arcade simulation.
 *
 * This is a translation layer, not a second simulation. Handling - engine, transmission, steering
 * curves, wheel setups, suspension - is configured on this component exactly as it would be on any
 * Chaos vehicle. What the class adds is the IKCFVehicleMovement surface the rest of the framework
 * talks to, so weapons, boost, powerups, damage and respawn work against a Chaos vehicle without a
 * single gameplay file knowing the solver exists.
 *
 * Three concepts have no direct Chaos equivalent and are implemented here rather than faked:
 *
 *   Speed multipliers. A Chaos vehicle has no top-speed scalar to multiply. A boost is therefore a
 *   forward acceleration applied while the multiplier is above one, and a multiplier below one
 *   becomes a deceleration. Applied as an acceleration change rather than a force, so a heavy kart
 *   and a light kart gain speed at the same rate and tuning does not have to track vehicle mass.
 *
 *   Drift. The framework's drift is the arcade handbrake-slide. Here it maps to the Chaos
 *   handbrake, and IsDrifting reports the handbrake being held *and* the body actually sliding,
 *   measured from lateral velocity, so the drift VFX and the drift-boost charge do not trigger
 *   from a handbrake tap at a standstill.
 *
 *   Ground contact. A short downward sweep from the body. The solver's per-wheel contact state
 *   would be the more precise answer, but gameplay only ever asks "is this kart airborne", and a
 *   sweep gives that without binding the bridge to a particular wheel setup.
 *
 * Setup requirement: a Chaos vehicle needs a skeletal mesh with a physics asset and wheel bones as
 * its simulating root. AKCFChaosKartPawn does that reparenting; use it (or a subclass) rather than
 * AKCFKartPawn directly. See Documentation/VehicleSystem.md.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORKCHAOSVEHICLES_API UKCFChaosKartMovementComponent : public UChaosWheeledVehicleMovementComponent, public IKCFVehicleMovement
{
	GENERATED_BODY()

public:
	UKCFChaosKartMovementComponent();

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	//~ Begin IKCFVehicleMovement
	virtual void SetVehicleMovementData(UKCFMovementDataAsset* InMovementData) override;
	virtual void SetThrottleInput(float Throttle) override;
	virtual void SetSteeringInput(float Steering) override;
	virtual void SetBrakeInput(float Brake) override;
	virtual void SetDriftInput(bool bDrifting) override;
	virtual void SetSpeedMultiplier(FName SourceId, float Multiplier) override;
	virtual void ClearSpeedMultiplier(FName SourceId) override;
	virtual void ApplyKnockback(const FVector& Impulse) override;
	virtual void SetMovementEnabled(bool bEnabled) override;
	virtual void ResetVehicle(const FTransform& NewTransform) override;
	virtual float GetForwardSpeed() const override;
	virtual float GetSpeedPercent() const override;
	virtual bool IsGrounded() const override;
	virtual bool IsDrifting() const override;
	virtual bool IsMovementEnabled() const override { return bKCFMovementEnabled; }
	//~ End IKCFVehicleMovement

	/** Combined multiplier from every registered source. 1 when nothing is boosting. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetCombinedSpeedMultiplier() const { return CombinedSpeedMultiplier; }

protected:
	/**
	 * Top speed used to turn cm/s into the 0..1 fraction the HUD, camera FOV and drift threshold
	 * read. Chaos derives its real top speed from the whole drivetrain, which is not a number this
	 * component can ask for, so it is declared here instead of guessed.
	 * Overridden by UKCFMovementDataAsset::MaxSpeed when the kart's vehicle definition supplies one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Movement", meta = (ClampMin = "1.0", Units = "cm/s"))
	float ReferenceTopSpeed = 2000.f;

	/**
	 * Forward acceleration produced by a speed multiplier of 2.0, in cm/s^2.
	 * The applied acceleration is this value scaled by (Multiplier - 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Movement", meta = (ClampMin = "0.0"))
	float SpeedMultiplierAcceleration = 1800.f;

	/** Length of the downward sweep used to answer IsGrounded, measured from the body origin, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Movement", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundProbeDistance = 120.f;

	/** Lateral speed above which a held handbrake counts as a drift, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DriftLateralSpeedThreshold = 200.f;

	/** Knockback impulses are clamped to this velocity change, cm/s, so stacked hits cannot launch a kart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxKnockbackSpeed = 2500.f;

	/** Reference values only: Chaos handling comes from this component's own vehicle setup. */
	UPROPERTY(Transient)
	TObjectPtr<UKCFMovementDataAsset> MovementData;

	TMap<FName, float> SpeedMultiplierSources;
	float CombinedSpeedMultiplier = 1.f;

	bool bKCFMovementEnabled = true;
	bool bDriftRequested = false;

	/** Recomputes CombinedSpeedMultiplier and enables tick only while it has work to do. */
	void RecomputeSpeedMultiplier();

	/** Applies the boost or drag force for the current multiplier. */
	void ApplySpeedMultiplierForce(float DeltaTime);

	/** The simulating body, or null before the component is set up. */
	UPrimitiveComponent* GetSimulatingBody() const;
};
