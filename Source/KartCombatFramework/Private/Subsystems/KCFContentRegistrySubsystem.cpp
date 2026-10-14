// Original work, MIT licensed.

#include "Subsystems/KCFContentRegistrySubsystem.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFArenaDataAsset.h"
#include "Data/KCFDataTableRows.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Data/KCFPowerupDataAsset.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

namespace KCFContentRegistry
{
	/** Resolves a soft table reference, loading it synchronously. Tables are small. */
	static UDataTable* LoadTable(const TSoftObjectPtr<UDataTable>& SoftTable)
	{
		return SoftTable.IsNull() ? nullptr : SoftTable.LoadSynchronous();
	}

	/** Shared index build: one entry per enabled row, keyed on the row's declared id. */
	template <typename RowType, typename AssetType>
	static void IndexTable(const UDataTable* Table, TSoftObjectPtr<AssetType> RowType::*AssetMember, TMap<FName, FSoftObjectPath>& OutPaths)
	{
		OutPaths.Reset();
		if (!Table)
		{
			return;
		}

		Table->ForeachRow<RowType>(TEXT("KCFContentRegistry::IndexTable"),
			[AssetMember, &OutPaths](const FName& RowName, const RowType& Row)
			{
				const TSoftObjectPtr<AssetType>& Asset = Row.*AssetMember;
				if (Asset.IsNull())
				{
					return;
				}

				// The Id column is authoritative so that a designer can rename a row without
				// breaking save data or inventories. Falling back to the row name keeps tables
				// that were imported from CSV without an explicit Id column working.
				const FName Key = Row.Id.IsNone() ? RowName : Row.Id;
				OutPaths.Add(Key, Asset.ToSoftObjectPath());
			});
	}

	/** Lazily resolves an id through a cache and a path index. */
	template <typename AssetType>
	static AssetType* ResolveCached(FName Id, TMap<FName, TObjectPtr<AssetType>>& Cache, const TMap<FName, FSoftObjectPath>& Paths)
	{
		if (Id.IsNone())
		{
			return nullptr;
		}

		if (const TObjectPtr<AssetType>* Existing = Cache.Find(Id))
		{
			if (*Existing)
			{
				return *Existing;
			}
		}

		const FSoftObjectPath* Path = Paths.Find(Id);
		if (!Path || !Path->IsValid())
		{
			return nullptr;
		}

		AssetType* Loaded = Cast<AssetType>(Path->TryLoad());
		if (Loaded)
		{
			Cache.Add(Id, Loaded);
		}
		return Loaded;
	}
}

void UKCFContentRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BuildIndices();
}

void UKCFContentRegistrySubsystem::Deinitialize()
{
	ResolvedWeapons.Reset();
	ResolvedPowerups.Reset();
	ResolvedVehicles.Reset();
	ResolvedGameModes.Reset();
	ResolvedArenas.Reset();
	ReportedMissingIds.Reset();

	Super::Deinitialize();
}

UKCFContentRegistrySubsystem* UKCFContentRegistrySubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKCFContentRegistrySubsystem>() : nullptr;
}

void UKCFContentRegistrySubsystem::BuildIndices()
{
	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	VehicleCatalogue = KCFContentRegistry::LoadTable(Settings->VehicleCatalogue);
	WeaponCatalogue = KCFContentRegistry::LoadTable(Settings->WeaponCatalogue);
	PowerupCatalogue = KCFContentRegistry::LoadTable(Settings->PowerupCatalogue);
	GameModeCatalogue = KCFContentRegistry::LoadTable(Settings->GameModeCatalogue);
	ArenaCatalogue = KCFContentRegistry::LoadTable(Settings->ArenaCatalogue);

	KCFContentRegistry::IndexTable<FKCFVehicleRow>(VehicleCatalogue, &FKCFVehicleRow::Vehicle, VehiclePaths);
	KCFContentRegistry::IndexTable<FKCFWeaponRow>(WeaponCatalogue, &FKCFWeaponRow::Weapon, WeaponPaths);
	KCFContentRegistry::IndexTable<FKCFPowerupRow>(PowerupCatalogue, &FKCFPowerupRow::Powerup, PowerupPaths);
	KCFContentRegistry::IndexTable<FKCFGameModeRow>(GameModeCatalogue, &FKCFGameModeRow::GameModeConfig, GameModePaths);
	KCFContentRegistry::IndexTable<FKCFArenaRow>(ArenaCatalogue, &FKCFArenaRow::Arena, ArenaPaths);

	UE_LOG(LogKartCombatFramework, Log,
		TEXT("Content registry indexed %d vehicles, %d weapons, %d powerups, %d game modes, %d arenas."),
		VehiclePaths.Num(), WeaponPaths.Num(), PowerupPaths.Num(), GameModePaths.Num(), ArenaPaths.Num());
}

