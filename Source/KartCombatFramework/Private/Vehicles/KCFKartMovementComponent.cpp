// Original work, MIT licensed.

#include "Vehicles/KCFKartMovementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFMovementDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Below this speed the kart is treated as stationary for drift and reverse decisions. */
	constexpr float KCFStandstillSpeed = 20.f;

	/** Minimum tick delta the simulation will integrate, guarding against a hitching frame. */
	constexpr float KCFMaxSimDelta = 1.f / 20.f;
}

UKCFKartMovementComponent::UKCFKartMovementComponent()
{
	// This is one of only three components in the framework that ticks. A vehicle simulation is
	// inherently per-frame: integrating it on a timer would make handling frame-rate dependent in a
	// way players feel immediately. Tick is disabled entirely on simulated proxies at BeginPlay.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	bAutoRegisterUpdatedComponent = true;
	bUpdateOnlyIfRendered = false;
}

void UKCFKartMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!ShouldSimulate())
	{
		// Simulated proxies receive the authoritative transform through the pawn's movement
		// replication. Running the simulation as well would fight that replication every frame and
		// waste the cost of the sweep on every remote kart.
		SetComponentTickEnabled(false);
	}

	if (!MovementData)
	{
		KCF_REPORT_RECOVERABLE(LogKartCombatFramework,
			TEXT("Kart movement has no UKCFMovementDataAsset. The kart will not move. Assign Movement on the vehicle Data Asset."));
	}
}

void UKCFKartMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!MovementData || !UpdatedComponent || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// Clamp the step so a frame spike (level streaming, a GC pause on mobile) cannot teleport a kart
	// through a wall. The lost time is simply not simulated, which is preferable to a tunnelling bug.
	const float Step = FMath::Min(DeltaTime, KCFMaxSimDelta);

	UpdateGroundContact();
	UpdateLongitudinal(Step);
	UpdateSteering(Step);
	UpdateVertical(Step);
	UpdateKnockback(Step);
	PerformMove(Step);
	UpdateGroundAlignment(Step);

	// UMovementComponent::Velocity is what the rest of the engine reads (including the pawn's own
	// movement replication), so the two channels are combined into it once per tick.
	Velocity = DriveVelocity + KnockbackVelocity + FVector(0.f, 0.f, VerticalVelocity);
	UpdateComponentVelocity();
}

bool UKCFKartMovementComponent::ShouldSimulate() const
{
	const APawn* OwnerPawn = PawnOwner;
	if (!OwnerPawn)
	{
		return false;
	}

	// Server simulates everything it has authority over; a client simulates only the kart it drives.
	return OwnerPawn->HasAuthority() || OwnerPawn->IsLocallyControlled();
}

// -------------------------------------------------------------------------------------------------
// Interface: input
// -------------------------------------------------------------------------------------------------

void UKCFKartMovementComponent::SetThrottleInput(float Throttle)
{
	ThrottleInput = FMath::IsFinite(Throttle) ? FMath::Clamp(Throttle, 0.f, 1.f) : 0.f;
}

void UKCFKartMovementComponent::SetSteeringInput(float Steering)
{
	RawSteeringInput = FMath::IsFinite(Steering) ? FMath::Clamp(Steering, -1.f, 1.f) : 0.f;
}

void UKCFKartMovementComponent::SetBrakeInput(float Brake)
{
	BrakeInput = FMath::IsFinite(Brake) ? FMath::Clamp(Brake, 0.f, 1.f) : 0.f;
}

void UKCFKartMovementComponent::SetDriftInput(bool bInDrifting)
{
	bDriftRequested = bInDrifting;
}

// -------------------------------------------------------------------------------------------------
// Interface: effects
// -------------------------------------------------------------------------------------------------

void UKCFKartMovementComponent::SetSpeedMultiplier(FName SourceId, float Multiplier)
{
	if (SourceId.IsNone() || !FMath::IsFinite(Multiplier))
	{
		return;
	}

	// Keyed by source so two overlapping effects cannot clobber each other, and so a source that
	// expires removes exactly its own contribution.
	SpeedMultiplierSources.Add(SourceId, FMath::Max(0.f, Multiplier));
	RecomputeSpeedMultiplier();
}

void UKCFKartMovementComponent::ClearSpeedMultiplier(FName SourceId)
{
	if (SpeedMultiplierSources.Remove(SourceId) > 0)
	{
		RecomputeSpeedMultiplier();
	}
}

void UKCFKartMovementComponent::RecomputeSpeedMultiplier()
{
	float Combined = 1.f;
	for (const TPair<FName, float>& Pair : SpeedMultiplierSources)
	{
		Combined *= Pair.Value;
	}

	// Bounded so a stack of buffs cannot produce a kart the collision sweep cannot keep up with.
	CombinedSpeedMultiplier = FMath::Clamp(Combined, 0.f, 4.f);
}

