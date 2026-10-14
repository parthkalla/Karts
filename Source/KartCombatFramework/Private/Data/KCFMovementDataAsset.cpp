// Original work, MIT licensed.

#include "Data/KCFMovementDataAsset.h"

#include "Curves/CurveFloat.h"

#define LOCTEXT_NAMESPACE "KCFMovementDataAsset"

namespace
{
	/**
	 * Evaluates a runtime curve, falling back to a default when the curve has no keys.
	 * An unauthored curve evaluating to 0 would make a kart undrivable, which is a far more
	 * confusing failure than simply behaving as if the curve were flat.
	 */
	float EvaluateCurveOrDefault(const FRuntimeFloatCurve& Curve, float Time, float DefaultValue)
	{
		const FRichCurve* RichCurve = Curve.GetRichCurveConst();
		if (!RichCurve || RichCurve->GetNumKeys() == 0)
		{
			return DefaultValue;
		}
		return RichCurve->Eval(Time, DefaultValue);
	}
}

UKCFMovementDataAsset::UKCFMovementDataAsset()
{
	Id = TEXT("DefaultHandling");
}

float UKCFMovementDataAsset::GetAccelerationMultiplier(float SpeedPercent) const
{
	return FMath::Max(0.f, EvaluateCurveOrDefault(AccelerationBySpeedCurve, FMath::Clamp(SpeedPercent, 0.f, 1.f), 1.f));
}

float UKCFMovementDataAsset::GetTurnRateMultiplier(float SpeedPercent) const
{
	return FMath::Max(0.f, EvaluateCurveOrDefault(TurnRateBySpeedCurve, FMath::Clamp(SpeedPercent, 0.f, 1.f), 1.f));
}

bool UKCFMovementDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	ValidatePositive(MaxSpeed, TEXT("MaxSpeed"), OutErrors);
	ValidatePositive(Acceleration, TEXT("Acceleration"), OutErrors);
	ValidatePositive(TurnRate, TEXT("TurnRate"), OutErrors);
	ValidatePositive(BrakingDeceleration, TEXT("BrakingDeceleration"), OutErrors);

	if (DriftGrip > Grip)
	{
		OutErrors.Add(LOCTEXT("DriftGripTooHigh",
			"DriftGrip is higher than Grip, so drifting will make the kart stick more rather than slide. This is almost certainly inverted."));
	}

	if (MaxReverseSpeed > MaxSpeed)
	{
		OutErrors.Add(LOCTEXT("ReverseFasterThanForward",
			"MaxReverseSpeed exceeds MaxSpeed. Reverse is faster than forward; check these values."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
