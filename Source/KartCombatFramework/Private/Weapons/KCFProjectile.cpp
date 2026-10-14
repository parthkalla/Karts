// Original work, MIT licensed.

#include "Weapons/KCFProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Interfaces/KCFDamageableInterface.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"
#include "Subsystems/KCFDamageSubsystem.h"
#include "Vehicles/KCFKartPawn.h"

AKCFProjectile::AKCFProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(18.f);
	CollisionSphere->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetNotifyRigidBodyCollision(true);
	CollisionSphere->CanCharacterStepUpOn = ECB_No;
	SetRootComponent(CollisionSphere);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(CollisionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Projectiles are small and fast; a shadow from each of them is a meaningful mobile cost for
	// something no player will ever notice.
	MeshComponent->SetCastShadow(false);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->SetUpdatedComponent(CollisionSphere);
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.f;

	bReplicates = true;
	SetReplicateMovement(true);

	// Replicating at a high rate buys nothing: the client-side projectile moves under its own
	// deterministic projectile movement between updates, and the authoritative hit is the server's.
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(5.f);
	NetPriority = 1.4f;
}

void AKCFProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		CollisionSphere->OnComponentHit.AddDynamic(this, &AKCFProjectile::HandleHit);
	}
	else
	{
		// Clients run no collision at all. A client cannot cause a hit, so giving its copy of the
		// projectile a collision shape would cost query time for no possible gameplay effect.
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AKCFProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Both are set before the actor is ever relevant and never change afterwards.
	DOREPLIFETIME_CONDITION(AKCFProjectile, WeaponData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AKCFProjectile, TeamId, COND_InitialOnly);
}

void AKCFProjectile::InitialiseProjectile(UKCFWeaponDataAsset* InWeaponData, AKCFPlayerState* InInstigatorState, AActor* InInstigatorActor, int32 InTeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	WeaponData = InWeaponData;
	InstigatorPlayerState = InInstigatorState;
	FiringActor = InInstigatorActor;
	TeamId = InTeamId;
	bHasDetonated = false;

	if (!WeaponData)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Projectile spawned with no weapon data; it will expire without effect."));
		SetLifeSpan(1.f);
		return;
	}

	SetOwner(InInstigatorActor);

	ProjectileMovement->InitialSpeed = WeaponData->ProjectileSpeed;
	ProjectileMovement->MaxSpeed = WeaponData->ProjectileSpeed * 1.5f;
	ProjectileMovement->ProjectileGravityScale = WeaponData->ProjectileGravityScale;
	ProjectileMovement->bShouldBounce = WeaponData->bBounceOffWorld;
	ProjectileMovement->Bounciness = WeaponData->Bounciness;
	ProjectileMovement->Velocity = GetActorForwardVector() * WeaponData->ProjectileSpeed;

	if (WeaponData->bHoming)
	{
		if (AActor* Target = AcquireHomingTarget())
		{
			ProjectileMovement->bIsHomingProjectile = true;
			ProjectileMovement->HomingTargetComponent = Target->GetRootComponent();
			ProjectileMovement->HomingAccelerationMagnitude = WeaponData->HomingStrength;
		}
		else
		{
			// No target in the cone: the missile flies straight rather than refusing to fire. A
			// weapon that silently does nothing when nobody is in front of you is a worse experience
			// than one that misses.
			ProjectileMovement->bIsHomingProjectile = false;
		}
	}

	// Lifespan bounds both the projectile's existence and its replication cost.
	SetLifeSpan(WeaponData->ProjectileLifetime);

	// Never collide with the kart that fired: at the muzzle the projectile starts inside its own
	// shooter's collision volume.
	if (FiringActor)
	{
		CollisionSphere->IgnoreActorWhenMoving(FiringActor, true);
	}
}

void AKCFProjectile::ResetForReuse()
{
	bHasDetonated = false;
	WeaponData = nullptr;
	InstigatorPlayerState = nullptr;
	FiringActor = nullptr;
	TeamId = KCF_NO_TEAM;

	if (ProjectileMovement)
	{
		ProjectileMovement->bIsHomingProjectile = false;
		ProjectileMovement->HomingTargetComponent = nullptr;
		ProjectileMovement->Velocity = FVector::ZeroVector;
	}

	SetLifeSpan(0.f);
}

void AKCFProjectile::LifeSpanExpired()
{
	// An expiring projectile detonates where it is, so a rocket that runs out of fuel next to a kart
	// still does something. Weapons that should simply vanish set an explosion radius of zero.
	if (HasAuthority() && !bHasDetonated && WeaponData && WeaponData->ExplosionRadius > 0.f)
	{
		Detonate(GetActorLocation(), FVector::UpVector, nullptr);
		return;
	}

	Super::LifeSpanExpired();
}

