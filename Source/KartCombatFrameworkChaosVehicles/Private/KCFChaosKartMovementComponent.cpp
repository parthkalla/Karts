// Original work, MIT licensed.

#include "KCFChaosKartMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Data/KCFMovementDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

// Several IKCFVehicleMovement members share a name with a non-virtual UChaosVehicleMovementComponent
// function. The declaration in this class overrides the interface and hides the Chaos one, so every
// call that is meant to reach the solver is written with an explicit qualification below. That is
// deliberate and the reason those calls look verbose.
using FChaosVehicle = UChaosVehicleMovementComponent;

UKCFChaosKartMovementComponent::UKCFChaosKartMovementComponent()
{
	// Tick stays on unconditionally: the Chaos solver drives itself from this component's tick, so
	// switching it off to save the bridge's own per-frame work would stop the vehicle entirely.
	PrimaryComponentTick.bCanEverTick = true;
}

void UKCFChaosKartMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!FMath::IsNearlyEqual(CombinedSpeedMultiplier, 1.f))
	{
		ApplySpeedMultiplierForce(DeltaTime);
	}
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

void UKCFChaosKartMovementComponent::SetVehicleMovementData(UKCFMovementDataAsset* InMovementData)
{
	MovementData = InMovementData;

	// Only the reference numbers are taken. Everything that shapes how the kart drives - torque
	// curves, gearing, steering curves, suspension - belongs to the Chaos setup on this component,
	// and silently overwriting it from an arcade profile would make the two impossible to tune.
	if (MovementData)
	{
		ReferenceTopSpeed = FMath::Max(1.f, MovementData->MaxSpeed);
		MaxKnockbackSpeed = MovementData->MaxKnockbackSpeed;
	}
}

// -------------------------------------------------------------------------------------------------
// Input
// -------------------------------------------------------------------------------------------------

void UKCFChaosKartMovementComponent::SetThrottleInput(float Throttle)
{
	const float Value = (bKCFMovementEnabled && FMath::IsFinite(Throttle)) ? FMath::Clamp(Throttle, 0.f, 1.f) : 0.f;
	FChaosVehicle::SetThrottleInput(Value);
}

void UKCFChaosKartMovementComponent::SetSteeringInput(float Steering)
{
	const float Value = (bKCFMovementEnabled && FMath::IsFinite(Steering)) ? FMath::Clamp(Steering, -1.f, 1.f) : 0.f;
	FChaosVehicle::SetSteeringInput(Value);
}

void UKCFChaosKartMovementComponent::SetBrakeInput(float Brake)
{
	const float Value = (bKCFMovementEnabled && FMath::IsFinite(Brake)) ? FMath::Clamp(Brake, 0.f, 1.f) : 0.f;
	FChaosVehicle::SetBrakeInput(Value);
}

void UKCFChaosKartMovementComponent::SetDriftInput(bool bInDrifting)
{
	bDriftRequested = bInDrifting && bKCFMovementEnabled;
	FChaosVehicle::SetHandbrakeInput(bDriftRequested);
}

// -------------------------------------------------------------------------------------------------
// Authority-side effects
// -------------------------------------------------------------------------------------------------

void UKCFChaosKartMovementComponent::SetSpeedMultiplier(FName SourceId, float Multiplier)
{
	if (SourceId.IsNone() || !FMath::IsFinite(Multiplier))
	{
		return;
	}

	SpeedMultiplierSources.Add(SourceId, FMath::Clamp(Multiplier, 0.f, 4.f));
	RecomputeSpeedMultiplier();
}

void UKCFChaosKartMovementComponent::ClearSpeedMultiplier(FName SourceId)
{
	if (SpeedMultiplierSources.Remove(SourceId) > 0)
	{
		RecomputeSpeedMultiplier();
	}
}

void UKCFChaosKartMovementComponent::RecomputeSpeedMultiplier()
{
	float Combined = 1.f;
	for (const TPair<FName, float>& Pair : SpeedMultiplierSources)
	{
		Combined *= Pair.Value;
	}

	CombinedSpeedMultiplier = FMath::Clamp(Combined, 0.f, 4.f);
}

