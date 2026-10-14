# Data Tables

Data Assets define *what things are*. Data Tables define *what exists in the game* — the catalogue of
vehicles a player can select, the weapons that can drop, the modes in the playlist. They are also how an
`FName` on the wire becomes a Data Asset on a client.

Create with `Add > Miscellaneous > Data Table` and pick the row struct.

## FKCFCatalogueRowBase

Every catalogue row inherits these.

| Field | Type | Purpose |
| --- | --- | --- |
| `Id` | `FName` | Must match both the row name and the target asset's `Id` |
| `DisplayName` | `FText` | Overrides the asset's name for this catalogue, if set |
| `Description` | `FText` | Overrides the asset's description |
| `bEnabled` | `bool` | Soft-disable without deleting the row |
| `SortOrder` | `int32` | Display order in selection UI |
| `UnlockCost` | `int32` | Currency to unlock. The framework does not implement a store |
| `UnlockLevel` | `int32` | Level required. Likewise |
| `Tags` | `FGameplayTagContainer` | Filtering |

### The three-way id rule

```
Row Name  ==  Row.Id  ==  TargetAsset.Id
```

All three must be the same string. `UKCFDataTableValidator` checks it and reports a mismatch, because the
failure mode otherwise is silent: a lookup returns null at runtime, in a packaged build, usually during a
match.

`bEnabled` is the right way to pull content. Deleting a row while a save game still references its id
leaves a dangling selection; disabling it lets the registry fall back cleanly.

---

## FKCFVehicleRow

| Field | Notes |
| --- | --- |
| `Vehicle` | `TSoftObjectPtr<UKCFVehicleDataAsset>` |
| `SpeedRating` | 0..1, for star bars in selection UI |
| `AccelerationRating` | 0..1 |
| `HandlingRating` | 0..1 |
| `DurabilityRating` | 0..1 |

The ratings are **presentation only**. They do not feed back into the vehicle's behaviour — the actual
numbers live on the Data Asset. Keeping them separate lets you show a comparable 5-star scale across karts
whose raw numbers are not linearly comparable.

Referenced by `AKCFGameMode::VehicleCatalogue` and by
`UKCFDeveloperSettings::VehicleCatalogue`. When a player calls
`AKCFPlayerController::RequestVehicleSelection(FName)`, the server validates the id against this table
before storing it on the player state.

---

## FKCFWeaponRow

| Field | Default | Notes |
| --- | --- | --- |
| `Weapon` | — | `TSoftObjectPtr<UKCFWeaponDataAsset>` |
| `DropWeight` | 1.0 | Relative likelihood in weighted selection. 0 excludes it |
| `LeaderDropWeightScale` | 1.0 | Multiplier when the collector is leading |
| `TrailerDropWeightScale` | 1.0 | Multiplier when the collector is behind |

The two scale fields are the hook for rubber-banding. A rocket with
`LeaderDropWeightScale = 0.2, TrailerDropWeightScale = 2.5` is rare for whoever is winning and common for
whoever is losing.

The framework stores and exposes these values but deliberately does **not** impose a rubber-band curve:
how aggressively to catch up is a design decision with a large effect on how a game feels, and it varies by
mode. Read them in your spawner subclass or game mode and apply the policy you want. `GetRaceOrder()` /
`GetScoreboard()` give you the position input.

---

## FKCFPowerupRow

Same shape as the weapon row: `Powerup` (`TSoftObjectPtr<UKCFPowerupDataAsset>`), `DropWeight`,
`LeaderDropWeightScale`, `TrailerDropWeightScale`.

Used directly by `AKCFPowerupSpawner::PowerupCatalogue`. When a spawner has a catalogue instead of an
explicit pool, it builds its weighted selection from every row where `bEnabled` is true and the row's
`Tags` satisfy `RequiredPowerupTags`.

---

## FKCFGameModeRow

| Field | Notes |
| --- | --- |
| `GameModeConfig` | `TSoftObjectPtr<UKCFGameModeDataAsset>` — the rules |
| `GameModeClass` | `FSoftClassPath` — the `AKCFGameMode` subclass to instantiate |
| `ArenaIds` | `TArray<FName>` of arenas this mode supports. Empty means all |
| `PlaylistWeight` | Relative likelihood in a random playlist |
| `bFeatured` | For a "featured mode" slot in a menu |

The split between `GameModeConfig` and `GameModeClass` is the point. Team deathmatch and free-for-all are
different *classes* because their win conditions differ; "TDM 10 minutes" and "TDM 3 minutes" are the same
class with different *configs*. A playlist with eight entries usually has three classes.

