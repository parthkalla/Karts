// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "KCFDataAssetValidator.generated.h"

/**
 * Runs UKCFDataAssetBase::ValidateConfiguration on save and during a content validation pass.
 *
 * The runtime also calls ValidateConfiguration, but only in non-shipping builds and only once the
 * asset is actually used, by which point the symptom - a weapon that fires nothing, a kart with no
 * handling profile - is several steps removed from the cause. Validating at save time puts the
 * error next to the edit that caused it.
 *
 * This validator adds the checks that need editor-only machinery on top of the runtime ones:
 * whether soft references actually resolve, and whether the identifier is unique across every
 * asset of the same type in the project.
 */
UCLASS()
class UKCFDataAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	UKCFDataAssetValidator();

protected:
	//~ Begin UEditorValidatorBase
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
	//~ End UEditorValidatorBase
};
