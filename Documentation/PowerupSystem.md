# Powerup System

Four pieces: `UKCFPowerupDataAsset` (what a powerup is), `UKCFPowerupEffect` (what it does),
`AKCFPowerupPickup` (the thing in the world), and `AKCFPowerupSpawner` (what puts it there).
`UKCFPowerupComponent` on the kart ties them together.

## Powerup types

`EKCFPowerupType` decides when the powerup resolves.

| Type | Behaviour |
| --- | --- |
| `Instant` | Applies the moment it is collected, never enters the inventory. Repairs, instant shields. |
| `Timed` | Goes into the inventory; activating it runs an effect for `Duration` seconds. |
| `Weapon` | Grants `GrantedWeapon` into an inventory slot. The player fires it like any weapon. |
| `Passive` | Applies on activation and persists until death or the end of the match. No duration. |
| `Defensive` / `Offensive` | Classification variants of `Timed` that let UI colour and sort them. |

`bAutoActivateOnPickup` forces immediate activation for any type. `Instant` behaves as though it is always
set.

## Collection

`UKCFPowerupComponent::CollectPowerup(UKCFPowerupDataAsset*)` is the entry point and is **server-only**
(`HasPowerupAuthority()` gates it). It:

1. Rejects the call if the component lacks authority.
2. If `Type == Weapon`, grants `GrantedWeapon` to the inventory and stops there.
3. Otherwise adds the powerup to the inventory, stacking onto an existing slot when `bStackable` and the
   stack is below `MaxStacks`.
4. If `bAutoActivateOnPickup` or `Type == Instant`, activates it immediately.
5. Increments `CollectedCount` (replicated `COND_OwnerOnly`) and the player state's `PowerupsCollected`.

Returns `false` when the inventory is full and the powerup cannot stack. The pickup actor uses that return
value to decide whether to consume itself — a full player drives through a pickup without wasting it.

## Activation and effects

`UKCFPowerupEffect` is a `UObject` (not an actor — effects have no transform and no collision) with three
`BlueprintNativeEvent`s:

```cpp
void ApplyEffect(AKCFKartPawn* Target, float Magnitude);
void RevertEffect(AKCFKartPawn* Target);
void TickEffect(AKCFKartPawn* Target, float DeltaSeconds);   // only if PeriodSeconds > 0
```

`Magnitude` comes from the data asset, so one effect class serves a whole family: a 25-point repair and a
100-point repair are the same `UKCFRepairEffect` with different `Magnitude` values.

`PeriodSeconds` turns an effect into a periodic one — `TickEffect` fires on a timer, not every frame, which
is how a damage-over-time or fuel-burn effect is built without adding a tick.

`OwningComponent` and `SourcePowerupId` are available inside the effect so it can find its way back to the
component that started it.

### Lifecycle

```
ActivatePowerup(Powerup)
  → cooldown check against GetServerWorldTimeSeconds()
  → StartEffect(Powerup)
      → instantiate EffectClass, ApplyEffect(Target, Magnitude)
      → if PeriodSeconds > 0: timer calling TickEffect
      → if Duration > 0 and Type != Passive: timer calling EndEffect
  → add to replicated ActivePowerups summary
EndEffect(PowerupId)
  → RevertEffect(Target)
  → remove from ActivePowerups, broadcast OnPowerupExpired
```

`Instant` effects apply and are immediately finished — no revert, no entry in `ActivePowerups`.
`Passive` effects apply and stay until `ResetForRespawn()` or `ClearAllEffects()`.

`RevertEffect` must undo exactly what `ApplyEffect` did. The built-in effects are the model:
`UKCFSpeedBoostEffect` registers a named speed multiplier and clears that same name;
`UKCFInvulnerabilityEffect` calls `PushInvulnerability()` then `PopInvulnerability()`. The push/pop pairing
is why two overlapping invulnerability powerups do not cancel each other when the first expires.

### Built-in effects

| Class | Apply | Revert |
| --- | --- | --- |
| `UKCFSpeedBoostEffect` | Named speed multiplier scaled by `Magnitude` | Clears that named multiplier |
| `UKCFShieldEffect` | `AddTemporaryShield(Magnitude)` | Removes it if `bRemoveShieldOnExpiry` |
| `UKCFRepairEffect` | `Heal(Magnitude)` | — (instant) |
| `UKCFInvulnerabilityEffect` | `PushInvulnerability()` | `PopInvulnerability()` |
| `UKCFNitroEffect` | Speed multiplier plus a periodic `TickEffect` | Clears the multiplier |

Subclass any of these in Blueprint and override `ApplyEffect` / `RevertEffect` to build your own.

## Replication

