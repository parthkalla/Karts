// Original work, MIT licensed.

#include "Validation/KCFDataTableValidator.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Data/KCFDataAssetBase.h"
#include "Data/KCFDataTableRows.h"
#include "Engine/DataTable.h"
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "KCFDataTableValidator"

namespace KCFTableValidation
{
	/**
	 * The soft pointer a catalogue row uses to name its definition.
	 *
	 * Found by reflection rather than by switching on the five row types, so a project that adds a
	 * sixth row struct deriving from FKCFCatalogueRowBase is validated too without touching this
	 * file. The row structs each carry exactly one soft object reference, which is what makes the
	 * "first one found" rule unambiguous.
	 */
	static const FSoftObjectProperty* FindDefinitionProperty(const UScriptStruct& RowStruct)
	{
		for (TFieldIterator<FProperty> It(&RowStruct); It; ++It)
		{
			if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(*It))
			{
				return SoftProperty;
			}
		}

		return nullptr;
	}

	/** Reads the Id tag of a referenced asset without loading it. Returns false when unresolvable. */
	static bool ReadDefinitionId(const FSoftObjectPath& Path, FString& OutId, bool& bOutAssetExists)
	{
		bOutAssetExists = false;
		OutId.Reset();

		const IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry || Path.IsNull())
		{
			return false;
		}

		const FAssetData AssetData = Registry->GetAssetByObjectPath(Path);
		if (!AssetData.IsValid())
		{
			return false;
		}

		bOutAssetExists = true;
		return AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UKCFDataAssetBase, Id), OutId);
	}
}

UKCFDataTableValidator::UKCFDataTableValidator()
{
	bIsEnabled = true;
}

bool UKCFDataTableValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	const UDataTable* Table = Cast<UDataTable>(InAsset);
	const UScriptStruct* RowStruct = Table ? Table->GetRowStruct() : nullptr;

	return RowStruct && RowStruct->IsChildOf(FKCFCatalogueRowBase::StaticStruct());
}

EDataValidationResult UKCFDataTableValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UDataTable* Table = Cast<UDataTable>(InAsset);
	const UScriptStruct* RowStruct = Table ? Table->GetRowStruct() : nullptr;
	if (!RowStruct)
	{
		return EDataValidationResult::NotValidated;
	}

	const FSoftObjectProperty* DefinitionProperty = KCFTableValidation::FindDefinitionProperty(*RowStruct);
	bool bFailed = false;

	TMap<FName, FName> SeenIds;

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FName RowName = Pair.Key;
		const FKCFCatalogueRowBase* Row = reinterpret_cast<const FKCFCatalogueRowBase*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		if (Row->Id.IsNone())
		{
			AssetFails(InAsset, FText::Format(
				LOCTEXT("RowMissingIdFmt", "Row '{0}' has an empty Id. Lookups go through the Id, not the row name, so this row can never be selected."),
				FText::FromName(RowName)));
			bFailed = true;
			continue;
		}

		if (const FName* ExistingRow = SeenIds.Find(Row->Id))
		{
			AssetFails(InAsset, FText::Format(
				LOCTEXT("DuplicateRowIdFmt", "Rows '{0}' and '{1}' both declare Id '{2}'. Whichever is scanned first wins and the other is unreachable."),
				FText::FromName(*ExistingRow),
				FText::FromName(RowName),
				FText::FromName(Row->Id)));
			bFailed = true;
		}
		else
		{
			SeenIds.Add(Row->Id, RowName);
		}

		if (!DefinitionProperty)
		{
			continue;
		}

		const FSoftObjectPtr* SoftPointer = DefinitionProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(Pair.Value);
		const FSoftObjectPath DefinitionPath = SoftPointer ? SoftPointer->ToSoftObjectPath() : FSoftObjectPath();

		if (DefinitionPath.IsNull())
		{
			// A row with no definition is only a problem for an entry that is actually offered, so
			// a disabled row is a warning rather than a failure: shipping content disabled ahead of
			// a release is a supported workflow.
			const FText Message = FText::Format(
				LOCTEXT("RowMissingDefinitionFmt", "Row '{0}' has no Data Asset reference. The catalogue entry exists but there is nothing behind it."),
				FText::FromName(RowName));

			if (Row->bEnabled)
			{
				AssetFails(InAsset, Message);
				bFailed = true;
			}
			else
			{
				AssetWarning(InAsset, Message);
			}
			continue;
		}

		FString DefinitionId;
		bool bAssetExists = false;
		const bool bReadId = KCFTableValidation::ReadDefinitionId(DefinitionPath, DefinitionId, bAssetExists);

		if (!bAssetExists)
		{
			AssetFails(InAsset, FText::Format(
				LOCTEXT("RowUnresolvableFmt", "Row '{0}' references '{1}', which does not exist. The reference will resolve to null at runtime."),
				FText::FromName(RowName),
				FText::FromString(DefinitionPath.ToString())));
			bFailed = true;
			continue;
		}

		if (bReadId && DefinitionId != Row->Id.ToString())
		{
			AssetFails(InAsset, FText::Format(
				LOCTEXT("RowIdMismatchFmt", "Row '{0}' declares Id '{1}' but the asset it points at has Id '{2}'. The two must match: gameplay resolves the asset's Id, the front end resolves the row's, and a mismatch makes this entry unreachable from one side or the other."),
				FText::FromName(RowName),
				FText::FromName(Row->Id),
				FText::FromString(DefinitionId)));
			bFailed = true;
		}
	}

	if (!bFailed)
	{
		AssetPasses(InAsset);
	}

	return bFailed ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
