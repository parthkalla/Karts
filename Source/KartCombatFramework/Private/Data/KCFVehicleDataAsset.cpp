// Original work, MIT licensed.

#include "Data/KCFVehicleDataAsset.h"

#include "Data/KCFMovementDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFVehicleDataAsset"

UKCFVehicleDataAsset::UKCFVehicleDataAsset()
{
	// Explosions hurt more than scrapes by default; a designer can override either entry.
	DamageMultipliers.Add(EKCFDamageCategory::Explosion, 1.0f);
	DamageMultipliers.Add(EKCFDamageCategory::Collision, 0.5f);
}

float UKCFVehicleDataAsset::GetDamageMultiplier(EKCFDamageCategory Category) const
{
	if (const float* Found = DamageMultipliers.Find(Category))
	{
		return FMath::Max(0.f, *Found);
	}
	return 1.f;
}

bool UKCFVehicleDataAsset::HasVisualMesh() const
{
	return !VehicleMesh.IsNull() || !StaticVehicleMesh.IsNull();
}

bool UKCFVehicleDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	if (!Movement)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("MissingMovement", "'{0}': Movement is unset. A kart with no handling profile cannot be driven."),
			FText::FromString(GetName())));
	}
	else
	{
		Movement->ValidateConfiguration(OutErrors);
	}

	ValidatePositive(MaxHealth, TEXT("MaxHealth"), OutErrors);
	ValidatePositive(Mass, TEXT("Mass"), OutErrors);

	if (!HasVisualMesh())
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("NoMesh", "'{0}': neither VehicleMesh nor StaticVehicleMesh is set. The kart will be invisible."),
			FText::FromString(GetName())));
	}

	if (VehicleMesh.IsNull() == false && StaticVehicleMesh.IsNull() == false)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("BothMeshes", "'{0}': both VehicleMesh and StaticVehicleMesh are set. The skeletal mesh wins and the static mesh is ignored."),
			FText::FromString(GetName())));
	}

	if (MaxShield > 0.f && ShieldRegenPerSecond > 0.f && ShieldRegenDelay <= 0.f)
	{
		OutErrors.Add(LOCTEXT("InstantShieldRegen",
			"ShieldRegenDelay is zero with regeneration enabled, so the shield refills the instant damage stops. This is rarely intended."));
	}

	if (MaxSpeedFOV < BaseFOV)
	{
		OutErrors.Add(LOCTEXT("InvertedFOV",
			"MaxSpeedFOV is lower than BaseFOV, so the view narrows as the kart speeds up. This inverts the usual sensation of speed."));
	}

	if (BoostCharges > 0 && BoostCooldown <= 0.f)
	{
		OutErrors.Add(LOCTEXT("FreeBoost",
			"BoostCooldown is zero, so boost charges refill instantly and boost is effectively unlimited."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
