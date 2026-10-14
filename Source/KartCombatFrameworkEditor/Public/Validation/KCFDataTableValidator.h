// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "KCFDataTableValidator.generated.h"

/**
 * Validates catalogue Data Tables built on FKCFCatalogueRowBase.
 *
 * A catalogue row and the Data Asset it points at are two records of the same thing, and the one
 * rule that keeps them from drifting into two sources of truth is that the row's Id must match the
 * asset's Id. Nothing at runtime can enforce that: a mismatched row simply makes the entry
 * unreachable, and the only symptom is a kart or weapon that never appears in the game. So it is
 * enforced here, at save time.
 *
 * The checks are deliberately read-only against the asset registry. Loading every referenced Data
 * Asset to compare identifiers would turn saving one table into loading the project's entire
 * content catalogue.
 */
UCLASS()
class UKCFDataTableValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	UKCFDataTableValidator();

protected:
	//~ Begin UEditorValidatorBase
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
	//~ End UEditorValidatorBase
};
