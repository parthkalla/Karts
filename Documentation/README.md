# Kart Combat Framework

A server-authoritative, data-driven gameplay framework for multiplayer arcade kart combat, built for
Unreal Engine 5.8. It is an original implementation: no engine sample, commercial title, or third-party
source was copied, and the plugin ships no art, audio, maps, or character content of any kind.

The framework is the *systems* half of a kart combat game. You bring meshes, materials, Niagara systems,
sounds, levels and UMG widgets; the plugin brings karts, movement, health and shields, a validated damage
pipeline, weapons, projectiles, deployables, powerups and spawners, inventory, match flow, respawn, teams,
scoring, race laps, a shrinking battle royale zone, mobile touch input, and a swappable online session layer.

Everything a game needs to configure is a Data Asset, a Data Table row, or a Blueprint-exposed property.
A project should be able to copy this plugin into `Plugins/`, enable it, author content, and ship —
**without editing a single plugin source file**.

## Documentation map

Start with Installation, then QuickStart. The rest are reference.

| Document | What it covers |
| --- | --- |
| [Installation.md](Installation.md) | Dropping the plugin into a project, module setup, required engine plugins |
| [QuickStart.md](QuickStart.md) | A playable free-for-all match from an empty project, step by step |
| [Architecture.md](Architecture.md) | Module layout, ownership rules, who-talks-to-whom, the tick budget |
| [Multiplayer.md](Multiplayer.md) | The authority model, every replicated property, every RPC and its validation |
| [VehicleSystem.md](VehicleSystem.md) | Kart pawn, arcade movement, the Chaos Vehicles backend, camera |
| [WeaponSystem.md](WeaponSystem.md) | Weapon data assets, firing, projectiles, deployables, hitscan, area effects |
| [PowerupSystem.md](PowerupSystem.md) | Powerups, effects, pickups, spawners, weighted drops |
| [DataAssets.md](DataAssets.md) | Every Data Asset type and what each field does |
| [DataTables.md](DataTables.md) | Catalogue row structs and how ids resolve at runtime |
| [BlueprintAPI.md](BlueprintAPI.md) | The Blueprint-callable surface, grouped by the UI or logic that needs it |
| [Mobile.md](Mobile.md) | Touch input, steering modes, auto-acceleration, building your own touch HUD |
| [DedicatedServer.md](DedicatedServer.md) | What is skipped on a dedicated server and how to verify it |
| [OnlineIntegration.md](OnlineIntegration.md) | The session interface, the built-in LAN service, writing an EOS/Steam backend |
| [Troubleshooting.md](Troubleshooting.md) | Symptoms, causes and fixes for the things that actually go wrong |
| [../../../Documentation/KartCombatFramework_ImplementationPlan.md](../../../Documentation/KartCombatFramework_ImplementationPlan.md) | The design plan the implementation follows |

## What is in the box

**Match flow.** `AKCFGameMode` drives a seven-state machine (`WaitingForPlayers`, `Preparing`,
`Countdown`, `InProgress`, `SuddenDeath`, `Finished`, `ReturningToLobby`) with a validated transition
table. `AKCFGameState` replicates the current state and the time it ends, so every client can render a
countdown from two replicated values rather than a per-frame float.

**Five game modes.** Free-for-all, team deathmatch, elimination (lives), race (checkpoints and laps), and
battle royale (elimination plus a shrinking play zone). Each one is a subclass that overrides
`CheckWinCondition` and, where relevant, `AssignFinalPlacements` and `GetRespawnTransformOverride`.

**Karts.** `AKCFKartPawn` composes health, inventory, weapon, powerup, boost and combat components around a
movement backend reached through `IKCFVehicleMovement`. The default backend is an arcade kinematic
controller; an optional module swaps in Chaos Wheeled Vehicles without changing any gameplay code.

**A single damage pipeline.** Everything that hurts a kart goes through `UKCFDamageSubsystem::ApplyDamage`,
which runs the same ordered checks every time: authority, target is damageable, target is alive, match is
live, friendly fire, immunity, per-category multiplier, shield, health, knockback, assist ledger, statistics.
There is no second path.

**Combat.** Data-driven weapons with five delivery modes (projectile, hitscan, deployable, self, area
effect), homing, bouncing, explosion falloff, spread, ammo and cooldowns — all fields on a Data Asset.

**Powerups.** Timed, instant, passive and weapon-granting powerups, with stacking, cooldowns, and
`UKCFPowerupEffect` subclasses (speed, shield, repair, invulnerability, nitro) that Blueprints can extend.

**Mobile as a first-class target.** Four steering models, auto-acceleration, optional toggle drift and
boost, an input buffer, and a fully Blueprint-callable input API. The plugin contains no widgets, so your
touch HUD is entirely yours.

**Dedicated-server safe.** The runtime module does not depend on UMG, Slate, or any audio or VFX module.
Camera, audio and cosmetic ticking are guarded so a `-server` build constructs none of it.

## Requirements

- Unreal Engine 5.8
- The `EnhancedInput` plugin (enabled automatically by the descriptor)
- The `ChaosVehiclesPlugin` plugin, only if you use the optional `KartCombatFrameworkChaosVehicles` module
- A C++ project (the plugin compiles C++; your game code can be pure Blueprint)

## Licence and originality

MIT. The plugin is original work and is not affiliated with, derived from, or endorsed by any commercial
kart game or its publisher. No proprietary source, asset, name, trademark or reverse-engineered system is
included. See [Architecture.md](Architecture.md) for the deliberate design decisions this implies.

## Known limitations

These are real and documented rather than hidden behind a `TODO`:

1. **Movement uses server reconciliation with smoothed correction, not full rollback prediction.** Local
   clients simulate immediately and the server corrects them; there is no replay of buffered inputs. This
   is the right trade for arcade karts at typical latencies, and it is wrong for a competitive racer at
   200 ms. Replacing it means implementing `FNetworkPredictionData_Client` on the movement component or
   adopting the Network Prediction plugin. The `IKCFVehicleMovement` boundary is where you would do it.
2. **The built-in LAN session service does not discover servers.** `UKCFNullSessionService` can host and
   can join an address you give it, and it returns servers you register with `AddKnownServer`, but it does
   not broadcast or scan. Real matchmaking means implementing `IKCFSessionService` against EOS, Steam or
   your own backend — see [OnlineIntegration.md](OnlineIntegration.md).
3. **The Chaos backend needs project-authored wheel setups.** `UKCFChaosKartMovementComponent` maps the
   framework's input and movement data onto Chaos, but wheel classes, suspension and the engine/transmission
   curves are physics-asset work that belongs in your project, not in a generic plugin.
4. **There is no `AKCFWeapon` actor.** Weapons are data, not actors. See
   [WeaponSystem.md](WeaponSystem.md#why-there-is-no-akcfweapon-actor) for why, and for the one case where
   you would add one.
5. **No automated network integration tests.** The automation tests cover deterministic logic (damage
   arithmetic, inventory rules, the transition table, input sanitisation). Replication and RPC behaviour
   are covered by the manual matrix in [Troubleshooting.md](Troubleshooting.md#test-matrix), because
   multi-process PIE testing needs an editor, which CI for a standalone plugin does not have.
