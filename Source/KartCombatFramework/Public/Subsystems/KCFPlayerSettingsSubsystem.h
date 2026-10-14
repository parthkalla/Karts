// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "KCFPlayerSettingsSubsystem.generated.h"

class UKCFMobileSettingsDataAsset;
class UKCFSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFSettingsChangedSignature);

/**
 * Loads, applies and saves one local player's preferences.
 *
 * A local player subsystem rather than a game instance one because settings are per-player: split
 * screen is rare in a kart game but two players on one device must not share a steering deadzone.
 *
 * The subsystem owns a runtime UKCFMobileSettingsDataAsset built from the saved values. That
 * instance is what the input component reads, which means a settings change takes effect the moment
 * it is applied rather than on the next match.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFPlayerSettingsSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	UFUNCTION(BlueprintPure, Category = "KCF|Settings", meta = (WorldContext = "WorldContextObject"))
	static UKCFPlayerSettingsSubsystem* Get(const UObject* WorldContextObject);

	/** Raised whenever settings are applied, so UI and the input layer can refresh. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Settings")
	FKCFSettingsChangedSignature OnSettingsChanged;

	/** The saved preferences. Never null after Initialize. */
	UFUNCTION(BlueprintPure, Category = "KCF|Settings")
	UKCFSaveGame* GetSettings() const { return Settings; }

	/**
	 * The live mobile tuning built from the saved preferences.
	 * Handed to UKCFKartInputComponent so that the player's own values drive the feel.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Settings")
	UKCFMobileSettingsDataAsset* GetRuntimeMobileSettings() const { return RuntimeMobileSettings; }

	/** Writes the save to disk and raises OnSettingsChanged. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Settings")
	bool SaveSettings();

	/** Re-reads the save from disk, discarding unsaved changes. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Settings")
	void ReloadSettings();

	/** Restores defaults and saves. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Settings")
	void ResetToDefaults();

	/**
	 * Pushes the current values into the live mobile settings and any possessed kart's input.
	 * Call after editing GetSettings() directly from a settings menu.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Settings")
	void ApplySettings();

	/** Slot name used for the save file. */
	static const TCHAR* SaveSlotName;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UKCFSaveGame> Settings;

	/**
	 * A runtime instance rather than a reference to the project's authored asset: writing the
	 * player's sensitivity into a shared content asset would modify the asset on disk in the editor
	 * and would be shared between split-screen players at runtime.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UKCFMobileSettingsDataAsset> RuntimeMobileSettings;

	/** Creates RuntimeMobileSettings, seeded from the project default asset when one is set. */
	void BuildRuntimeMobileSettings();

	/** Copies saved preferences onto RuntimeMobileSettings. */
	void CopySettingsToRuntime();

	/** Finds the local player's kart input component and hands it the runtime settings. */
	void PushSettingsToInput();
};