void UKCFKartMovementComponent::ApplyKnockback(const FVector& Impulse)
{
	if (Impulse.ContainsNaN() || !MovementData)
	{
		return;
	}

	KnockbackVelocity += Impulse;

	// Clamping the accumulated channel rather than each impulse means three simultaneous rockets
	// cannot launch a kart out of the arena, while a single large hit still feels heavy.
	if (KnockbackVelocity.SizeSquared() > FMath::Square(MovementData->MaxKnockbackSpeed))
	{
		KnockbackVelocity = KnockbackVelocity.GetSafeNormal() * MovementData->MaxKnockbackSpeed;
	}

	// An upward component is applied through the vertical channel so the kart actually leaves the
	// ground instead of being pressed into it by the next ground snap.
	if (Impulse.Z > 0.f)
	{
		VerticalVelocity = FMath::Max(VerticalVelocity, Impulse.Z);
		bGrounded = false;
	}
}

void UKCFKartMovementComponent::SetMovementEnabled(bool bEnabled)
{
	if (bMovementEnabled == bEnabled)
	{
		return;
	}

	bMovementEnabled = bEnabled;

	if (!bEnabled)
	{
		// Zeroing drive velocity rather than all velocity: a kart frozen for the countdown should
		// still settle onto the ground under gravity.
		DriveVelocity = FVector::ZeroVector;
		ThrottleInput = 0.f;
		BrakeInput = 0.f;
		RawSteeringInput = 0.f;
		SmoothedSteering = 0.f;
		bDrifting = false;
	}
}

void UKCFKartMovementComponent::ResetVehicle(const FTransform& NewTransform)
{
	DriveVelocity = FVector::ZeroVector;
	KnockbackVelocity = FVector::ZeroVector;
	VerticalVelocity = 0.f;
	Velocity = FVector::ZeroVector;
	SmoothedSteering = 0.f;
	RawSteeringInput = 0.f;
	ThrottleInput = 0.f;
	BrakeInput = 0.f;
	bDrifting = false;
	bDriftRequested = false;
	DriftVisualYaw = 0.f;
	SpeedMultiplierSources.Reset();
	RecomputeSpeedMultiplier();

	if (UpdatedComponent)
	{
		UpdatedComponent->SetWorldTransform(NewTransform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
}

// -------------------------------------------------------------------------------------------------
// Interface: queries
// -------------------------------------------------------------------------------------------------

float UKCFKartMovementComponent::GetForwardSpeed() const
{
	if (!UpdatedComponent)
	{
		return 0.f;
	}

	return FVector::DotProduct(DriveVelocity, UpdatedComponent->GetForwardVector());
}

float UKCFKartMovementComponent::GetSpeedPercent() const
{
	const float MaxSpeed = GetEffectiveMaxSpeed();
	if (MaxSpeed <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(DriveVelocity.Size() / MaxSpeed, 0.f, 1.f);
}

float UKCFKartMovementComponent::GetSignedSpeedPercent() const
{
	const float MaxSpeed = GetEffectiveMaxSpeed();
	if (MaxSpeed <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(GetForwardSpeed() / MaxSpeed, -1.f, 1.f);
}

float UKCFKartMovementComponent::GetEffectiveMaxSpeed() const
{
	return MovementData ? MovementData->MaxSpeed * CombinedSpeedMultiplier : 0.f;
}

void UKCFKartMovementComponent::SetMovementData(UKCFMovementDataAsset* InMovementData)
{
	MovementData = InMovementData;
}

// -------------------------------------------------------------------------------------------------
// Simulation
// -------------------------------------------------------------------------------------------------

void UKCFKartMovementComponent::UpdateGroundContact()
{
	UWorld* World = GetWorld();
	if (!World || !UpdatedComponent || !MovementData)
	{
		return;
	}

	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector End = Start - FVector(0.f, 0.f, MovementData->GroundTraceDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KCFGroundTrace), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(PawnOwner);

	FHitResult Hit;
	// A single line trace rather than a sweep: the kart body is already swept during the move, and
	// the ground probe only needs to answer "is there drivable surface beneath me".
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	if (bHit)
	{
		const float SlopeAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Hit.ImpactNormal.Z), -1.f, 1.f)));
		bGrounded = SlopeAngle <= MovementData->MaxDrivableSlopeAngle;
		GroundNormal = bGrounded ? Hit.ImpactNormal : FVector::UpVector;
	}
	else
	{
		bGrounded = false;
		GroundNormal = FVector::UpVector;
	}
}

