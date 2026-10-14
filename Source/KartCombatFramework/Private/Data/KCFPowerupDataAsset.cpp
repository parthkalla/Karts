// Original work, MIT licensed.

#include "Data/KCFPowerupDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFPowerupDataAsset"

bool UKCFPowerupDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	if (Type == EKCFPowerupType::Weapon)
	{
		ValidateSoftRef(GrantedWeapon.ToSoftObjectPath(), TEXT("GrantedWeapon"), /*bRequired=*/true, OutErrors);

		if (!EffectClass.IsNull())
		{
			OutErrors.Add(LOCTEXT("WeaponWithEffect",
				"Type is Weapon but EffectClass is also set. Only GrantedWeapon is used; the effect will never run."));
		}
	}
	else
	{
		ValidateSoftRef(EffectClass.ToSoftObjectPath(), TEXT("EffectClass"), /*bRequired=*/true, OutErrors);
	}

	const bool bIsTimed = Type == EKCFPowerupType::Timed
		|| Type == EKCFPowerupType::Defensive
		|| Type == EKCFPowerupType::Offensive;

	if (bIsTimed && Duration <= 0.f)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("TimedZeroDuration", "'{0}': a timed powerup needs a Duration greater than zero, otherwise it is applied and reverted in the same frame."),
			FText::FromString(GetName())));
	}

	if (bStackable && MaxStacks <= 1)
	{
		OutErrors.Add(LOCTEXT("StackableOneStack",
			"bStackable is true but MaxStacks is 1, which behaves identically to a non-stackable powerup."));
	}

	if (Type == EKCFPowerupType::Weapon && bAutoActivateOnPickup)
	{
		OutErrors.Add(LOCTEXT("AutoActivateWeapon",
			"bAutoActivateOnPickup has no meaning for a Weapon powerup: the weapon is placed into the inventory and fired by the player."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
