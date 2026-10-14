// Original work, MIT licensed.

#include "Validation/KCFDataAssetValidator.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/KCFDataAssetBase.h"
#include "Misc/DataValidation.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "KCFDataAssetValidator"

namespace KCFValidation
{
	/**
	 * Finds other assets of the same class that already use this identifier.
	 *
	 * Reads the Id from the asset registry tag rather than loading each candidate: a project with a
	 * few hundred definitions would otherwise pull all of them into memory every time one is saved.
	 * UKCFDataAssetBase::Id is marked AssetRegistrySearchable for exactly this.
	 */
	static TArray<FString> FindDuplicateIdAssets(const UKCFDataAssetBase& Asset)
	{
		TArray<FString> Duplicates;

		const IAssetRegistry* Registry = IAssetRegistry::Get();
		if (!Registry || Asset.Id.IsNone())
		{
			return Duplicates;
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(Asset.GetClass()->GetClassPathName());
		Filter.bRecursiveClasses = false;

		TArray<FAssetData> Candidates;
		Registry->GetAssets(Filter, Candidates);

		const FSoftObjectPath SelfPath(&Asset);
		const FString ExpectedId = Asset.Id.ToString();

		for (const FAssetData& Candidate : Candidates)
		{
			if (Candidate.GetSoftObjectPath() == SelfPath)
			{
				continue;
			}

			FString CandidateId;
			if (Candidate.GetTagValue(GET_MEMBER_NAME_CHECKED(UKCFDataAssetBase, Id), CandidateId)
				&& CandidateId == ExpectedId)
			{
				Duplicates.Add(Candidate.GetSoftObjectPath().ToString());
			}
		}

		return Duplicates;
	}
}

UKCFDataAssetValidator::UKCFDataAssetValidator()
{
	bIsEnabled = true;
}

bool UKCFDataAssetValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset && InAsset->IsA<UKCFDataAssetBase>();
}

EDataValidationResult UKCFDataAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UKCFDataAssetBase* Asset = Cast<UKCFDataAssetBase>(InAsset);
	if (!Asset)
	{
		return EDataValidationResult::NotValidated;
	}

	bool bFailed = false;

	TArray<FText> RuntimeErrors;
	if (!Asset->ValidateConfiguration(RuntimeErrors))
	{
		for (const FText& Error : RuntimeErrors)
		{
			AssetFails(InAsset, Error);
		}
		bFailed = true;
	}

	if (Asset->Id.IsNone())
	{
		// Not cosmetic: the identifier is what inventory slots, the kill feed, save data and the
		// content registry all key off. An asset without one can be referenced directly but can
		// never be looked up, granted or dropped.
		AssetFails(InAsset, LOCTEXT("MissingId", "Id is empty. Every framework definition needs a stable identifier; it is what replication, save data and the content registry use."));
		bFailed = true;
	}
	else
	{
		const TArray<FString> Duplicates = KCFValidation::FindDuplicateIdAssets(*Asset);
		if (Duplicates.Num() > 0)
		{
			AssetFails(InAsset, FText::Format(
				LOCTEXT("DuplicateIdFmt", "Id '{0}' is already used by {1}. Lookups by this id resolve to whichever asset the registry indexed first, so one of the two will silently never be found."),
				FText::FromName(Asset->Id),
				FText::FromString(FString::Join(Duplicates, TEXT(", ")))));
			bFailed = true;
		}
	}

	if (Asset->DisplayName.IsEmpty())
	{
		AssetWarning(InAsset, LOCTEXT("MissingDisplayName", "DisplayName is empty. UI that lists this definition will show a blank entry."));
	}

	if (!bFailed)
	{
		AssetPasses(InAsset);
	}

	return bFailed ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