void UKCFKartMovementComponent::UpdateLongitudinal(float DeltaTime)
{
	if (!UpdatedComponent || !MovementData)
	{
		return;
	}

	const FVector Forward = UpdatedComponent->GetForwardVector();
	const float CurrentForwardSpeed = FVector::DotProduct(DriveVelocity, Forward);
	const float MaxSpeed = GetEffectiveMaxSpeed();
	const float SpeedPercent = (MaxSpeed > 0.f) ? FMath::Abs(CurrentForwardSpeed) / MaxSpeed : 0.f;

	float NewForwardSpeed = CurrentForwardSpeed;

	if (!bMovementEnabled)
	{
		// Frozen: bleed off speed quickly but do not snap, so a kart frozen mid-corner settles
		// naturally rather than stopping dead.
		NewForwardSpeed = FMath::FInterpConstantTo(NewForwardSpeed, 0.f, DeltaTime, MovementData->BrakingDeceleration);
	}
	else if (ThrottleInput > 0.f)
	{
		const float AccelMultiplier = MovementData->GetAccelerationMultiplier(SpeedPercent);
		// Airborne karts keep their speed but cannot accelerate; wheels need ground to push against.
		const float Accel = MovementData->Acceleration * AccelMultiplier * ThrottleInput * (bGrounded ? 1.f : 0.f);
		NewForwardSpeed = FMath::Min(NewForwardSpeed + Accel * DeltaTime, MaxSpeed);
	}
	else if (BrakeInput > 0.f)
	{
		if (CurrentForwardSpeed > KCFStandstillSpeed)
		{
			NewForwardSpeed -= MovementData->BrakingDeceleration * BrakeInput * DeltaTime;
			NewForwardSpeed = FMath::Max(NewForwardSpeed, 0.f);
		}
		else
		{
			// Brake becomes reverse once the kart is essentially stopped, which is the behaviour a
			// single-button arcade control scheme needs.
			const float ReverseAccel = MovementData->Acceleration * 0.6f * BrakeInput * (bGrounded ? 1.f : 0.f);
			NewForwardSpeed = FMath::Max(NewForwardSpeed - ReverseAccel * DeltaTime, -MovementData->MaxReverseSpeed);
		}
	}
	else
	{
		NewForwardSpeed = FMath::FInterpConstantTo(NewForwardSpeed, 0.f, DeltaTime, MovementData->CoastingDeceleration);
	}

	// Rebuild the drive velocity: the forward component is the integrated speed, the lateral
	// component is whatever grip has not yet removed (handled in UpdateSteering).
	const FVector Lateral = DriveVelocity - Forward * CurrentForwardSpeed;
	DriveVelocity = Forward * NewForwardSpeed + Lateral;
}

void UKCFKartMovementComponent::UpdateSteering(float DeltaTime)
{
	if (!UpdatedComponent || !MovementData)
	{
		return;
	}

	SmoothedSteering = FMath::FInterpTo(SmoothedSteering, bMovementEnabled ? RawSteeringInput : 0.f, DeltaTime, MovementData->SteeringInterpSpeed);

	const float SpeedPercent = GetSpeedPercent();
	const float ForwardSpeed = GetForwardSpeed();

	// Drift engages only above a minimum speed, otherwise holding the drift button at a standstill
	// would let a kart spin on the spot.
	bDrifting = bDriftRequested
		&& bMovementEnabled
		&& bGrounded
		&& SpeedPercent >= MovementData->MinDriftSpeedPercent;

	float TurnRate = MovementData->TurnRate * MovementData->GetTurnRateMultiplier(SpeedPercent);

	if (bDrifting)
	{
		TurnRate *= MovementData->DriftTurnMultiplier;
	}

	if (!bGrounded)
	{
		TurnRate *= MovementData->AirControl;
	}

	// Steering authority scales with speed in a parked kart's favour: at a standstill there is no
	// yaw at all, which stops a stationary kart from pirouetting.
	const float SteerAuthority = FMath::Clamp(FMath::Abs(ForwardSpeed) / FMath::Max(1.f, MovementData->MaxSpeed * 0.15f), 0.f, 1.f);

	// Reversing inverts the steering direction, as it does on a real vehicle.
	const float DirectionSign = (ForwardSpeed < -KCFStandstillSpeed) ? -1.f : 1.f;

	const float YawDelta = SmoothedSteering * TurnRate * SteerAuthority * DirectionSign * DeltaTime;

	if (!FMath::IsNearlyZero(YawDelta))
	{
		const FRotator CurrentRotation = UpdatedComponent->GetComponentRotation();
		const FRotator NewRotation(CurrentRotation.Pitch, CurrentRotation.Yaw + YawDelta, CurrentRotation.Roll);
		MoveUpdatedComponent(FVector::ZeroVector, NewRotation, /*bSweep=*/false);
	}

	// Grip: remove some fraction of the lateral velocity each second. The exponential form keeps the
	// behaviour frame-rate independent, which a linear lerp would not.
	const float GripValue = bDrifting ? MovementData->DriftGrip : MovementData->Grip;
	const float RetainedLateral = FMath::Pow(1.f - FMath::Clamp(GripValue, 0.f, 1.f), DeltaTime);

	const FVector Forward = UpdatedComponent->GetForwardVector();
	const FVector Right = UpdatedComponent->GetRightVector();

	const float ForwardComponent = FVector::DotProduct(DriveVelocity, Forward);
	const float LateralComponent = FVector::DotProduct(DriveVelocity, Right) * RetainedLateral;

	DriveVelocity = Forward * ForwardComponent + Right * LateralComponent;

	// Cosmetic lean, exposed for the mesh to follow. Never read by gameplay.
	const float TargetVisualYaw = bDrifting ? (-SmoothedSteering * MovementData->DriftVisualYawOffset) : 0.f;
	DriftVisualYaw = FMath::FInterpTo(DriftVisualYaw, TargetVisualYaw, DeltaTime, 6.f);
}

