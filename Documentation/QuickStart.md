# Quick Start

Goal: a playable four-player free-for-all kart match, from an empty project to two clients shooting at
each other in PIE. This is all content work — you will not write a line of C++.

Assumes the plugin is installed ([Installation.md](Installation.md)).

## 1. Movement data

`Content Browser > Add > Miscellaneous > Data Asset > KCFMovementDataAsset`. Name it `DA_Movement_Default`.

The defaults are tuned to be drivable immediately. The four fields worth setting first:

| Field | Default | Feel |
| --- | --- | --- |
| Max Speed | 1800 | cm/s. ~65 km/h. Raise to 2600 for something twitchier. |
| Acceleration | 2400 | Time to top speed is roughly `MaxSpeed / Acceleration` seconds. |
| Turn Rate | 160 | Degrees per second at full lock. |
| Drift Grip | 0.35 | Lower slides more. `Grip` (0.92) is the non-drifting value. |

Everything else — slope limits, air control, knockback decay, the speed-dependent acceleration and turn
curves — has a working default. [DataAssets.md](DataAssets.md#ukcfmovementdataasset) documents all of it.

## 2. Vehicle data

`Add > Data Asset > KCFVehicleDataAsset`. Name it `DA_Vehicle_Starter`.

Set:

- **Id**: `Starter` — this is the key other systems look the vehicle up by. It must be unique.
- **Display Name**: `Starter Kart`
- **Movement**: `DA_Movement_Default`
- **Static Vehicle Mesh**: any box or cube mesh will do for now. Use `Shape_Cube` from the engine content
  (enable *Show Engine Content* in the Content Browser filter).
- **Max Health**: 100
- **Inventory Slots**: 1 — raise it later if you want players to carry more than one item.

Leave the Niagara and sound fields empty. They are all `TSoftObjectPtr` and nothing breaks when unset.

## 3. Game mode configuration

`Add > Data Asset > KCFGameModeDataAsset`. Name it `DA_GameMode_FFA`.

| Field | Value | Why |
| --- | --- | --- |
| Id | `FFA` | Unique id |
| Min Players | 1 | So PIE with one client still starts |
| Match Duration Seconds | 180 | Three-minute matches |
| Score Limit | 10 | First to ten kills ends it |
| Score Per Kill | 1 | |
| Team Count | 0 | Zero means free-for-all. Two or more makes it a team mode. |
| Respawn Enabled | true | |
| Respawn Delay Seconds | 3 | |
| Spawn Protection Seconds | 2.5 | Invulnerable but unable to fire, unless the weapon opts in |

`Countdown Seconds` (3) and `Preparing Seconds` (3) control the pre-match states. Drop both to 0.5 while
iterating so you are not waiting six seconds per PIE run.

## 4. A weapon

`Add > Data Asset > KCFWeaponDataAsset`. Name it `DA_Weapon_Blaster`.

- **Id**: `Blaster`
- **Delivery**: `Projectile`
- **Projectile Class**: `KCFProjectile` (the base class works out of the box)
- **Damage**: 40
- **Projectile Speed**: 4200
- **Ammo** / **Max Ammo**: 3
- **Fire Interval**: 0.45
- **Remove When Empty**: true

That is a complete, functional weapon. No Blueprint, no projectile subclass.

## 5. A powerup that grants it

`Add > Data Asset > KCFPowerupDataAsset`. Name it `DA_Powerup_Blaster`.

- **Id**: `PU_Blaster`
- **Type**: `Weapon`
- **Granted Weapon**: `DA_Weapon_Blaster`
- **Pickup Mesh**: a sphere or cube
- **Auto Activate On Pickup**: false — the player chooses when to fire

Add a second one so pickups are not all identical: duplicate it as `DA_Powerup_Repair`, set **Type** to
`Instant`, clear **Granted Weapon**, set **Effect Class** to `KCFRepairEffect` and **Magnitude** to 50.
That is a 50-point repair.

## 6. Catalogue tables

The registry resolves ids through Data Tables. `Add > Miscellaneous > Data Table`, and pick the row struct:

| Table name | Row struct | Rows |
| --- | --- | --- |
| `DT_Vehicles` | `KCFVehicleRow` | Row name `Starter`, Id `Starter`, Vehicle `DA_Vehicle_Starter` |
| `DT_Weapons` | `KCFWeaponRow` | Row name `Blaster`, Id `Blaster`, Weapon `DA_Weapon_Blaster`, Drop Weight 1.0 |
| `DT_Powerups` | `KCFPowerupRow` | `PU_Blaster` and `PU_Repair`, each pointing at its data asset |

The **row name and the `Id` field must match**. The editor validator flags it if they drift.

Now point the project at them: `Project Settings > Game > Kart Combat Framework` → Vehicle Catalogue,
Weapon Catalogue, Powerup Catalogue. Also set Default Vehicle to `DA_Vehicle_Starter` and Default Game
Mode Config to `DA_GameMode_FFA`.

## 7. The level

Create or open a level. Add:

**Floor.** Any large static mesh with collision. The arcade movement component traces downward
(`GroundTraceDistance`, 60 cm by default) so it needs something to stand on.

**Spawn points.** Place four `KCFPlayerSpawnPoint` actors, spread out and a little above the floor
(z ≈ 100). Leave `TeamId` at -1 for a free-for-all. Spawn selection defaults to
`FarthestFromEnemies`, which needs several to choose between — with one spawn point everyone lands on
top of each other.

**Powerup spawners.** Place two or three `KCFPowerupSpawner` actors. On each:

- **Powerup Pool**: add two entries — `DA_Powerup_Blaster` weight 1.0, `DA_Powerup_Repair` weight 0.5
- **Spawn Interval**: 12
- **Respawn Delay**: 8
- **Max Active Pickups**: 1

Alternatively set **Powerup Catalogue** to `DT_Powerups` and leave the pool empty, and the spawner draws
from every enabled row using each row's `DropWeight`.

Spawners placed in a level managed by `AKCFGameMode` do **not** self-start; the game mode enables them when
the match reaches `InProgress` and disables them at `Finished`. That is deliberate — nobody should be
collecting powerups during the countdown.

## 8. The game mode Blueprint

`Add > Blueprint Class`, parent `KCFFreeForAllGameMode`. Name it `BP_GameMode_FFA`.

In the Details panel:

- **Game Mode Config**: `DA_GameMode_FFA`
- **Kart Pawn Class**: `KCFKartPawn` (or your own Blueprint subclass of it)
- **Vehicle Catalogue**: `DT_Vehicles`
- **Fallback Vehicle**: `DA_Vehicle_Starter`

The parent already sets `GameStateClass`, `PlayerStateClass` and `PlayerControllerClass` to the framework
classes, and sets `DefaultPawnClass` to null on purpose — the framework spawns karts itself, after the
vehicle definition is known, rather than letting the engine's automatic restart do it early.

Set it on the level: `World Settings > Game Mode Override > BP_GameMode_FFA`.

## 9. Input

`Add > Input > Input Mapping Context`, name it `IMC_Kart`. Add Input Actions for the controls you want —
at minimum `IA_Steer` (Axis1D), `IA_Throttle` (Axis1D), `IA_Fire` (Digital), `IA_Boost` (Digital).

Bind keys in `IMC_Kart`: A/D or Left/Right on `IA_Steer` (with a Negate modifier on one), W on
`IA_Throttle`, Left Mouse on `IA_Fire`, Space on `IA_Boost`.

`Add > Data Asset > KCFInputConfigDataAsset`, name it `DA_Input_Kart`, set **Mapping Context** to `IMC_Kart`
and wire each action field to the matching Input Action.

Finally, create `BP_PlayerController` parented to `KCFPlayerController`, select its **Kart Input**
component, and set **Input Config** to `DA_Input_Kart`. Point `BP_GameMode_FFA`'s **Player Controller
Class** at it.

For touch controls, see [Mobile.md](Mobile.md) — the input component's mobile API needs no Input Actions
at all.

## 10. Play

`Play > Advanced Settings > Number of Players: 2`, Net Mode `Play As Listen Server`. Press Play.

You should see:

- Two karts spawn at different spawn points
- A three-second countdown, during which movement is disabled
- Karts drive; powerup pickups appear at the spawners after the match starts
- Driving over a pickup grants the item; firing destroys the other kart; the victim respawns after 3 s
- The match ends at ten kills or three minutes

Nothing appears on screen besides the world, because the plugin ships no HUD. That is step 11.

## 11. A HUD

Create a UMG widget and add it from your player controller's `BeginPlay`. Everything you need is a
delegate or a pure getter:

| You want to show | Bind / call |
| --- | --- |
| Match countdown | `GetKCFGameState()` → `GetFormattedRemainingTime()` |
| Match state changes | `AKCFGameState::OnMatchStateChanged` |
| Health bar | Kart's `GetHealthComponent()` → `OnHealthChanged` |
| Shield bar | `OnShieldChanged` |
| Current item icon | Inventory's `OnInventoryChanged`, then `GetCurrentItem()` |
| Weapon cooldown ring | `UKCFWeaponComponent::GetCooldownPercent()` |
| Boost charges | `UKCFBoostComponent::OnChargesChanged` |
| Kill feed | `AKCFGameState::OnKillFeedEntryAdded` |
| Hit marker | `AKCFPlayerController::OnHitConfirmed` |
| Scoreboard | `UKCFBlueprintLibrary::GetScoreboard()` |
| Respawn timer | `AKCFPlayerController::GetRespawnCountdown()` |

[BlueprintAPI.md](BlueprintAPI.md) is the complete list.

## 12. Test it properly

Before you believe it works, run the matrix in
[Troubleshooting.md](Troubleshooting.md#test-matrix). The two that catch the most bugs are *dedicated
server with two clients* (anything you accidentally made client-only stops working) and *one client, 200 ms
simulated latency* (anything you accidentally made client-predicted starts fighting the server).

## Where to go next

- **Teams**: set `Team Count` to 2 on the config, fill in `Teams`, parent your game mode to
  `KCFTeamDeathmatchGameMode`, and set `TeamId` on the spawn points.
- **Lives instead of respawns**: parent to `KCFEliminationGameMode` and set `Lives Per Player`.
- **Racing**: parent to `KCFRaceGameMode` and place `KCFRaceCheckpoint` actors with ascending
  `CheckpointIndex`, marking the last one `bIsFinishLine`. See
  [VehicleSystem.md](VehicleSystem.md) and the race section of [Architecture.md](Architecture.md).
- **Battle royale**: parent to `KCFBattleRoyaleGameMode` and configure the zone phases.
- **More interesting weapons**: homing missiles, mines and shockwaves are all field combinations on
  `UKCFWeaponDataAsset`. [WeaponSystem.md](WeaponSystem.md) has recipes.
