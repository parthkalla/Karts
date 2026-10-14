// Original work, MIT licensed.

#include "Components/KCFWeaponComponent.h"

#include "Components/KCFInventoryComponent.h"
#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"
#include "Powerups/KCFPowerupEffect.h"
#include "Subsystems/KCFContentRegistrySubsystem.h"
#include "Subsystems/KCFDamageSubsystem.h"
#include "Vehicles/KCFKartPawn.h"
#include "Weapons/KCFDeployable.h"
#include "Weapons/KCFProjectile.h"

UKCFWeaponComponent::UKCFWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKCFWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UKCFInventoryComponent* Inventory = GetInventory())
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &UKCFWeaponComponent::HandleInventoryChanged);
		Inventory->OnSelectedSlotChanged.AddDynamic(this, &UKCFWeaponComponent::HandleSelectedSlotChanged);
	}

	RefreshCurrentWeapon();
}

void UKCFWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKCFInventoryComponent* Inventory = GetInventory())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &UKCFWeaponComponent::HandleInventoryChanged);
		Inventory->OnSelectedSlotChanged.RemoveDynamic(this, &UKCFWeaponComponent::HandleSelectedSlotChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UKCFWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Only the firing player's own UI cares about the cooldown.
	DOREPLIFETIME_CONDITION(UKCFWeaponComponent, NextFireTime, COND_OwnerOnly);
}

// --- Helpers -------------------------------------------------------------------------------------

AKCFKartPawn* UKCFWeaponComponent::GetKartPawn() const
{
	return Cast<AKCFKartPawn>(GetOwner());
}

UKCFInventoryComponent* UKCFWeaponComponent::GetInventory() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	return Kart ? Kart->GetInventoryComponent() : nullptr;
}

AKCFPlayerState* UKCFWeaponComponent::GetOwningPlayerState() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Pawn->GetPlayerState<AKCFPlayerState>() : nullptr;
}

float UKCFWeaponComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;

	// The synchronised server clock, not local world time: the client's cooldown has to be measured
	// on the same timeline as the server's or the two will disagree by the connection's latency.
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
}

void UKCFWeaponComponent::HandleInventoryChanged()
{
	RefreshCurrentWeapon();
}

void UKCFWeaponComponent::HandleSelectedSlotChanged(int32 /*NewSlot*/)
{
	RefreshCurrentWeapon();
}

void UKCFWeaponComponent::RefreshCurrentWeapon()
{
	UKCFWeaponDataAsset* Resolved = nullptr;

	const UKCFInventoryComponent* Inventory = GetInventory();
	if (Inventory)
	{
		const FKCFInventorySlot Slot = Inventory->GetCurrentItem();
		if (!Slot.IsEmpty() && !Slot.WeaponId.IsNone())
		{
			if (UKCFContentRegistrySubsystem* Registry = UKCFContentRegistrySubsystem::Get(this))
			{
				Resolved = Registry->FindWeapon(Slot.WeaponId);
			}
		}
	}

	if (Resolved == CurrentWeapon)
	{
		return;
	}

	CurrentWeapon = Resolved;
	OnWeaponChanged.Broadcast(CurrentWeapon);
	OnAmmoChanged.Broadcast(GetCurrentAmmo(), CurrentWeapon ? CurrentWeapon->MaxAmmo : 0);
}

void UKCFWeaponComponent::ResetWeaponState()
{
	NextFireTime = 0.f;
	CurrentWeapon = nullptr;
	LiveProjectiles.Reset();
	OnWeaponChanged.Broadcast(nullptr);
	OnAmmoChanged.Broadcast(0, 0);
}

// --- Queries --------------------------------------------------------------------------------------

int32 UKCFWeaponComponent::GetCurrentAmmo() const
{
	const UKCFInventoryComponent* Inventory = GetInventory();
	if (!Inventory)
	{
		return 0;
	}

	const FKCFInventorySlot Slot = Inventory->GetCurrentItem();
	return Slot.WeaponId.IsNone() ? 0 : Slot.Count;
}

float UKCFWeaponComponent::GetRemainingCooldown() const
{
	return FMath::Max(0.f, NextFireTime - GetServerTime());
}

float UKCFWeaponComponent::GetCooldownPercent() const
{
	if (!CurrentWeapon || CurrentWeapon->FireInterval <= 0.f)
	{
		return 1.f;
	}

	const float Remaining = GetRemainingCooldown();
	return FMath::Clamp(1.f - (Remaining / CurrentWeapon->FireInterval), 0.f, 1.f);
}