void UKCFKartMovementComponent::UpdateVertical(float DeltaTime)
{
	if (!MovementData)
	{
		return;
	}

	if (bGrounded && VerticalVelocity <= 0.f)
	{
		// A small downward bias keeps the kart pressed against the surface so that driving over a
		// crest does not produce a frame of false airtime.
		VerticalVelocity = -50.f;
		return;
	}

	VerticalVelocity -= MovementData->Gravity * DeltaTime;
	VerticalVelocity = FMath::Max(VerticalVelocity, -MovementData->TerminalVelocity);
}

void UKCFKartMovementComponent::UpdateKnockback(float DeltaTime)
{
	if (KnockbackVelocity.IsNearlyZero())
	{
		KnockbackVelocity = FVector::ZeroVector;
		return;
	}

	const float DecayRate = MovementData ? MovementData->KnockbackDecayRate : 3.5f;
	KnockbackVelocity *= FMath::Exp(-DecayRate * DeltaTime);
}

void UKCFKartMovementComponent::PerformMove(float DeltaTime)
{
	if (!UpdatedComponent)
	{
		return;
	}

	const FVector TotalVelocity = DriveVelocity + KnockbackVelocity + FVector(0.f, 0.f, VerticalVelocity);
	const FVector Delta = TotalVelocity * DeltaTime;

	if (Delta.IsNearlyZero())
	{
		return;
	}

	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), /*bSweep=*/true, Hit);

	if (!Hit.IsValidBlockingHit())
	{
		return;
	}

	const float Restitution = MovementData ? MovementData->WallRestitution : 0.25f;

	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.7f)
	{
		// Floor or ceiling contact: kill vertical motion and slide along the surface so slopes are
		// drivable rather than acting as walls.
		VerticalVelocity = (Hit.ImpactNormal.Z > 0.f) ? 0.f : FMath::Min(VerticalVelocity, 0.f);
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact=*/true);
	}
	else
	{
		// Wall contact: reflect a fraction of the velocity and slide off the rest. Reflecting all of
		// it would make every wall a trampoline; absorbing all of it would make glancing blows stop
		// the kart dead.
		const FVector Normal = Hit.ImpactNormal;
		DriveVelocity = (DriveVelocity - 2.f * FVector::DotProduct(DriveVelocity, Normal) * Normal) * Restitution;
		KnockbackVelocity = (KnockbackVelocity - 2.f * FVector::DotProduct(KnockbackVelocity, Normal) * Normal) * Restitution;

		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact=*/true);
	}
}

void UKCFKartMovementComponent::UpdateGroundAlignment(float DeltaTime)
{
	if (!UpdatedComponent || !MovementData)
	{
		return;
	}

	// Only pitch and roll follow the ground; yaw belongs to steering. Building the target from the
	// current forward vector keeps the two concerns from fighting.
	const FVector TargetUp = bGrounded ? GroundNormal : FVector::UpVector;
	const FVector CurrentForward = UpdatedComponent->GetForwardVector();
	const FVector ProjectedForward = FVector::VectorPlaneProject(CurrentForward, TargetUp).GetSafeNormal();

	if (ProjectedForward.IsNearlyZero())
	{
		return;
	}

	const FRotator TargetRotation = FRotationMatrix::MakeFromZX(TargetUp, ProjectedForward).Rotator();
	const FRotator CurrentRotation = UpdatedComponent->GetComponentRotation();
	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, TargetRotation, DeltaTime, MovementData->GroundAlignSpeed);

	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, /*bSweep=*/false);
}
