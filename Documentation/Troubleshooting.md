# Troubleshooting

Symptoms, causes, fixes. Ordered roughly by how often each one comes up.

## Logging

Four categories. Turn up what you need and nothing else:

```
log LogKartCombatFramework Verbose    general
log LogKCFNet Verbose                 replication, RPCs, sessions
log LogKCFCombat Verbose              damage, weapons, powerups
log LogKCFMatch Verbose               match state, scoring, respawn
```

Two developer settings worth knowing (`Project Settings > Game > Kart Combat Framework`):

| Setting | Effect |
| --- | --- |
| `bLogRejectedRPCs` | Logs every server-side rejection at Warning. The first thing to turn on when something a client asks for is not happening |
| `bEnableDebugDrawing` | Draws spawn selection, damage radii and zone bounds in the world |

---

## Nothing happens when I press Play

**The match never leaves `WaitingForPlayers`.** The state machine waits for `MinPlayers` on the game mode
config. PIE with one player and `MinPlayers = 2` waits forever.

Fix: set `MinPlayers` to 1 while iterating, or call `AKCFGameMode::ForceStartMatch()`.

**Karts spawn but cannot move.** Movement is locked outside live match states. Check
`GetMatchState()` — during `Preparing` and `Countdown`, `bDrivingEnabled` is false by design.

**No karts spawn at all.** Check in order:

1. The level has at least one `AKCFPlayerSpawnPoint`.
2. `KartPawnClass` is set on the game mode.
3. `FallbackVehicle` is set, or the vehicle catalogue resolves.
4. The World Settings game mode override is your Blueprint, not the project default.

`DefaultPawnClass` is deliberately null on `AKCFGameMode` — the framework spawns karts itself so the
vehicle definition is applied before `BeginPlay`. Setting it yourself causes double spawns.

---

## The kart falls through the floor

The arcade movement component traces down `GroundTraceDistance` (60 cm). If it starts further above the
ground than that, it never finds it.

- Spawn points should be ~100 cm above the floor, not 500.
- The floor needs collision enabled and a channel the kart queries.
- Check `KillZ` on the arena data asset — if it is above your floor, every kart dies on spawn.

---

## The kart will not turn, or spins on the spot

Both are `TurnRateBySpeedCurve` on the movement data asset.

An empty curve means a flat 1.0 multiplier, so `TurnRate` applies at every speed including zero — which is
a kart that pirouettes while stationary. Author a curve that starts near 0.3 at zero speed, rises to 1.0
around 40%, and eases to 0.7 at top speed.

If the kart will not turn at all: `TurnRate` too low, `SteeringInterpSpeed` too low (steering is still
ramping when you release), or `Grip` so high that lateral velocity cannot develop.

---

## Drift does nothing

Three gates, all on the movement data:

1. Speed is below `MinDriftSpeedPercent` (0.35 of `MaxSpeed`). Drift is intentionally unavailable at low
   speed.
2. `DriftGrip` is too close to `Grip`. The entire drift model is substituting one for the other — with
   0.92 and 0.90 there is nothing to feel. Try 0.92 and 0.35.
3. The drift input is not reaching the component. Check `AKCFKartPawn::IsDrifting()`, and on mobile that
   `StartDrift()` is wired and `bDriftIsToggle` matches your button's behaviour.

---

## Damage is not applied

Work down the pipeline — each stage has its own rejection reason, visible in the returned
`FKCFDamageResult::RejectReason`:

| Reason | Meaning |
| --- | --- |
| `NoAuthority` | You called `ApplyDamage` from a client. It only works on the server |
| `TargetNotDamageable` | The target does not implement `IKCFDamageable` |
| `TargetAlreadyDead` | Already dead |
| `MatchNotLive` | Match is not in `InProgress` or `SuddenDeath` |
| `FriendlyFireDisabled` | Same team and `bFriendlyFire` is false |
| `TargetInvulnerable` | Spawn protection or an invulnerability powerup |
| `ZeroDamage` | Zero base damage, or scaled to zero by a `DamageMultipliers` entry |

The most common by far is `TargetInvulnerable` from spawn protection — 2.5 seconds is long enough that it
looks like a bug during testing. `log LogKCFCombat Verbose` prints the reason for every rejection.

---

## Weapons fire on the client but nothing happens

The client is predicting an animation the server rejected. Turn on `bLogRejectedRPCs`.

Likely causes:

- **Cooldown.** Evaluated against server time, so a client that thinks it is ready may not be.
- **Ammo.** The server's count is authoritative; the client's may be stale.
- **Spawn protection.** Firing is blocked unless the weapon sets
  `bUsableDuringSpawnProtection`.
