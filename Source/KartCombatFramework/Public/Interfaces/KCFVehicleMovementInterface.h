// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "KCFVehicleMovementInterface.generated.h"

class UKCFMovementDataAsset;

UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UKCFVehicleMovement : public UInterface
{
	GENERATED_BODY()
};

/**
 * The seam between the gameplay framework and whatever actually moves the kart.
 *
 * Every gameplay system in the plugin (weapons, powerups, damage, boost, respawn) talks to the
 * vehicle exclusively through this interface. Nothing outside an implementation of this interface
 * names a physics type, which is what allows the default arcade movement component and the optional
 * Chaos Vehicles bridge to be swapped with no edits anywhere else.
 *
 * Implementations are expected to be UActorComponents living on the kart pawn. This interface is
 * intentionally not Blueprint-implementable: a Blueprint cannot run at the fixed-step rate a vehicle
 * simulation needs, and a half-implemented movement backend is a worse failure than a compile error.
 */
class KARTCOMBATFRAMEWORK_API IKCFVehicleMovement
{
	GENERATED_BODY()

public:
	// --- Configuration ----------------------------------------------------------------------------

	/**
	 * Hands the backend the handling profile from the kart's vehicle definition. Called before
	 * BeginPlay on a freshly spawned kart, and again whenever the definition changes.
	 *
	 * Not pure virtual, because a backend that carries its own tuning - a Chaos wheeled vehicle
	 * configures engine, transmission and wheels on its own component - has nothing to do with an
	 * arcade profile and should say so by not overriding this.
	 */
	virtual void SetVehicleMovementData(UKCFMovementDataAsset* InMovementData) {}

	// --- Input (called every frame by the pawn, on every machine that simulates the kart) -------

	/** 0..1 forward throttle. */
	virtual void SetThrottleInput(float Throttle) = 0;

	/** -1..+1 steering, negative is left. */
	virtual void SetSteeringInput(float Steering) = 0;

	/** 0..1 brake. Implementations decide when braking becomes reverse. */
	virtual void SetBrakeInput(float Brake) = 0;

	/** Drift / handbrake held. Implementations reduce lateral grip and widen the steering range. */
	virtual void SetDriftInput(bool bDrifting) = 0;

	// --- Authority-side effects (server only) ---------------------------------------------------

	/**
	 * Multiplies the effective top speed and acceleration. Multiple sources stack multiplicatively
	 * via a handle so that two expiring boosts cannot clobber each other.
	 * @param SourceId  Unique per effect source; re-using an id replaces that source's contribution.
	 * @param Multiplier 1.0 removes the source's contribution.
	 */
	virtual void SetSpeedMultiplier(FName SourceId, float Multiplier) = 0;

	/** Removes a multiplier previously registered with SetSpeedMultiplier. */
	virtual void ClearSpeedMultiplier(FName SourceId) = 0;

	/** Applies an instantaneous impulse, in cm/s of velocity change. */
	virtual void ApplyKnockback(const FVector& Impulse) = 0;

	/** Suppresses or restores all driving input, e.g. during the pre-match countdown or an EMP. */
	virtual void SetMovementEnabled(bool bEnabled) = 0;

	/** Hard reset: zeroes velocity and teleports the body to a known-good transform. */
	virtual void ResetVehicle(const FTransform& NewTransform) = 0;

	// --- Queries --------------------------------------------------------------------------------

	/** Signed speed along the vehicle's forward axis, cm/s. Negative while reversing. */
	virtual float GetForwardSpeed() const = 0;

	/** Absolute speed as a fraction of the configured maximum, 0..1. Used by HUD and camera FOV. */
	virtual float GetSpeedPercent() const = 0;

	/** True when the vehicle currently has ground contact. */
	virtual bool IsGrounded() const = 0;

	/** True when the implementation considers the vehicle to be drifting. */
	virtual bool IsDrifting() const = 0;

	/** True when driving input is currently being accepted. */
	virtual bool IsMovementEnabled() const = 0;
};
