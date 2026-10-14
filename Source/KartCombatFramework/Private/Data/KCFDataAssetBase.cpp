// Original work, MIT licensed.

#include "Data/KCFDataAssetBase.h"

#include "Core/KCFLog.h"

#define LOCTEXT_NAMESPACE "KCFDataAsset"

bool UKCFDataAssetBase::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();

	if (Id.IsNone())
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("MissingId", "'{0}': Id is None. Every KCF data asset needs a unique, stable Id; it is what gets replicated and saved."),
			FText::FromString(GetName())));
	}

	if (DisplayName.IsEmpty())
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("MissingDisplayName", "'{0}': DisplayName is empty and will show as a blank entry in any selection UI."),
			FText::FromString(GetName())));
	}

	return OutErrors.Num() == ErrorsBefore;
}

bool UKCFDataAssetBase::K2_ValidateConfiguration(TArray<FText>& OutErrors) const
{
	return ValidateConfiguration(OutErrors);
}

FPrimaryAssetId UKCFDataAssetBase::GetPrimaryAssetId() const
{
	const FName AssetType = GetPrimaryAssetType();
	if (AssetType.IsNone())
	{
		return Super::GetPrimaryAssetId();
	}

	// Key the primary asset on the authored Id rather than the package name so that renaming the
	// asset file does not invalidate saved references.
	return FPrimaryAssetId(AssetType, Id.IsNone() ? GetFName() : Id);
}

void UKCFDataAssetBase::ValidatePositive(float Value, const TCHAR* PropertyName, TArray<FText>& OutErrors)
{
	if (!(Value > 0.f) || !FMath::IsFinite(Value))
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("NotPositive", "'{0}' must be greater than zero (currently {1})."),
			FText::FromString(PropertyName),
			FText::AsNumber(Value)));
	}
}

void UKCFDataAssetBase::ValidateSoftRef(const FSoftObjectPath& Path, const TCHAR* PropertyName, bool bRequired, TArray<FText>& OutErrors)
{
	// Only null-ness is checked here. Whether a non-null path actually resolves to an existing asset
	// needs the Asset Registry, which the runtime module deliberately does not depend on; that check
	// lives in UKCFDataAssetValidator in the editor module.
	if (Path.IsNull() && bRequired)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("MissingRequiredRef", "'{0}' is required but unset."),
			FText::FromString(PropertyName)));
	}
}

#undef LOCTEXT_NAMESPACE
