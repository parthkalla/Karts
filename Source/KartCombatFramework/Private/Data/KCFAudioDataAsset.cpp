// Original work, MIT licensed.

#include "Data/KCFAudioDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFAudioDataAsset"

bool UKCFAudioDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	if (EngineMaxPitch < EngineIdlePitch)
	{
		OutErrors.Add(LOCTEXT("InvertedEnginePitch",
			"EngineMaxPitch is below EngineIdlePitch, so the engine note drops as the kart accelerates."));
	}

	// Every reference here is optional by design: a kart with no audio is silent, not broken.
	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