bool UKCFWeaponComponent::CanFireWeapon() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Kart->CanAct())
	{
		return false;
	}

	if (!CurrentWeapon || GetCurrentAmmo() <= 0)
	{
		return false;
	}

	if (GetRemainingCooldown() > 0.f)
	{
		return false;
	}

	// Firing from inside spawn protection is usually disallowed: otherwise the safest moment in the
	// match is also a free shot, and players learn to camp their own respawn.
	if (!CurrentWeapon->bUsableDuringSpawnProtection && Kart->IsSpawnProtected())
	{
		return false;
	}

	return true;
}

// --- Firing ------------------------------------------------------------------------------------------

void UKCFWeaponComponent::FireCurrentWeapon()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (CanFireWeapon())
		{
			ExecuteFire(CurrentWeapon);
		}
		return;
	}

	if (!CanFireWeapon())
	{
		return;
	}

	// The client starts its own cooldown before the server confirms. This is prediction of the UI
	// only - no projectile, no damage, nothing another player can observe. If the server refuses,
	// ClientFireRejected hands the cooldown back.
	if (CurrentWeapon)
	{
		NextFireTime = GetServerTime() + CurrentWeapon->FireInterval;
		OnRep_NextFireTime();
	}

	ServerFireWeapon();
}

bool UKCFWeaponComponent::ServerFireWeapon_Validate()
{
	// Structural validation only. The RPC carries no arguments, so there is nothing here that could
	// be out of range; the gameplay decision belongs in the implementation where the full state is
	// available.
	return true;
}

void UKCFWeaponComponent::ServerFireWeapon_Implementation()
{
	// Re-resolved rather than trusted: the client's idea of which weapon is selected could be
	// stale, or forged. The server reads its own inventory.
	RefreshCurrentWeapon();

	if (!CanFireWeapon())
	{
		const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
		if (Settings && Settings->bLogRejectedRPCs)
		{
			KCF_LOG(LogKCFNet, Verbose, TEXT("Rejected fire request from %s: weapon=%s ammo=%d cooldown=%.2f"),
				*GetNameSafe(GetOwner()),
				*GetNameSafe(CurrentWeapon),
				GetCurrentAmmo(),
				GetRemainingCooldown());
		}

		ClientFireRejected();
		return;
	}

	ExecuteFire(CurrentWeapon);
}

void UKCFWeaponComponent::ClientFireRejected_Implementation()
{
	// Drop the predicted cooldown so the button becomes usable again immediately rather than after
	// a cooldown the player never actually spent.
	NextFireTime = 0.f;
	OnRep_NextFireTime();
}

void UKCFWeaponComponent::OnRep_NextFireTime()
{
	OnAmmoChanged.Broadcast(GetCurrentAmmo(), CurrentWeapon ? CurrentWeapon->MaxAmmo : 0);
}

void UKCFWeaponComponent::ExecuteFire(UKCFWeaponDataAsset* Weapon)
{
	AKCFKartPawn* Kart = GetKartPawn();
	if (!Weapon || !Kart || !GetOwner()->HasAuthority())
	{
		return;
	}

	const FTransform Muzzle = Kart->GetMuzzleTransform();

	switch (Weapon->Delivery)
	{
	case EKCFWeaponDelivery::Projectile:
		FireProjectiles(Weapon, Muzzle);
		break;
	case EKCFWeaponDelivery::Hitscan:
		FireHitscan(Weapon, Muzzle);
		break;
	case EKCFWeaponDelivery::Deployable:
		PlaceDeployable(Weapon);
		break;
	case EKCFWeaponDelivery::Self:
		ApplySelfEffect(Weapon);
		break;
	case EKCFWeaponDelivery::AreaEffect:
		FireAreaEffect(Weapon);
		break;
	default:
		break;
	}

	NextFireTime = GetServerTime() + Weapon->FireInterval + Weapon->PostFireCooldown;

	ConsumeAmmoAfterFiring(Weapon);

	// Shooting gives away a protected spawn, so protection that is configured to break on fire ends
	// here rather than letting a player trade shots from safety.
	Kart->BreakSpawnProtection();

	OnWeaponFired.Broadcast(Weapon);
	MulticastWeaponFired(Weapon, Muzzle.GetLocation());
}

