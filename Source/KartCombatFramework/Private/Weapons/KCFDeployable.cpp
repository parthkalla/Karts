// Original work, MIT licensed.

#include "Weapons/KCFDeployable.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/World.h"
#include "Interfaces/KCFDamageableInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"
#include "Subsystems/KCFDamageSubsystem.h"
#include "TimerManager.h"

AKCFDeployable::AKCFDeployable()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
	TriggerSphere->InitSphereRadius(180.f);
	TriggerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TriggerSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	TriggerSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	TriggerSphere->CanCharacterStepUpOn = ECB_No;
	SetRootComponent(TriggerSphere);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(TriggerSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);

	bReplicates = true;
	// A deployable never moves after it is placed, so replicating its transform every update is
	// pure waste. The initial spawn transform is all a client needs.
	SetReplicateMovement(false);
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
	NetPriority = 1.2f;

	// Mines spend most of their life doing nothing. Dormancy takes them out of the replication
	// loop entirely until arming or triggering flushes them.
	NetDormancy = DORM_DormantAll;
}

void AKCFDeployable::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		// The client copy is cosmetic only. Only the server decides that a mine went off.
		TriggerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	TriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &AKCFDeployable::HandleTriggerOverlap);

	const float ArmDelay = WeaponData ? WeaponData->DeployableArmDelay : 0.f;
	if (ArmDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(ArmTimerHandle, this, &AKCFDeployable::Arm, ArmDelay, false);
	}
	else
	{
		Arm();
	}
}

void AKCFDeployable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ArmTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AKCFDeployable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AKCFDeployable, WeaponData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AKCFDeployable, TeamId, COND_InitialOnly);
	DOREPLIFETIME(AKCFDeployable, bArmed);
}

void AKCFDeployable::InitialiseDeployable(UKCFWeaponDataAsset* InWeaponData, AKCFPlayerState* InInstigatorState, AActor* InInstigatorActor, int32 InTeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	WeaponData = InWeaponData;
	InstigatorPlayerState = InInstigatorState;
	PlacingActor = InInstigatorActor;
	TeamId = InTeamId;

	if (!WeaponData)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Deployable placed with no weapon data; it will expire without effect."));
		SetLifeSpan(1.f);
		return;
	}

	SetOwner(InInstigatorActor);
	TriggerSphere->SetSphereRadius(WeaponData->DeployableTriggerRadius, false);

	if (WeaponData->DeployableLifetime > 0.f)
	{
		SetLifeSpan(WeaponData->DeployableLifetime);
	}
}

void AKCFDeployable::Arm()
{
	if (!HasAuthority() || bArmed || bHasTriggered)
	{
		return;
	}

	bArmed = true;
	TriggerSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FlushNetDormancy();
	OnRep_Armed();

	// Something may already be sitting on top of the mine when it arms - most often the kart that
	// dropped it, which has not driven away. Arming has to re-check rather than wait for a new
	// overlap event that will never come.
	TArray<AActor*> Overlapping;
	TriggerSphere->GetOverlappingActors(Overlapping);
	for (AActor* Other : Overlapping)
	{
		if (Other && Other != this && Other->GetClass()->ImplementsInterface(UKCFDamageable::StaticClass()))
		{
			Trigger(Other);
			return;
		}
	}
}

void AKCFDeployable::OnRep_Armed()
{
	if (bArmed)
	{
		BP_OnArmed();
	}
}

void AKCFDeployable::HandleTriggerOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || !bArmed || bHasTriggered || !OtherActor || OtherActor == this)
	{
		return;
	}

	if (!OtherActor->GetClass()->ImplementsInterface(UKCFDamageable::StaticClass()))
	{
		return;
	}

	if (IKCFDamageable::Execute_IsDeadOrDestroyed(OtherActor))
	{
		return;
	}

	Trigger(OtherActor);
}

void AKCFDeployable::Trigger(AActor* TriggeringActor)
{
	if (!HasAuthority() || bHasTriggered)
	{
		return;
	}

	bHasTriggered = true;
	TriggerSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetWorldTimerManager().ClearTimer(ArmTimerHandle);

	const FVector Location = GetActorLocation();

	if (UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this); DamageSubsystem && WeaponData)
	{
		FKCFDamageInfo Info(WeaponData->Damage, WeaponData->DamageCategory);
		Info.InstigatorPlayerState = InstigatorPlayerState;
		Info.DamageCauser = this;
		Info.HitLocation = Location;
		Info.HitNormal = FVector::UpVector;
		Info.KnockbackForce = WeaponData->Knockback;
		Info.WeaponId = WeaponData->Id;
		Info.DamageTags = WeaponData->DamageTags;

		if (WeaponData->ExplosionRadius > 0.f)
		{
			TArray<AActor*> Ignored;
			Ignored.Add(this);
			if (PlacingActor && !WeaponData->bCanDamageInstigator)
			{
				Ignored.Add(PlacingActor);
			}

			DamageSubsystem->ApplyRadialDamage(Location, WeaponData->ExplosionRadius, WeaponData->MinimumFalloffDamageFraction, Info, Ignored);
		}
		else if (TriggeringActor)
		{
			// No blast radius: the kart that drove onto it is launched straight up, which reads as
			// being flipped by the trap rather than shoved sideways by nothing.
			Info.KnockbackDirection = FVector::UpVector;
			DamageSubsystem->ApplyDamage(TriggeringActor, Info);
		}
	}

	FlushNetDormancy();
	MulticastTriggered(Location);

	SetActorHiddenInGame(true);
	SetLifeSpan(0.05f);
}

void AKCFDeployable::MulticastTriggered_Implementation(FVector Location)
{
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	SetActorHiddenInGame(true);
	BP_OnTriggered(Location);
}
