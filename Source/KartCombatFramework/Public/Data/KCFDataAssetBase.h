// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "KCFDataAssetBase.generated.h"

/**
 * Shared base for every framework Data Asset.
 *
 * Gives all definitions a stable identifier, a tag container, and a validation entry point. The
 * identifier is what the rest of the framework replicates and persists: replicating an FName costs
 * a couple of bytes against a name-table index, whereas replicating an object pointer forces the
 * asset to be loaded and net-relevant on every client.
 */
UCLASS(Abstract, BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFDataAssetBase : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Stable identifier. Must be unique within its asset type and must not change once content
	 * referencing it exists, because saves and replication both key off this value.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", AssetRegistrySearchable)
	FName Id;

	/** Player-facing name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** Player-facing description, for UI tooltips and selection screens. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	/** UI icon. Soft so that a catalogue of 200 items does not load 200 textures. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Free-form classification. Used for filtering, immunity checks and project-specific rules. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTagContainer Tags;

	/**
	 * Appends a human-readable message for every configuration problem found.
	 * Called by the editor validator on save and by the runtime once per asset in non-shipping
	 * builds, so a broken asset is reported at the point it is first used rather than by a
	 * confusing downstream symptom.
	 * @return true when the asset is usable.
	 */
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const;

	/** Blueprint-facing wrapper around ValidateConfiguration. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Data", meta = (DisplayName = "Validate Configuration"))
	bool K2_ValidateConfiguration(TArray<FText>& OutErrors) const;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

protected:
	/** Override to declare the Asset Manager primary type for this asset family. */
	virtual FName GetPrimaryAssetType() const PURE_VIRTUAL(UKCFDataAssetBase::GetPrimaryAssetType, return NAME_None;);

	/** Helper used by subclasses: records an error when Value is not strictly positive. */
	static void ValidatePositive(float Value, const TCHAR* PropertyName, TArray<FText>& OutErrors);

	/** Helper used by subclasses: records an error when the soft reference is set but unresolvable. */
	static void ValidateSoftRef(const FSoftObjectPath& Path, const TCHAR* PropertyName, bool bRequired, TArray<FText>& OutErrors);
};