- **Projectile cap.** `MaxLiveProjectilesPerPlayer` (default 8) refuses further shots.
- **Wrong slot.** The server reads *its* inventory. If the client's selection has not round-tripped, the
  server may be looking at a different slot.

`ClientFireRejected()` fires back to the client so you can clear a predicted animation — bind it.

---

## A weapon or powerup id resolves to null

`FindWeaponById` returning null means the content registry could not resolve the id. Check, in order:

1. The catalogue Data Table is assigned in `Project Settings > Game > Kart Combat Framework`. **This is
   the one people forget** — a perfectly authored table that nothing points at behaves exactly like no
   table at all.
2. The row exists and `bEnabled` is true.
3. The three-way id rule holds: row name == row `Id` == target asset `Id`.
4. The soft reference in the row actually points at an asset.

Run the editor validator (`Window > Developer Tools > Data Validation`) — it checks all four.

---

## Powerups do not spawn

**Spawners placed in a level managed by `AKCFGameMode` do not start themselves.** The game mode enables
them at `InProgress` and disables them at `Finished`. If the match has not started, there are no powerups —
that is correct behaviour.

Otherwise:

- `bPowerupsEnabled` is false on the game mode config.
- `PowerupPool` is empty *and* `PowerupCatalogue` is unset.
- `RequiredPowerupTags` excludes everything in the catalogue.
- `MaxActivePickups` is already reached.
- Every pool entry has `Weight = 0`.

---

## Collecting a pickup does nothing

- The kart's inventory is full and the powerup is not stackable. `CollectPowerup` returns false and the
  pickup is deliberately left in the world.
- `Type == Weapon` but `GrantedWeapon` is unset.
- A non-weapon type with no `EffectClass`.
- The powerup is on cooldown for that kart (`IsOnCooldown`).

---

## Health bars stop updating after the first death

The classic one. Component delegates belong to a *specific kart*, and respawning gives the player a new
pawn. The old bindings point at a destroyed actor.

Fix: bind `AKCFPlayerController::OnPossessedKartChanged` and re-bind every component delegate there.

---

## Other players' karts do not steer or drift visually

Simulated proxies drive their visuals from `ReplicatedInput`, which is `COND_SkipOwner`. If a custom pawn
subclass stopped registering it in `GetLifetimeReplicatedProps`, remote karts slide along interpolated
positions with the wheels straight.

Check your override calls `Super::GetLifetimeReplicatedProps(OutLifetimeProps)`. Forgetting that line
silently drops every parent-class property.

---

## I cannot see another player's inventory

