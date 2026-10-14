// Original work, MIT licensed.

#include "Vehicles/KCFKartCameraComponent.h"

#include "Camera/CameraComponent.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/KCFKartPawn.h"

UKCFKartCameraComponent::UKCFKartCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	bUsePawnControlRotation = false;
	bInheritPitch = false;
	bInheritRoll = false;
	bInheritYaw = true;

	// Lag makes a kart feel heavy and hides a little network jitter on a remote kart. Rotation lag
	// is deliberately faster than location lag: a camera that lags in rotation feels unresponsive.
	bEnableCameraLag = true;
	bEnableCameraRotationLag = true;
	CameraLagSpeed = 9.f;
	CameraRotationLagSpeed = 12.f;
	CameraLagMaxDistance = 220.f;

	bDoCollisionTest = true;
	ProbeSize = 14.f;

	TargetArmLength = 520.f;
	SocketOffset = FVector(0.f, 0.f, 150.f);
}

void UKCFKartCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();

	// The decision is made once, at the registration site, rather than per frame inside Tick. On a
	// dedicated server this component is inert from this point on.
	bHasLocalViewer = World
		&& World->GetNetMode() != NM_DedicatedServer
		&& OwnerPawn
		&& OwnerPawn->IsLocallyControlled();

	if (!bHasLocalViewer)
	{
		SetComponentTickEnabled(false);
	}
}

void UKCFKartCameraComponent::SetManagedCamera(UCameraComponent* InCamera)
{
	ManagedCamera = InCamera;

	if (ManagedCamera)
	{
		ManagedCamera->SetFieldOfView(BaseFOV);
	}
}

void UKCFKartCameraComponent::ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData)
{
	if (!InVehicleData)
	{
		return;
	}

	TargetArmLength = InVehicleData->CameraDistance;
	SocketOffset = InVehicleData->CameraOffset;
	BaseFOV = InVehicleData->BaseFOV;
	MaxSpeedFOV = InVehicleData->MaxSpeedFOV;
	BoostFOVBonus = InVehicleData->BoostFOVBonus;

	if (ManagedCamera)
	{
		ManagedCamera->SetFieldOfView(BaseFOV);
	}
}

void UKCFKartCameraComponent::AddLookInput(const FVector2D& LookDelta)
{
	if (LookDelta.ContainsNaN() || LookDelta.IsNearlyZero())
	{
		return;
	}

	LookOffset.X = FMath::UnwindDegrees(LookOffset.X + LookDelta.X);
	LookOffset.Y = FMath::Clamp(LookOffset.Y + LookDelta.Y, PitchLimits.X, PitchLimits.Y);
	TimeSinceLookInput = 0.f;
}

void UKCFKartCameraComponent::SnapBehindKart()
{
	LookOffset = FVector2D::ZeroVector;
	CurrentDriftYaw = 0.f;
	TimeSinceLookInput = AutoCenterDelay;
}

void UKCFKartCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHasLocalViewer)
	{
		return;
	}

	const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(GetOwner());
	if (!Kart)
	{
		return;
	}

	TimeSinceLookInput += DeltaTime;

	// Auto-centre: after a quiet period the player's manual yaw decays back to zero. Pitch is left
	// alone, because a player who tilted the view down usually wants it to stay there.
	if (AutoCenterDelay > 0.f && TimeSinceLookInput > AutoCenterDelay)
	{
		LookOffset.X = FMath::FInterpTo(LookOffset.X, 0.f, DeltaTime, AutoCenterSpeed);
	}

	// Drift yaw leans the camera into the slide. Smoothed so that a flick of the drift button does
	// not snap the view.
	const float TargetDriftYaw = Kart->IsDrifting() ? (DriftYawInfluence * FMath::Sign(Kart->GetSpeedPercent())) : 0.f;
	CurrentDriftYaw = FMath::FInterpTo(CurrentDriftYaw, TargetDriftYaw, DeltaTime, 4.f);

	SetRelativeRotation(FRotator(LookOffset.Y, LookOffset.X + CurrentDriftYaw, 0.f));

	if (!ManagedCamera)
	{
		return;
	}

	// Speed-driven FOV. The widening is what sells acceleration; without it a kart at top speed
	// looks identical to one crawling.
	float TargetFOV = FMath::Lerp(BaseFOV, MaxSpeedFOV, Kart->GetSpeedPercent());
	if (Kart->IsBoosting())
	{
		TargetFOV += BoostFOVBonus;
	}

	const float NewFOV = FMath::FInterpTo(ManagedCamera->FieldOfView, TargetFOV, DeltaTime, FOVInterpSpeed);
	ManagedCamera->SetFieldOfView(NewFOV);
}