void UKCFContentRegistrySubsystem::RefreshCatalogues()
{
	// Cached objects are kept: a refresh adds newly authored content rather than invalidating
	// definitions that live gameplay may still be holding a pointer to.
	ReportedMissingIds.Reset();
	BuildIndices();
}

UKCFWeaponDataAsset* UKCFContentRegistrySubsystem::FindWeapon(FName WeaponId)
{
	UKCFWeaponDataAsset* Result = KCFContentRegistry::ResolveCached(WeaponId, ResolvedWeapons, WeaponPaths);
	if (!Result && !WeaponId.IsNone())
	{
		ReportMissing(WeaponId, TEXT("weapon"));
	}
	return Result;
}

UKCFPowerupDataAsset* UKCFContentRegistrySubsystem::FindPowerup(FName PowerupId)
{
	UKCFPowerupDataAsset* Result = KCFContentRegistry::ResolveCached(PowerupId, ResolvedPowerups, PowerupPaths);
	if (!Result && !PowerupId.IsNone())
	{
		ReportMissing(PowerupId, TEXT("powerup"));
	}
	return Result;
}

UKCFVehicleDataAsset* UKCFContentRegistrySubsystem::FindVehicle(FName VehicleId)
{
	UKCFVehicleDataAsset* Result = KCFContentRegistry::ResolveCached(VehicleId, ResolvedVehicles, VehiclePaths);
	if (!Result && !VehicleId.IsNone())
	{
		ReportMissing(VehicleId, TEXT("vehicle"));
	}
	return Result;
}

UKCFGameModeDataAsset* UKCFContentRegistrySubsystem::FindGameModeConfig(FName GameModeId)
{
	return KCFContentRegistry::ResolveCached(GameModeId, ResolvedGameModes, GameModePaths);
}

UKCFArenaDataAsset* UKCFContentRegistrySubsystem::FindArena(FName ArenaId)
{
	return KCFContentRegistry::ResolveCached(ArenaId, ResolvedArenas, ArenaPaths);
}

void UKCFContentRegistrySubsystem::RegisterWeapon(UKCFWeaponDataAsset* Weapon)
{
	if (Weapon && !Weapon->Id.IsNone())
	{
		ResolvedWeapons.Add(Weapon->Id, Weapon);
		ReportedMissingIds.Remove(Weapon->Id);
	}
}

void UKCFContentRegistrySubsystem::RegisterPowerup(UKCFPowerupDataAsset* Powerup)
{
	if (Powerup && !Powerup->Id.IsNone())
	{
		ResolvedPowerups.Add(Powerup->Id, Powerup);
		ReportedMissingIds.Remove(Powerup->Id);
	}
}

void UKCFContentRegistrySubsystem::RegisterVehicle(UKCFVehicleDataAsset* Vehicle)
{
	if (Vehicle && !Vehicle->Id.IsNone())
	{
		ResolvedVehicles.Add(Vehicle->Id, Vehicle);
		ReportedMissingIds.Remove(Vehicle->Id);
	}
}

TArray<FName> UKCFContentRegistrySubsystem::GetAllWeaponIds() const
{
	TArray<FName> Ids;
	WeaponPaths.GetKeys(Ids);
	for (const TPair<FName, TObjectPtr<UKCFWeaponDataAsset>>& Pair : ResolvedWeapons)
	{
		Ids.AddUnique(Pair.Key);
	}
	return Ids;
}

TArray<FName> UKCFContentRegistrySubsystem::GetAllPowerupIds() const
{
	TArray<FName> Ids;
	PowerupPaths.GetKeys(Ids);
	for (const TPair<FName, TObjectPtr<UKCFPowerupDataAsset>>& Pair : ResolvedPowerups)
	{
		Ids.AddUnique(Pair.Key);
	}
	return Ids;
}

void UKCFContentRegistrySubsystem::ReportMissing(FName Id, const TCHAR* Kind)
{
	bool bAlreadyReported = false;
	ReportedMissingIds.Add(Id, &bAlreadyReported);
	if (!bAlreadyReported)
	{
		UE_LOG(LogKartCombatFramework, Warning,
			TEXT("No %s definition registered for id '%s'. Add it to the catalogue table in Project Settings > Kart Combat Framework, or register it at runtime."),
			Kind, *Id.ToString());
	}
}
