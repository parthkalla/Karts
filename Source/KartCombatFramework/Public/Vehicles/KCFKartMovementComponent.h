// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/KCFVehicleMovementInterface.h"
#include "KCFKartMovementComponent.generated.h"

class UKCFMovementDataAsset;

/**
 * The default arcade kart simulation.
 *
 * Deliberately not a physics-constraint vehicle. An arcade kart wants predictable, tunable,
 * identical behaviour on every machine, and a swept-body kinematic simulation gives that for a
 * fraction of the cost of a wheeled vehicle solver - which matters a great deal when the target is
 * a mobile client running eight karts.
 *
 * The model:
 *   - Longitudinal speed is integrated towards a target derived from throttle, brake and the
 *     active speed multipliers, using an acceleration curve keyed on current speed.
 *   - Steering rotates the body yaw directly. Velocity is then re-projected onto the new forward
 *     axis by an amount governed by Grip, which is what produces understeer, and by DriftGrip,
 *     which is what produces a slide.
 *   - Knockback lives in its own velocity channel that decays independently, so a rocket hit
 *     pushes the kart without corrupting its driving speed.
 *
 * Networking: this component simulates on the server and on the autonomous proxy. Simulated proxies
 * do not run the simulation at all - they receive the replicated transform from the pawn's movement
 * replication and interpolate. See Documentation/Multiplayer.md for the reconciliation model and
 * its known limitation (smoothed correction, not full rollback prediction).
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFKartMovementComponent : public UPawnMovementComponent, public IKCFVehicleMovement
{
	GENERATED_BODY()

public:
	UKCFKartMovementComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	//~ Begin IKCFVehicleMovement
	virtual void SetVehicleMovementData(UKCFMovementDataAsset* InMovementData) override { SetMovementData(InMovementData); }
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
	virtual bool IsGrounded() const override { return bGrounded; }
	virtual bool IsDrifting() const override { return bDrifting; }
	virtual bool IsMovementEnabled() const override { return bMovementEnabled; }
	//~ End IKCFVehicleMovement

	/** Handling profile. Set by the kart pawn from its vehicle Data Asset before BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Movement")
	void SetMovementData(UKCFMovementDataAsset* InMovementData);

	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	UKCFMovementDataAsset* GetMovementData() const { return MovementData; }

	/** Combined multiplier from every registered source. Always at least 0. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetCombinedSpeedMultiplier() const { return CombinedSpeedMultiplier; }

	/** Visual-only chassis yaw offset for the drift lean, degrees. Read by the pawn's mesh update. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetDriftVisualYaw() const { return DriftVisualYaw; }

	/** Surface normal under the kart. Up vector when airborne. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	FVector GetGroundNormal() const { return GroundNormal; }

	/** Speed along forward as a signed fraction of max, clamped to -1..1. */
	UFUNCTION(BlueprintPure, Category = "KCF|Movement")
	float GetSignedSpeedPercent() const;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UKCFMovementDataAsset> MovementData;

	// --- Input state ----------------------------------------------------------------------------

	float ThrottleInput = 0.f;
	float BrakeInput = 0.f;
	float RawSteeringInput = 0.f;

	/** Steering after interpolation towards RawSteeringInput. This is what the simulation uses. */
	float SmoothedSteering = 0.f;

	bool bDriftRequested = false;
	bool bDrifting = false;
	bool bMovementEnabled = true;

	// --- Simulation state ------------------------------------------------------------------------

	/** Velocity from driving. Kept separate from knockback so the two decay independently. */
	FVector DriveVelocity = FVector::ZeroVector;

	/** Velocity from impulses. Decays towards zero at KnockbackDecayRate. */
	FVector KnockbackVelocity = FVector::ZeroVector;

	/** Vertical velocity, integrated separately so slopes do not bleed into forward speed. */
	float VerticalVelocity = 0.f;

	bool bGrounded = true;
	FVector GroundNormal = FVector::UpVector;
	float DriftVisualYaw = 0.f;

	/** Named speed multipliers. Small map; sources are things like "Boost" and "SpeedPowerup". */
	TMap<FName, float> SpeedMultiplierSources;
	float CombinedSpeedMultiplier = 1.f;

	// --- Simulation steps ----------------------------------------------------------------------------

	/** Sweeps downward to establish ground contact and the surface normal. */
	void UpdateGroundContact();

	/** Integrates longitudinal speed from throttle, brake and the acceleration curve. */
	void UpdateLongitudinal(float DeltaTime);

	/** Rotates the body and re-projects velocity according to grip. */
	void UpdateSteering(float DeltaTime);

	/** Applies gravity and clamps to terminal velocity while airborne. */
	void UpdateVertical(float DeltaTime);

	/** Decays the knockback channel. */
	void UpdateKnockback(float DeltaTime);

	/** Performs the swept move and handles wall impacts. */
	void PerformMove(float DeltaTime);

	/** Aligns the chassis to the ground normal over time. */
	void UpdateGroundAlignment(float DeltaTime);

	/** Recomputes CombinedSpeedMultiplier from the source map. */
	void RecomputeSpeedMultiplier();

	/** Effective top speed after multipliers. */
	float GetEffectiveMaxSpeed() const;

	/** True when this machine should run the simulation rather than consume replicated transforms. */
	bool ShouldSimulate() const;
};
