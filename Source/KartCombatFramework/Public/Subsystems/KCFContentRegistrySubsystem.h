// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KCFContentRegistrySubsystem.generated.h"

class UDataTable;
class UKCFArenaDataAsset;
class UKCFGameModeDataAsset;
class UKCFPowerupDataAsset;
class UKCFVehicleDataAsset;
class UKCFWeaponDataAsset;

/**
 * Turns the FName ids that travel over the network back into definition assets.
 *
 * Inventory slots, damage records, kill-feed entries and client requests all carry ids rather than
 * object pointers, because an FName is a few bytes and does not drag an asset into memory on every
 * machine that can see it. Something still has to perform the lookup on the machine that needs the
 * actual numbers, and that is this registry.
 *
 * It is a game instance subsystem so the tables survive travel between maps - resolving a weapon id
 * must not depend on which arena happens to be loaded.
 *
 * Resolution is deliberately lenient: an unknown id returns null and logs once, rather than
 * asserting. A missing weapon should cost one shot, not the match.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFContentRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Convenience accessor. Returns null outside a game instance. */
	UFUNCTION(BlueprintPure, Category = "KCF|Content", meta = (WorldContext = "WorldContextObject"))
	static UKCFContentRegistrySubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	UKCFWeaponDataAsset* FindWeapon(FName WeaponId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	UKCFPowerupDataAsset* FindPowerup(FName PowerupId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	UKCFVehicleDataAsset* FindVehicle(FName VehicleId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	UKCFGameModeDataAsset* FindGameModeConfig(FName GameModeId);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	UKCFArenaDataAsset* FindArena(FName ArenaId);

	/**
	 * Registers a definition that is not in any catalogue table.
	 *
	 * The escape hatch for projects that build their content list some other way - a downloaded
	 * manifest, a plugin that ships its own weapons, a test fixture. Entries registered here win
	 * over table entries with the same id.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	void RegisterWeapon(UKCFWeaponDataAsset* Weapon);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	void RegisterPowerup(UKCFPowerupDataAsset* Powerup);

	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	void RegisterVehicle(UKCFVehicleDataAsset* Vehicle);

	/** Re-reads the catalogue tables. Useful after a hot content update in the editor. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Content")
	void RefreshCatalogues();

	/** Every weapon id the registry can resolve, including runtime registrations. */
	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	TArray<FName> GetAllWeaponIds() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	TArray<FName> GetAllPowerupIds() const;

	/** The loaded vehicle catalogue table, or null when none is configured. */
	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	UDataTable* GetVehicleCatalogue() const { return VehicleCatalogue; }

	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	UDataTable* GetWeaponCatalogue() const { return WeaponCatalogue; }

	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	UDataTable* GetPowerupCatalogue() const { return PowerupCatalogue; }

	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	UDataTable* GetGameModeCatalogue() const { return GameModeCatalogue; }

	UFUNCTION(BlueprintPure, Category = "KCF|Content")
	UDataTable* GetArenaCatalogue() const { return ArenaCatalogue; }

protected:
	UPROPERTY(Transient) TObjectPtr<UDataTable> VehicleCatalogue;
	UPROPERTY(Transient) TObjectPtr<UDataTable> WeaponCatalogue;
	UPROPERTY(Transient) TObjectPtr<UDataTable> PowerupCatalogue;
	UPROPERTY(Transient) TObjectPtr<UDataTable> GameModeCatalogue;
	UPROPERTY(Transient) TObjectPtr<UDataTable> ArenaCatalogue;

	/**
	 * Resolved definitions, kept alive by being UPROPERTYs.
	 *
	 * Populated lazily on first lookup rather than eagerly at startup: a project with two hundred
	 * karts should not load all two hundred meshes because one of them is in a catalogue. The cost
	 * of the first resolve is a synchronous load of a single asset.
	 */
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UKCFWeaponDataAsset>> ResolvedWeapons;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UKCFPowerupDataAsset>> ResolvedPowerups;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UKCFVehicleDataAsset>> ResolvedVehicles;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UKCFGameModeDataAsset>> ResolvedGameModes;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UKCFArenaDataAsset>> ResolvedArenas;

	/** Id -> unloaded asset path, built from the tables at startup. Cheap: no assets are loaded. */
	TMap<FName, FSoftObjectPath> WeaponPaths;
	TMap<FName, FSoftObjectPath> PowerupPaths;
	TMap<FName, FSoftObjectPath> VehiclePaths;
	TMap<FName, FSoftObjectPath> GameModePaths;
	TMap<FName, FSoftObjectPath> ArenaPaths;

	/** Ids already reported as missing, so a weapon fired every frame logs once rather than always. */
	TSet<FName> ReportedMissingIds;

	void BuildIndices();
	void ReportMissing(FName Id, const TCHAR* Kind);
};
