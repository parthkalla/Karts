// Original work, MIT licensed.

#include "Match/KCFRaceCheckpoint.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Match/KCFRaceGameMode.h"
#include "Vehicles/KCFKartPawn.h"

AKCFRaceCheckpoint::AKCFRaceCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// Not replicated: a gate has no observable state. What a client sees is the lap counter on the
	// player state, which replicates on its own.
	bReplicates = false;

	TriggerVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerVolume"));
	TriggerVolume->SetBoxExtent(FVector(80.f, 600.f, 300.f));
	TriggerVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerVolume->SetGenerateOverlapEvents(true);
	SetRootComponent(TriggerVolume);

	RespawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("RespawnPoint"));
	RespawnPoint->SetupAttachment(TriggerVolume);
}

void AKCFRaceCheckpoint::BeginPlay()
{
	Super::BeginPlay();

	// Overlap handling is subscribed on the server only. A client that ran this would be counting
	// laps nobody asked it to count, and the result would be discarded anyway.
	if (HasAuthority())
	{
		TriggerVolume->OnComponentBeginOverlap.AddDynamic(this, &AKCFRaceCheckpoint::OnTriggerBeginOverlap);
	}
	else
	{
		TriggerVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

FTransform AKCFRaceCheckpoint::GetRespawnTransform() const
{
	return RespawnPoint ? RespawnPoint->GetComponentTransform() : GetActorTransform();
}

void AKCFRaceCheckpoint::OnTriggerBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	AKCFKartPawn* Kart = Cast<AKCFKartPawn>(OtherActor);
	if (!Kart || !HasAuthority())
	{
		return;
	}

	if (AKCFRaceGameMode* RaceMode = GetWorld() ? GetWorld()->GetAuthGameMode<AKCFRaceGameMode>() : nullptr)
	{
		RaceMode->NotifyCheckpointReached(Kart, this);
	}
}