Correct. `Slots` and `SelectedSlot` are `COND_OwnerOnly` on purpose — see
[Multiplayer.md](Multiplayer.md#components). Knowing everyone's held item would be a significant
information advantage.

What *is* visible: `ActivePowerups` (so shields and nitro show), health and shield, boost state.

---

## Values look right on the server and wrong on a client

Usually one of three things:

1. **Written on a client.** A client write to a replicated property is overwritten at the next update and
   never propagates. Guard writes with `HasAuthority()`.
2. **Not registered.** A `UPROPERTY(Replicated)` that is not in `GetLifetimeReplicatedProps` silently never
   replicates. There is no warning.
3. **Sent via multicast.** Multicasts do not reach clients that were not relevant or not yet connected.
   Anything durable must be a replicated property. The framework's three multicasts are all purely
   cosmetic.

---

## The countdown is wrong on clients

Use `AKCFGameState::GetRemainingStateTime()` or `GetFormattedRemainingTime()`. They derive from the
replicated `StateEndServerTime` and the synchronised server clock.

A locally accumulated timer drifts; `GetWorld()->GetTimeSeconds()` is per-machine and means nothing across
the network.

---

## The battle royale zone jumps on clients

The zone is derived from two replicated timestamps, so it should be smooth. Jumping means either:

- A `ShrinkSeconds` of 0 in a phase, making the transition instantaneous.
- `GetServerWorldTimeSeconds()` is unavailable because no game state exists yet. Check for the game state
  before reading the radius on a client that just connected.

---

## Race laps do not count

The anti-shortcut rule is strict by design: **only the checkpoint you are actually due to cross counts.**

```cpp
if (Ordinal != NextCheckpointOrdinal) { return false; }
```

Driving back through a checkpoint you already passed, or cutting the track to a later one, does nothing.

So if laps are not counting:

- `CheckpointIndex` values must be a contiguous sequence starting at 0 with no gaps and no duplicates.
- Exactly one checkpoint must have `bIsFinishLine` set.
- Trigger volumes must be wide enough to cover the whole track, including the racing line through a
  corner. A gap at the edge means a fast player misses the gate.

---

## Build failures

**`Cannot open include file: 'KartCombatFramework/...'`** — the include path is module-relative, not
plugin-relative. Use `#include "Vehicles/KCFKartPawn.h"`, not
`#include "KartCombatFramework/Public/Vehicles/KCFKartPawn.h"`.

**`Unresolved external symbol` on a framework class** — `"KartCombatFramework"` is missing from your
module's `Build.cs` dependencies.

**`Unrecognized type 'UKCFVehicleDataAsset'`** — UHT needs the generated header included last:

```cpp
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MyKart.generated.h"    // always last
```

**`ChaosWheeledVehicleMovementComponent` not found** — the Chaos Vehicles plugin is disabled, or your
module depends on the Chaos bridge module without it.

**Stale generated code after moving files** — delete `Binaries/` and `Intermediate/` in both the project
and the plugin, regenerate project files, and rebuild.

---

## Static validation

The repository ships a static checker that runs without an Unreal toolchain:

```bash
python3 Tools/validate_plugin.py
```

It verifies reflection macros, `GetLifetimeReplicatedProps` registration against declared `Replicated`
properties, RPC naming and `_Validate` presence, include hygiene, export macros on cross-module types, and
the plugin descriptor against the modules on disk.

It is not a compiler. It catches the class of mistake that is tedious to find by eye and obvious in
retrospect; it cannot catch a type error.

---

## Automation tests

Fifteen tests covering deterministic logic. Run from the editor's Session Frontend
(`Window > Test Automation`), filter `KartCombatFramework`, or headless:

```bash
UnrealEditor-Cmd YourProject.uproject \
    -ExecCmds="Automation RunTests KartCombatFramework; Quit" \
    -unattended -nopause -nullrhi -log
```

| Group | Covers |
| --- | --- |
| `Damage.*` | Shield absorbs before health, shield bypass, lethal damage kills exactly once, invulnerability stacking, kill volumes reaching protected targets, revive, heal clamping |
| `Inventory.*` | Pickups stack before taking a new slot, consuming a slot clears and reselects, client selection requests are validated, clear-all |
| `Match.*` | The transition table is well formed, state classification is consistent |
| `MobileInput.*` | The steering curve is well behaved, input state is sanitised before use |

These cover arithmetic and rules, not replication. Network behaviour needs the matrix below.

---

## Test matrix

Run before shipping. Each row catches a distinct class of bug.

| Configuration | Catches |
| --- | --- |
| Standalone, 1 player | Basic functionality |
| Listen server, 1 player | Authority and local-player paths coexisting |
| Listen server, 2+ clients | Replication, relevancy, the usual multiplayer bugs |
| **Dedicated server, 2+ clients** | Anything you accidentally made client-only stops working |
| **200 ms simulated latency** (`Net PktLag=200`) | Anything you accidentally made client-predicted starts fighting the server |
| Packet loss (`Net PktLoss=5`) | Reliance on unreliable RPCs for state |
| Client joins mid-match | State that was only ever sent by multicast |
| Client disconnects and reconnects | `CopyProperties` / `OverrideWith` coverage |
| Mobile device, cellular | Touch layout, thermal throttling, real latency |
| 12 players, full match | Performance, projectile caps, ledger growth |

Specific things to verify by hand:

- Damage applies, death triggers, respawn works on **every** client
- Powerups can only be collected once, by one player
- Weapon cooldowns hold under latency
- Score and the kill feed agree across all machines
- The match ends on both the time limit and the score limit
- Teams assign evenly and friendly fire behaves as configured
- A modified client cannot fire faster than `FireInterval` (the server rejects it)

### Simulating bad networks

```
Net PktLag=200          add 200 ms
Net PktLagVariance=50   add jitter
Net PktLoss=5           drop 5%
Net PktOrder=1          reorder packets
```

Test with all four at once at least once. Problems that hide under clean conditions appear immediately.

---

## Performance investigation

```
stat game       gameplay tick cost
stat net        replication bandwidth and actor counts
stat unit       the frame budget overall
```

Expected: kart pawns and movement components dominate; nothing else ticks meaningfully. If something else
is at the top, it is in your project code — check [Architecture.md](Architecture.md#the-tick-budget) for
the complete list of what the framework ticks.

Bandwidth too high? Lower `KartNetUpdateFrequency`, lower `KartNetCullDistance`, and check that nothing in
your own code replicates a value every frame that could be two timestamps instead.

---

## Still stuck

1. `log LogKCFNet Verbose` and `log LogKCFCombat Verbose`, then reproduce.
2. Turn on `bLogRejectedRPCs` — a surprising number of "it doesn't work" cases are a server-side rejection
   that was silent.
3. Run `python3 Tools/validate_plugin.py`.
4. Run the editor data validator over your content folders.
5. Reproduce on a listen server first. If it only fails on a dedicated server, the problem is a cosmetic
   path running where there is no renderer — see [DedicatedServer.md](DedicatedServer.md).