`ActivePowerups` is a `TArray<FKCFActivePowerupInfo>` replicated **unconditionally**, because shields,
nitro trails and invulnerability shimmer have to be visible to opponents. Each entry carries
`FName PowerupId`, `float EndServerTime` and `int32 Stacks` — an id and a timestamp, not an asset pointer
and not a ticking countdown. A client calls `GetRemainingTime(PowerupId)` and does the subtraction itself.

`CollectedCount` is `COND_OwnerOnly`.

Inventory contents are `COND_OwnerOnly` too (see
[Multiplayer.md](Multiplayer.md#components)), so what you are *holding* stays private while what you have
*activated* is public. That asymmetry is intentional: holding a missile should be a surprise, using one
should not be.

## Pickups

`AKCFPowerupPickup` is a sphere trigger plus a mesh.

| Replicated | Condition |
| --- | --- |
| `Powerup` | `COND_InitialOnly` |
| `bCollected` | — (`OnRep_Collected` hides the mesh on clients) |

The server handles the overlap, calls `CollectPowerup`, and sets `bCollected` only if the collection
succeeded. Clients never decide a pickup was taken.

It is the one cosmetic ticking actor in the framework — spinning at `SpinRateDegreesPerSecond` and bobbing
with `BobAmplitude` / `BobFrequency` — and it disables its own tick on a dedicated server, where nobody is
looking at it.

`OnCollected(AActor* Collector, UKCFPowerupDataAsset* Powerup)` and `BP_OnCollected()` are the hooks for
your collection VFX and sound.

## Spawners

`AKCFPowerupSpawner` decides what appears where.

| Property | Default | Meaning |
| --- | --- | --- |
| `PickupClass` | `AKCFPowerupPickup` | The actor to spawn |
| `PowerupPool` | empty | Explicit weighted list of `FKCFWeightedPowerupEntry` |
| `PowerupCatalogue` | none | Alternative: draw from a `FKCFPowerupRow` Data Table |
| `RequiredPowerupTags` | empty | Filters the catalogue by gameplay tag |
| `SpawnInterval` | 12 s | Between spawns while below `MaxActivePickups` |
| `RespawnDelay` | 8 s | After one is collected |
| `InitialSpawnDelay` | 2 s | After spawning starts |
| `MaxActivePickups` | 1 | Live pickups from this spawner |
| `AdditionalSpawnOffsets` | empty | Extra local positions, for a cluster from one actor |
| `bAutoStart` | true | Only honoured when no `AKCFGameMode` is present |

### Pool vs catalogue

An explicit `PowerupPool` gives per-spawner control: the spawner at the back of the pack can be weighted
toward offensive items. Setting `PowerupCatalogue` instead draws from every enabled row using each row's
`DropWeight`, with `RequiredPowerupTags` narrowing it — so a spawner tagged
`KCF.Powerup.Category.Defensive` offers only defensive items without you listing them.

If both are set, `PowerupPool` wins.

### Who starts them

A spawner in a level run by `AKCFGameMode` does **not** start itself:

```cpp
if (HasAuthority() && bAutoStart && !GetWorld()->GetAuthGameMode<AKCFGameMode>())
{
    StartSpawning();
}
```

The game mode enables every spawner when the match enters `InProgress` and disables them (removing live
pickups) at `Finished`, via `SetPowerupSpawnersEnabled(bool, bool bRemoveExistingPickups)`. Nobody collects
powerups during the countdown, and the arena is clean when the scoreboard comes up.

The `bAutoStart` fallback exists so a spawner dropped into a test level with no KCF game mode still works.

`UKCFGameModeDataAsset::bPowerupsEnabled` disables them for the whole match, and
`PowerupSpawnIntervalScale` multiplies every interval — one value to make a mode frantic or sparse.

## Cooldowns

`UKCFPowerupDataAsset::Cooldown` is per-powerup-id and per-kart, checked against server time via
`IsOnCooldown(FName)`. It is separate from `Duration`: a 6-second shield with a 20-second cooldown can be
picked up again immediately but not *used* again for 20 seconds.

## Stacking

`bStackable` with `MaxStacks` lets a player hold several of the same item in one inventory slot.
Separately, `FKCFActivePowerupInfo::Stacks` tracks how many are simultaneously active, so an effect that
should intensify rather than refresh can read it.

Design note: for most effects, re-applying while active should extend the duration rather than stack the
magnitude. Stacking magnitude compounds, and three stacked speed boosts is a kart that cannot turn.

## Death and respawn

`ResetForRespawn()` clears active effects on death when
`UKCFGameModeDataAsset::bClearInventoryOnDeath` is set (the default). `Passive` effects go too — otherwise
they would accumulate across lives for the whole match.

`ClearAllEffects()` runs on `EndPlay` so a destroyed kart does not leave a timer holding a dangling target
pointer.