void UKCFWeaponComponent::ConsumeAmmoAfterFiring(const UKCFWeaponDataAsset* Weapon)
{
	UKCFInventoryComponent* Inventory = GetInventory();
	if (!Inventory || !Weapon)
	{
		return;
	}

	const int32 Slot = Inventory->GetSelectedSlot();
	Inventory->ConsumeItem(Slot, 1);

	// ConsumeItem already clears an exhausted slot. bRemoveWhenEmpty == false means the weapon is
	// meant to stay selected and simply be unusable, so the slot is restored with a zero count.
	if (!Weapon->bRemoveWhenEmpty && Inventory->GetItem(Slot).IsEmpty())
	{
		FKCFInventorySlot Restored;
		Restored.WeaponId = Weapon->Id;
		Restored.Count = 0;
		Inventory->ReplaceItem(Slot, Restored);
	}

	RefreshCurrentWeapon();
	OnAmmoChanged.Broadcast(GetCurrentAmmo(), Weapon->MaxAmmo);
}

void UKCFWeaponComponent::MulticastWeaponFired_Implementation(UKCFWeaponDataAsset* Weapon, FVector MuzzleLocation)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	BP_OnWeaponFired(Weapon, MuzzleLocation);
}

// --- Delivery modes ------------------------------------------------------------------------------------

int32 UKCFWeaponComponent::CountLiveProjectiles() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& Projectile : LiveProjectiles)
	{
		if (Projectile.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void UKCFWeaponComponent::FireProjectiles(UKCFWeaponDataAsset* Weapon, const FTransform& Muzzle)
{
	UWorld* World = GetWorld();
	AKCFKartPawn* Kart = GetKartPawn();
	if (!World || !Kart)
	{
		return;
	}

	UClass* ProjectileClass = Weapon->ProjectileClass.LoadSynchronous();
	if (!ProjectileClass)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Weapon '%s' is a projectile weapon with no projectile class."), *Weapon->Id.ToString());
		return;
	}

	// Expired weak pointers accumulate otherwise, and the per-player cap would eventually refuse
	// every shot.
	LiveProjectiles.RemoveAll([](const TWeakObjectPtr<AActor>& Projectile) { return !Projectile.IsValid(); });

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const int32 MaxLive = Settings ? Settings->MaxLiveProjectilesPerPlayer : 8;
	if (CountLiveProjectiles() >= MaxLive)
	{
		// A bounded number of live projectiles per player is what keeps a pathological fire rate
		// from flooding a mobile client's replication budget.
		return;
	}

	AKCFPlayerState* OwningState = GetOwningPlayerState();
	const int32 TeamId = IKCFTeamAgent::ResolveTeamId(Kart);

	const int32 ShotCount = FMath::Clamp(Weapon->ProjectilesPerShot, 1, 16);
	const float HalfSpread = Weapon->SpreadAngle * 0.5f;

	for (int32 Index = 0; Index < ShotCount; ++Index)
	{
		FRotator Rotation = Muzzle.Rotator();
		if (ShotCount > 1 && HalfSpread > 0.f)
		{
			// Even fan rather than random scatter: a shotgun-style spread that is deterministic is
			// both fairer and reproducible in a test.
			const float Alpha = (ShotCount == 1) ? 0.5f : static_cast<float>(Index) / static_cast<float>(ShotCount - 1);
			Rotation.Yaw += FMath::Lerp(-HalfSpread, HalfSpread, Alpha);
		}

		AKCFProjectile* Projectile = World->SpawnActorDeferred<AKCFProjectile>(
			ProjectileClass, FTransform(Rotation, Muzzle.GetLocation()), Kart, Kart,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (!Projectile)
		{
			continue;
		}

		// Configured before FinishSpawning so BeginPlay sees a fully initialised projectile and the
		// initial replication carries the right data.
		Projectile->InitialiseProjectile(Weapon, OwningState, Kart, TeamId);
		Projectile->FinishSpawning(FTransform(Rotation, Muzzle.GetLocation()));

		LiveProjectiles.Add(Projectile);
	}
}

void UKCFWeaponComponent::FireHitscan(UKCFWeaponDataAsset* Weapon, const FTransform& Muzzle)
{
	UWorld* World = GetWorld();
	AKCFKartPawn* Kart = GetKartPawn();
	UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this);
	if (!World || !Kart || !DamageSubsystem)
	{
		return;
	}

	const FVector Start = Muzzle.GetLocation();
	const FVector End = Start + Muzzle.GetRotation().GetForwardVector() * Weapon->HitscanRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KCFHitscan), false, Kart);
	Params.AddIgnoredActor(Kart);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);
	if (!bHit || !Hit.GetActor())
	{
		return;
	}

	FKCFDamageInfo Info(Weapon->Damage, Weapon->DamageCategory);
	Info.InstigatorPlayerState = GetOwningPlayerState();
	Info.DamageCauser = Kart;
	Info.HitLocation = Hit.ImpactPoint;
	Info.HitNormal = Hit.ImpactNormal;
	Info.KnockbackForce = Weapon->Knockback;
	Info.KnockbackDirection = (End - Start).GetSafeNormal();
	Info.WeaponId = Weapon->Id;
	Info.DamageTags = Weapon->DamageTags;

	if (Weapon->ExplosionRadius > 0.f)
	{
		TArray<AActor*> Ignored;
		if (!Weapon->bCanDamageInstigator)
		{
			Ignored.Add(Kart);
		}
		DamageSubsystem->ApplyRadialDamage(Hit.ImpactPoint, Weapon->ExplosionRadius, Weapon->MinimumFalloffDamageFraction, Info, Ignored);
	}
	else
	{
		DamageSubsystem->ApplyDamage(Hit.GetActor(), Info);
	}
}