`FSoftClassPath` rather than `TSoftClassPtr` because the value is a travel URL option — it is turned into
a string for `ServerTravel`, so storing it as a path avoids a load just to produce a name.

---

## FKCFArenaRow

| Field | Notes |
| --- | --- |
| `Arena` | `TSoftObjectPtr<UKCFArenaDataAsset>` |
| `PlaylistWeight` | Relative likelihood in rotation |
| `bExcludeFromRandomRotation` | For tutorial or event maps that should only be chosen explicitly |

---

## Runtime resolution

`UKCFContentRegistrySubsystem` is a `UGameInstanceSubsystem` that turns ids into assets.

```cpp
UKCFWeaponDataAsset*   FindWeapon(FName WeaponId);
UKCFPowerupDataAsset*  FindPowerup(FName PowerupId);
UKCFVehicleDataAsset*  FindVehicle(FName VehicleId);
UKCFGameModeDataAsset* FindGameModeConfig(FName GameModeId);
UKCFArenaDataAsset*    FindArena(FName ArenaId);

TArray<FName> GetAllWeaponIds() const;
TArray<FName> GetAllPowerupIds() const;
void RefreshCatalogues();
```

On initialise it reads the five catalogue paths from `UKCFDeveloperSettings`. Lookups resolve the soft
pointer on first use and cache the result in a `TMap`, so the second lookup of an id is a hash lookup and
the asset stays loaded for the session.

From Blueprint, prefer the library wrappers — they handle a missing subsystem:

```
UKCFBlueprintLibrary::FindWeaponById(WorldContext, Id)
UKCFBlueprintLibrary::FindPowerupById(WorldContext, Id)
UKCFBlueprintLibrary::FindVehicleById(WorldContext, Id)
```

### Why ids on the wire

An inventory slot holds `FName WeaponId`, not `UKCFWeaponDataAsset*`. Replicating an object pointer forces
the asset to be loaded and net-relevant on every receiving client. Replicating a name costs a few bytes,
and each client resolves it lazily — only if it actually needs to draw an icon. A dedicated server never
resolves any of them.

This is the same reason kill feed entries carry a weapon id rather than a weapon.

### Registering assets without a table

For dynamic or test content:

```cpp
void RegisterWeapon(UKCFWeaponDataAsset* Weapon);
void RegisterPowerup(UKCFPowerupDataAsset* Powerup);
void RegisterVehicle(UKCFVehicleDataAsset* Vehicle);
```

These add to the resolved map directly. Useful in automation tests and for content delivered outside the
catalogue; not a substitute for a table in shipping content, because nothing enumerates them.

---

## Authoring workflow

1. Create the Data Asset. Set its `Id`.
2. Create or open the catalogue Data Table.
3. Add a row. **Name the row exactly the same as the `Id`.**
4. Set the row's `Id` field to the same string again.
5. Point the row's soft reference at the asset.
6. Make sure the table is assigned in `Project Settings > Game > Kart Combat Framework`.

Step 6 is the one people forget. A perfectly authored table that nothing points at produces exactly the
same symptom as no table at all — see
[Troubleshooting.md](Troubleshooting.md#a-weapon-or-powerup-id-resolves-to-null).

### CSV and JSON

Data Tables import from CSV and JSON, which is how a balance pass gets done in a spreadsheet. The header
row uses the struct's property names:

```csv
Name,Id,DisplayName,bEnabled,SortOrder,Weapon,DropWeight,LeaderDropWeightScale,TrailerDropWeightScale
Blaster,Blaster,"Blaster",True,10,"/Game/Weapons/DA_Weapon_Blaster.DA_Weapon_Blaster",1.0,1.0,1.0
Missile,Missile,"Homing Missile",True,20,"/Game/Weapons/DA_Weapon_Missile.DA_Weapon_Missile",0.6,0.2,2.5
```

Soft object references are full object paths with the `Package.Object` form. Re-import overwrites the
table, so keep the CSV as the source of truth once you start this way.

---

## Validation

`UKCFDataTableValidator` runs in the editor module and checks each catalogue table for:

- row name, `Id` field and target asset `Id` all matching
- duplicate ids
- null or unresolvable soft references
- negative `DropWeight` or `PlaylistWeight`
- rows whose referenced asset fails its own `ValidateConfiguration`

It reads ids through the asset registry where it can — `UKCFDataAssetBase::Id` is marked
`AssetRegistrySearchable` — so validating a large catalogue does not load every asset in it.

Run it from `Window > Developer Tools > Data Validation`, or right-click a folder and choose
*Validate Assets in Folder*. Wire the same commandlet into CI if you have one.
