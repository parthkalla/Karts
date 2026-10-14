// Original work, MIT licensed.

#include "Data/KCFWeaponDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFWeaponDataAsset"

bool UKCFWeaponDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	// Each delivery mode has exactly one reference it cannot work without. Catching this here turns
	// a silent "firing does nothing" bug into an asset-save error.
	switch (Delivery)
	{
	case EKCFWeaponDelivery::Projectile:
		ValidateSoftRef(ProjectileClass.ToSoftObjectPath(), TEXT("ProjectileClass"), /*bRequired=*/true, OutErrors);
		ValidatePositive(ProjectileSpeed, TEXT("ProjectileSpeed"), OutErrors);
		break;

	case EKCFWeaponDelivery::Deployable:
		ValidateSoftRef(DeployableClass.ToSoftObjectPath(), TEXT("DeployableClass"), /*bRequired=*/true, OutErrors);
		break;

	case EKCFWeaponDelivery::Self:
		ValidateSoftRef(SelfEffectClass.ToSoftObjectPath(), TEXT("SelfEffectClass"), /*bRequired=*/true, OutErrors);
		break;

	case EKCFWeaponDelivery::Hitscan:
		ValidatePositive(HitscanRange, TEXT("HitscanRange"), OutErrors);
		break;

	case EKCFWeaponDelivery::AreaEffect:
		ValidatePositive(AreaEffectRadius, TEXT("AreaEffectRadius"), OutErrors);
		break;

	default:
		break;
	}

	if (Ammo > MaxAmmo)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("AmmoAboveMax", "'{0}': Ammo ({1}) exceeds MaxAmmo ({2}); the weapon would be clamped down the moment it is granted."),
			FText::FromString(GetName()),
			FText::AsNumber(Ammo),
			FText::AsNumber(MaxAmmo)));
	}

	if (Delivery != EKCFWeaponDelivery::Self && Damage <= 0.f && ExplosionRadius <= 0.f)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("HarmlessWeapon", "'{0}': Damage and ExplosionRadius are both zero, so this weapon cannot affect anything."),
			FText::FromString(GetName())));
	}

	if (ProjectilesPerShot > 1 && SpreadAngle <= 0.f)
	{
		OutErrors.Add(LOCTEXT("StackedProjectiles",
			"ProjectilesPerShot is greater than 1 with a SpreadAngle of 0, so every projectile is spawned on the same trajectory."));
	}

	if (bHoming && Delivery != EKCFWeaponDelivery::Projectile)
	{
		OutErrors.Add(LOCTEXT("HomingNonProjectile",
			"bHoming is set on a weapon that does not use Projectile delivery. The homing settings will be ignored."));
	}

	if (!bRemoveWhenEmpty && Ammo < MaxAmmo && FireInterval <= 0.f)
	{
		OutErrors.Add(LOCTEXT("InfiniteFireRate",
			"FireInterval is zero on a weapon that is never removed, so it can be fired as fast as a client can send RPCs. The server will still rate-limit, but set a real interval."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
