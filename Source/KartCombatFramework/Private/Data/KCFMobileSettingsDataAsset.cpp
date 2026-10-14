// Original work, MIT licensed.

#include "Data/KCFMobileSettingsDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFMobileSettingsDataAsset"

float UKCFMobileSettingsDataAsset::ProcessSteeringAxis(float RawAxis) const
{
	if (!FMath::IsFinite(RawAxis))
	{
		return 0.f;
	}

	const float Clamped = FMath::Clamp(RawAxis, -1.f, 1.f);
	const float Magnitude = FMath::Abs(Clamped);

	if (Magnitude <= SteeringDeadzone)
	{
		return 0.f;
	}

	// Rescale the live range back to 0..1 after removing the deadzone, so that the first pixel of
	// movement past the deadzone produces a tiny steering value rather than an immediate step.
	const float DeadzoneRange = FMath::Max(KINDA_SMALL_NUMBER, 1.f - SteeringDeadzone);
	const float Rescaled = (Magnitude - SteeringDeadzone) / DeadzoneRange;

	const float Curved = FMath::Pow(Rescaled, SteeringExponent);
	const float Result = Curved * SteeringSensitivity * FMath::Sign(Clamped);

	return FMath::Clamp(Result, -1.f, 1.f);
}

bool UKCFMobileSettingsDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	if (SteeringDeadzone >= 0.5f)
	{
		OutErrors.Add(LOCTEXT("HugeDeadzone",
			"SteeringDeadzone is at or above 0.5, so half of the control's travel does nothing."));
	}

	if (bAutoAccelerate && AutoAccelerateThrottle <= 0.f)
	{
		OutErrors.Add(LOCTEXT("AutoAccelZero",
			"bAutoAccelerate is enabled with an AutoAccelerateThrottle of zero, so the kart will never move on its own."));
	}

	if (SteeringMode == EKCFSteeringMode::Tilt && TiltFullLockDegrees < 5.f)
	{
		OutErrors.Add(LOCTEXT("TwitchyTilt",
			"TiltFullLockDegrees is below 5, which makes tilt steering reach full lock on almost any hand movement."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