void UKCFChaosKartMovementComponent::ApplySpeedMultiplierForce(float DeltaTime)
{
	UPrimitiveComponent* Body = GetSimulatingBody();
	if (!Body || !Body->IsSimulatingPhysics())
	{
		return;
	}

	const float AccelerationScale = CombinedSpeedMultiplier - 1.f;
	const FVector Forward = Body->GetForwardVector();

	if (AccelerationScale < 0.f)
	{
		// A multiplier below one is a slow effect, and a slow effect must never push a stationary
		// kart backwards. The deceleration is only applied while it is actually moving forward.
		if (FChaosVehicle::GetForwardSpeed() <= 0.f)
		{
			return;
		}
	}

	// bAccelChange means the vector is cm/s^2 and the solver scales by mass for us, so tuning does
	// not have to be redone every time a kart's mass changes.
	Body->AddForce(Forward * SpeedMultiplierAcceleration * AccelerationScale, NAME_None, /*bAccelChange*/ true);
}

void UKCFChaosKartMovementComponent::ApplyKnockback(const FVector& Impulse)
{
	UPrimitiveComponent* Body = GetSimulatingBody();
	if (!Body || Impulse.ContainsNaN() || !Body->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Clamped = Impulse.GetClampedToMaxSize(MaxKnockbackSpeed);

	// bVelChange means the vector is a velocity change in cm/s, which is the unit the damage
	// pipeline works in, so a rocket shoves a heavy kart and a light kart by the same amount.
	Body->AddImpulse(Clamped, NAME_None, /*bVelChange*/ true);
}

void UKCFChaosKartMovementComponent::SetMovementEnabled(bool bEnabled)
{
	if (bKCFMovementEnabled == bEnabled)
	{
		return;
	}

	bKCFMovementEnabled = bEnabled;

	if (!bEnabled)
	{
		FChaosVehicle::SetThrottleInput(0.f);
		FChaosVehicle::SetSteeringInput(0.f);
		FChaosVehicle::SetBrakeInput(1.f);
		FChaosVehicle::SetHandbrakeInput(true);
		bDriftRequested = false;
	}
	else
	{
		FChaosVehicle::SetBrakeInput(0.f);
		FChaosVehicle::SetHandbrakeInput(false);
	}
}

void UKCFChaosKartMovementComponent::ResetVehicle(const FTransform& NewTransform)
{
	FChaosVehicle::SetThrottleInput(0.f);
	FChaosVehicle::SetSteeringInput(0.f);
	FChaosVehicle::SetBrakeInput(0.f);
	FChaosVehicle::SetHandbrakeInput(false);
	bDriftRequested = false;

	SpeedMultiplierSources.Reset();
	RecomputeSpeedMultiplier();

	StopMovementImmediately();

	if (UPrimitiveComponent* Body = GetSimulatingBody())
	{
		// TeleportPhysics rather than a plain move: without it the solver keeps the old velocity and
		// the kart arrives at its spawn point already travelling at whatever killed it.
		Body->SetWorldTransform(NewTransform, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);

		if (Body->IsSimulatingPhysics())
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------------------------------

float UKCFChaosKartMovementComponent::GetForwardSpeed() const
{
	return FChaosVehicle::GetForwardSpeed();
}

float UKCFChaosKartMovementComponent::GetSpeedPercent() const
{
	if (ReferenceTopSpeed <= 0.f)
	{
		return 0.f;
	}

	const UPrimitiveComponent* Body = GetSimulatingBody();
	const float Speed = Body ? Body->GetComponentVelocity().Size() : FMath::Abs(FChaosVehicle::GetForwardSpeed());

	return FMath::Clamp(Speed / ReferenceTopSpeed, 0.f, 1.f);
}

bool UKCFChaosKartMovementComponent::IsGrounded() const
{
	const UPrimitiveComponent* Body = GetSimulatingBody();
	const UWorld* World = GetWorld();
	if (!Body || !World)
	{
		return false;
	}

	const FVector Start = Body->GetComponentLocation();
	const FVector End = Start - FVector(0.f, 0.f, GroundProbeDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KCFChaosGroundProbe), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

bool UKCFChaosKartMovementComponent::IsDrifting() const
{
	if (!bDriftRequested)
	{
		return false;
	}

	const UPrimitiveComponent* Body = GetSimulatingBody();
	if (!Body)
	{
		return false;
	}

	// Holding the handbrake is a request, not a drift. Reporting the slide itself is what keeps the
	// drift VFX and the drift-boost charge from triggering on a stationary handbrake tap.
	const float LateralSpeed = FMath::Abs(FVector::DotProduct(Body->GetComponentVelocity(), Body->GetRightVector()));
	return LateralSpeed >= DriftLateralSpeedThreshold;
}

UPrimitiveComponent* UKCFChaosKartMovementComponent::GetSimulatingBody() const
{
	if (UpdatedPrimitive)
	{
		return UpdatedPrimitive;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<UPrimitiveComponent>(OwnerPawn->GetRootComponent()) : nullptr;
}
