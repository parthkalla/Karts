// Original work, MIT licensed.

#include "Input/KCFInputConfigDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFInputConfig"

bool UKCFInputConfigDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	// A configuration with a mapping context but no driving action is almost certainly a mistake:
	// the context will be installed and the kart will sit still.
	const bool bHasAnyDriving = !MoveAction.IsNull() || !SteerAction.IsNull() || !ThrottleAction.IsNull();
	if (!MappingContext.IsNull() && !bHasAnyDriving)
	{
		OutErrors.Add(LOCTEXT("NoDrivingAction",
			"A mapping context is set but no Move, Steer or Throttle action is bound. Keyboard and gamepad players will not be able to drive."));
	}

	// Conversely, actions with no context will never fire, because nothing maps a key to them.
	if (MappingContext.IsNull() && bHasAnyDriving)
	{
		OutErrors.Add(LOCTEXT("NoMappingContext",
			"Driving actions are set but no Input Mapping Context is assigned, so no hardware input will reach them."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
