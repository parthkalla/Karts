// Original work, MIT licensed.

#include "Data/KCFArenaDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFArenaDataAsset"

bool UKCFArenaDataAsset::IsInsidePlayspace(const FVector& WorldLocation) const
{
	if (WorldLocation.Z < KillZ)
	{
		return false;
	}

	const FVector Local = WorldLocation - PlayspaceOrigin;

	// A zero extent on an axis means "unbounded on this axis" rather than "zero-width volume",
	// which lets a designer bound only the horizontal plane of an open-sky arena.
	auto WithinAxis = [](double Value, double Extent)
	{
		return Extent <= 0.0 || FMath::Abs(Value) <= Extent;
	};

	return WithinAxis(Local.X, PlayspaceExtent.X)
		&& WithinAxis(Local.Y, PlayspaceExtent.Y)
		&& WithinAxis(Local.Z, PlayspaceExtent.Z);
}

bool UKCFArenaDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	ValidateSoftRef(Map.ToSoftObjectPath(), TEXT("Map"), /*bRequired=*/true, OutErrors);

	if (RecommendedMinPlayers > RecommendedMaxPlayers)
	{
		OutErrors.Add(LOCTEXT("RecommendedRangeInverted",
			"RecommendedMinPlayers exceeds RecommendedMaxPlayers, so this arena will never pass a player-count filter."));
	}

	if (PlayspaceExtent.Z > 0.f && KillZ > PlayspaceOrigin.Z - PlayspaceExtent.Z)
	{
		OutErrors.Add(LOCTEXT("KillZInsidePlayspace",
			"KillZ sits above the bottom of the playspace, so karts are destroyed while still inside the authored bounds."));
	}

	if (OutOfBoundsDamagePerSecond <= 0.f && PlayspaceExtent.IsNearlyZero())
	{
		OutErrors.Add(LOCTEXT("NoBoundsEnforcement",
			"The playspace is unbounded and out-of-bounds damage is disabled, so KillZ is the only thing stopping a kart from driving away forever."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
