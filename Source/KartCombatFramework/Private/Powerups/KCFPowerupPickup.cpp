// Original work, MIT licensed.

#include "Powerups/KCFPowerupPickup.h"

#include "Components/KCFPowerupComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/KCFPowerupDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Powerups/KCFPowerupSpawner.h"
#include "Vehicles/KCFKartPawn.h"

AKCFPowerupPickup::AKCFPowerupPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CollectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectionSphere"));
	CollectionSphere->InitSphereRadius(140.f);
	CollectionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollectionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	CollectionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	CollectionSphere->CanCharacterStepUpOn = ECB_No;
	SetRootComponent(CollectionSphere);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(CollectionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);

	bReplicates = true;
	SetReplicateMovement(false);

	// A pickup changes twice in its life: when it appears and when it is taken. Between those it
	// should not consume a replication slot at all.
	NetDormancy = DORM_Initial;
	SetNetUpdateFrequency(2.f);
	NetPriority = 1.f;
}

void AKCFPowerupPickup::BeginPlay()
{
	Super::BeginPlay();

	BaseMeshLocation = MeshComponent->GetRelativeLocation();

	if (HasAuthority())
	{
		CollectionSphere->OnComponentBeginOverlap.AddDynamic(this, &AKCFPowerupPickup::HandleOverlap);
	}
	else
	{
		// A client overlap proves nothing, so it is never even subscribed to. The collision shape
		// stays enabled so that the server's copy and the client's copy agree about geometry, but
		// nothing local acts on it.
		CollectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	ApplyPresentation();
	UpdateCosmeticTick();
}

void AKCFPowerupPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AKCFPowerupPickup, Powerup, COND_InitialOnly);
	DOREPLIFETIME(AKCFPowerupPickup, bCollected);
}

void AKCFPowerupPickup::InitialisePickup(UKCFPowerupDataAsset* InPowerup, AKCFPowerupSpawner* InSpawner)
{
	if (!HasAuthority())
	{
		return;
	}

	Powerup = InPowerup;
	OwningSpawner = InSpawner;
	bCollected = false;
}

void AKCFPowerupPickup::HandleOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	TryCollect(OtherActor);
}

void AKCFPowerupPickup::TryCollect(AActor* Collector)
{
	if (!HasAuthority() || bCollected || !Powerup)
	{
		return;
	}

	AKCFKartPawn* Kart = Cast<AKCFKartPawn>(Collector);
	if (!Kart || !Kart->CanAct())
	{
		return;
	}

	UKCFPowerupComponent* PowerupComponent = Kart->GetPowerupComponent();
	if (!PowerupComponent)
	{
		return;
	}

	// The component decides. A full inventory or an active per-player cooldown means the crate is
	// left in the world rather than consumed for nothing.
	if (!PowerupComponent->CollectPowerup(Powerup))
	{
		return;
	}

	bCollected = true;
	FlushNetDormancy();
	CollectionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OnCollected.Broadcast(Kart, Powerup);
	OnRep_Collected();

	if (AKCFPowerupSpawner* Spawner = OwningSpawner.Get())
	{
		Spawner->NotifyPickupCollected(this);
	}
	else
	{
		// Standalone pickup placed by hand in a level: nothing owns its respawn, so it just goes.
		SetLifeSpan(0.1f);
	}
}

void AKCFPowerupPickup::OnRep_Powerup()
{
	ApplyPresentation();
	UpdateCosmeticTick();
}

void AKCFPowerupPickup::OnRep_Collected()
{
	if (!bCollected)
	{
		return;
	}

	SetActorHiddenInGame(true);
	UpdateCosmeticTick();

	if (GetNetMode() != NM_DedicatedServer)
	{
		BP_OnCollected();
	}
}

void AKCFPowerupPickup::ApplyPresentation()
{
	// Nothing to render on a dedicated server, and loading the mesh there would waste memory for
	// the entire match.
	if (GetNetMode() == NM_DedicatedServer || !Powerup)
	{
		return;
	}

	if (UStaticMesh* Mesh = Powerup->PickupMesh.LoadSynchronous())
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
}

void AKCFPowerupPickup::UpdateCosmeticTick()
{
	const bool bWantsTick =
		GetNetMode() != NM_DedicatedServer
		&& !bCollected
		&& (SpinRateDegreesPerSecond != 0.f || BobAmplitude > 0.f);

	SetActorTickEnabled(bWantsTick);
}

void AKCFPowerupPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SpinRateDegreesPerSecond != 0.f)
	{
		MeshComponent->AddLocalRotation(FRotator(0.f, SpinRateDegreesPerSecond * DeltaSeconds, 0.f));
	}

	if (BobAmplitude > 0.f)
	{
		BobTime += DeltaSeconds;
		const float Offset = FMath::Sin(BobTime * BobFrequency * 2.f * PI) * BobAmplitude;
		MeshComponent->SetRelativeLocation(BaseMeshLocation + FVector(0.f, 0.f, Offset));
	}
}