bool AKCFProjectile::CanHit(const AActor* Other) const
{
	if (!Other || Other == this)
	{
		return false;
	}

	if (Other == FiringActor && WeaponData && !WeaponData->bCanDamageInstigator)
	{
		return false;
	}

	return true;
}

void AKCFProjectile::HandleHit(UPrimitiveComponent* /*HitComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, FVector /*NormalImpulse*/, const FHitResult& Hit)
{
	if (!HasAuthority() || bHasDetonated)
	{
		return;
	}

	if (OtherActor && !CanHit(OtherActor))
	{
		return;
	}

	// Bouncing projectiles only detonate on something damageable; world geometry just bounces them.
	if (WeaponData && WeaponData->bBounceOffWorld)
	{
		const bool bHitDamageable = OtherActor && OtherActor->GetClass()->ImplementsInterface(UKCFDamageable::StaticClass());
		if (!bHitDamageable)
		{
			return;
		}
	}

	Detonate(Hit.ImpactPoint, Hit.ImpactNormal, OtherActor);
}

void AKCFProjectile::Detonate(const FVector& Location, const FVector& Normal, AActor* DirectHitActor)
{
	if (!HasAuthority() || bHasDetonated)
	{
		return;
	}

	bHasDetonated = true;

	UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this);

	if (DamageSubsystem && WeaponData)
	{
		FKCFDamageInfo Info(WeaponData->Damage, WeaponData->DamageCategory);
		Info.InstigatorPlayerState = InstigatorPlayerState;
		Info.DamageCauser = this;
		Info.HitLocation = Location;
		Info.HitNormal = Normal;
		Info.KnockbackForce = WeaponData->Knockback;
		Info.WeaponId = WeaponData->Id;
		Info.DamageTags = WeaponData->DamageTags;

		if (WeaponData->ExplosionRadius > 0.f)
		{
			TArray<AActor*> Ignored;
			Ignored.Add(this);
			if (FiringActor && !WeaponData->bCanDamageInstigator)
			{
				Ignored.Add(FiringActor);
			}

			DamageSubsystem->ApplyRadialDamage(
				Location,
				WeaponData->ExplosionRadius,
				WeaponData->MinimumFalloffDamageFraction,
				Info,
				Ignored);
		}
		else if (DirectHitActor)
		{
			// Direct hit with no splash: knockback travels along the projectile's path, which reads
			// as being shoved by the shot rather than blown outward.
			Info.KnockbackDirection = GetActorForwardVector();
			DamageSubsystem->ApplyDamage(DirectHitActor, Info);
		}
	}

	MulticastExplode(Location, Normal);

	// Deferred by a frame so the multicast is actually sent before the actor is torn down.
	SetLifeSpan(0.05f);
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
	}
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
}

void AKCFProjectile::MulticastExplode_Implementation(FVector Location, FVector Normal)
{
	// Purely cosmetic, and never run on a dedicated server, which has nothing to play it on.
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	SetActorHiddenInGame(true);
	BP_OnExplode(Location, Normal);
}

AActor* AKCFProjectile::AcquireHomingTarget() const
{
	UWorld* World = GetWorld();
	if (!World || !WeaponData)
	{
		return nullptr;
	}

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const float MaxRangeSq = FMath::Square(WeaponData->HomingAcquisitionRange);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(WeaponData->HomingAcquisitionAngle));

	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;

	// Iterating karts directly rather than running an overlap query: the candidate set is at most
	// the player count, which is far cheaper than a sphere overlap of several thousand centimetres.
	for (TActorIterator<AKCFKartPawn> It(World); It; ++It)
	{
		AKCFKartPawn* Candidate = *It;
		if (!Candidate || !CanHit(Candidate))
		{
			continue;
		}

		if (IKCFDamageable::Execute_IsDeadOrDestroyed(Candidate))
		{
			continue;
		}

		// Never home onto a team-mate, regardless of the friendly-fire setting: a missile that
		// chases your own team is a bug no matter how the rules are configured.
		if (TeamId != KCF_NO_TEAM && IKCFTeamAgent::ResolveTeamId(Candidate) == TeamId)
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - Origin;
		const float DistSq = ToCandidate.SizeSquared();
		if (DistSq > MaxRangeSq || DistSq < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector Direction = ToCandidate.GetSafeNormal();
		const float Dot = FVector::DotProduct(Forward, Direction);
		if (Dot < MinDot)
		{
			continue;
		}

		// Prefer targets that are both close and well centred. Dividing the alignment by distance
		// keeps a distant, perfectly aligned kart from beating a nearby one just off-axis.
		const float Score = Dot / FMath::Sqrt(DistSq);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	return Best;
}
