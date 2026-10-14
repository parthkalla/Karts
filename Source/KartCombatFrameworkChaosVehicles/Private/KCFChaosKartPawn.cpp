// Original work, MIT licensed.

#include "KCFChaosKartPawn.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "KCFChaosKartMovementComponent.h"

AKCFChaosKartPawn::AKCFChaosKartPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UKCFChaosKartMovementComponent>(AKCFKartPawn::KartMovementComponentName))
{
	// Reparent so the skeletal mesh is the simulating body. The box stays, because the rest of the
	// framework uses it for pickup and trigger overlaps, but it stops being the physics root.
	SkeletalBody->SetupAttachment(nullptr);
	SetRootComponent(SkeletalBody);
	CollisionBox->SetupAttachment(SkeletalBody);

	SkeletalBody->SetVisibility(true);
	SkeletalBody->SetCollisionProfileName(TEXT("Vehicle"));
	SkeletalBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SkeletalBody->BodyInstance.bSimulatePhysics = true;
	SkeletalBody->BodyInstance.bNotifyRigidBodyCollision = true;
	// Karts are small and fast; without continuous detection they tunnel through arena walls at
	// boost speed, which is the one failure a player always notices.
	SkeletalBody->BodyInstance.bUseCCD = true;
	SkeletalBody->bBlendPhysics = true;
	SkeletalBody->SetGenerateOverlapEvents(true);
	SkeletalBody->SetCanEverAffectNavigation(false);

	// Query-only: two colliding bodies on one kart would fight each other inside the solver.
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	CollisionBox->SetNotifyRigidBodyCollision(false);
	CollisionBox->SetGenerateOverlapEvents(true);

	if (KartMovement)
	{
		KartMovement->SetUpdatedComponent(SkeletalBody);
	}
}

UKCFChaosKartMovementComponent* AKCFChaosKartPawn::GetChaosKartMovement() const
{
	return Cast<UKCFChaosKartMovementComponent>(KartMovement);
}

void AKCFChaosKartPawn::ApplyVehicleVisuals()
{
	// Unlike the arcade backend, the mesh here is not decoration: the physics asset attached to it
	// is the simulated body and the wheel bones are what the solver steers. A dedicated server that
	// skipped loading it would have a kart that cannot move, so this part runs on every net mode.
	if (VehicleData)
	{
		if (USkeletalMesh* Skeletal = VehicleData->VehicleMesh.LoadSynchronous())
		{
			SkeletalBody->SetSkeletalMesh(Skeletal);
		}
		else
		{
			UE_LOG(LogKartCombatFramework, Error,
				TEXT("Vehicle '%s' has no skeletal mesh. A Chaos kart cannot be simulated from a static mesh; set VehicleMesh on the vehicle Data Asset."),
				*GetNameSafe(VehicleData));
		}
	}

	// The base pass handles materials and team tinting, and returns immediately on a dedicated
	// server, which is correct for everything it does.
	Super::ApplyVehicleVisuals();
}