void UKCFWeaponComponent::PlaceDeployable(UKCFWeaponDataAsset* Weapon)
{
	UWorld* World = GetWorld();
	AKCFKartPawn* Kart = GetKartPawn();
	if (!World || !Kart)
	{
		return;
	}

	UClass* DeployableClass = Weapon->DeployableClass.LoadSynchronous();
	if (!DeployableClass)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Weapon '%s' is a deployable weapon with no deployable class."), *Weapon->Id.ToString());
		return;
	}

	FTransform DropTransform = Kart->GetDropTransform();
	DropTransform.SetLocation(DropTransform.GetLocation() - Kart->GetActorForwardVector() * Weapon->DeployBehindDistance);

	AKCFDeployable* Deployable = World->SpawnActorDeferred<AKCFDeployable>(
		DeployableClass, DropTransform, Kart, Kart, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Deployable)
	{
		return;
	}

	Deployable->InitialiseDeployable(Weapon, GetOwningPlayerState(), Kart, IKCFTeamAgent::ResolveTeamId(Kart));
	Deployable->FinishSpawning(DropTransform);
}

void UKCFWeaponComponent::ApplySelfEffect(UKCFWeaponDataAsset* Weapon)
{
	AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart)
	{
		return;
	}

	UClass* EffectClass = Weapon->SelfEffectClass.LoadSynchronous();
	if (!EffectClass)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Weapon '%s' is a self-effect weapon with no effect class."), *Weapon->Id.ToString());
		return;
	}

	UKCFPowerupEffect* Effect = NewObject<UKCFPowerupEffect>(this, EffectClass);
	if (!Effect)
	{
		return;
	}

	// A self-delivery weapon is an immediate, un-reverted effect: a repair pack or an instant shield
	// top-up. Anything that needs a duration belongs in the powerup system, which owns scheduling.
	Effect->SourcePowerupId = Weapon->Id;
	Effect->ApplyEffect(Kart, Weapon->Damage);
}

void UKCFWeaponComponent::FireAreaEffect(UKCFWeaponDataAsset* Weapon)
{
	AKCFKartPawn* Kart = GetKartPawn();
	UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this);
	if (!Kart || !DamageSubsystem)
	{
		return;
	}

	FKCFDamageInfo Info(Weapon->Damage, Weapon->DamageCategory);
	Info.InstigatorPlayerState = GetOwningPlayerState();
	Info.DamageCauser = Kart;
	Info.HitLocation = Kart->GetActorLocation();
	Info.HitNormal = FVector::UpVector;
	Info.KnockbackForce = Weapon->Knockback;
	Info.WeaponId = Weapon->Id;
	Info.DamageTags = Weapon->DamageTags;

	TArray<AActor*> Ignored;
	if (!Weapon->bCanDamageInstigator)
	{
		Ignored.Add(Kart);
	}

	DamageSubsystem->ApplyRadialDamage(
		Kart->GetActorLocation(),
		Weapon->AreaEffectRadius,
		Weapon->MinimumFalloffDamageFraction,
		Info,
		Ignored);
}
