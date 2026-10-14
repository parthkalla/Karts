# Multiplayer

The framework assumes every client is hostile. This document states the rule, then lists every replicated
property and every RPC with the validation it performs, so you can audit the claim rather than take it on
faith.

## The rule

> A client sends *intent*. The server decides what that intent means, using only state the server already
> owns.

Concretely, the client may never supply: damage, score, kills, inventory contents, weapon ownership,
cooldown state, match result, health, currency, or any position-sensitive gameplay result. If a value
affects the outcome of the match, the server computes it.

The strongest expression of this is that the gameplay-critical server RPCs take **no parameters at all**:

```cpp
UFUNCTION(Server, Reliable, WithValidation) void ServerFireWeapon();
UFUNCTION(Server, Reliable, WithValidation) void ServerUsePowerup();
UFUNCTION(Server, Reliable, WithValidation) void ServerCycleSlot();
UFUNCTION(Server, Reliable, WithValidation) void ServerRequestBoost();
UFUNCTION(Server, Reliable, WithValidation) void ServerRequestRespawn();
UFUNCTION(Server, Reliable, WithValidation) void ServerRequestKartReset();
```

There is no field in which to put a lie. A modified client cannot say *which* weapon, in *which*
direction, for *how much* damage, because the message does not carry those things. The server re-derives
all of them from the inventory it replicated to that client in the first place.

## Authority map

| Actor | Exists on | Replicates | Writes |
| --- | --- | --- | --- |
| `AKCFGameMode` | Server only | Never | Server |
| `AKCFGameState` | Everywhere | Yes | Server |
| `AKCFPlayerState` | Everywhere | Yes | Server |
| `AKCFPlayerController` | Server + its own client | Owner-only properties | Server |
| `AKCFKartPawn` | Everywhere | Yes | Server (except raw input) |
| `AKCFProjectile` | Everywhere | Spawn + initial data | Server |
| `AKCFPowerupPickup` | Everywhere | Yes | Server |
| `AKCFPlayZone` | Everywhere | Yes | Server |
| `UKCFDamageSubsystem` | Everywhere, acts only on server | n/a | Server |

## Replicated properties

Everything below is `UPROPERTY(Replicated)` or `ReplicatedUsing`, registered in
`GetLifetimeReplicatedProps()`. `COND_OwnerOnly` means other clients never receive it;
`COND_InitialOnly` means it is sent once at spawn and never again.

### AKCFGameState

| Property | Condition | Notes |
| --- | --- | --- |
| `MatchState` | — | `ReplicatedUsing=OnRep_MatchState`, drives all client-side match UI |
| `StateEndServerTime` | — | Written once per state change; clients derive countdowns from it |
| `GameModeConfig` | — | So clients can read score limits, team definitions, respawn rules |
| `TeamScores` | — | `OnRep_TeamScores` fires `OnTeamScoreChanged` per changed team |
| `KillFeed` | — | Bounded ring of `FKCFKillFeedEntry`; carries `FName WeaponId`, not an asset pointer |
| `WinningTeamId`, `WinningPlayer`, `MatchEndReason` | — | Written once, at `Finished` |

Note what is *not* here: there is no replicated `RemainingSeconds`. See
[Architecture.md](Architecture.md#synchronised-time).

### AKCFPlayerState

`KCFScore`, `TeamId` (`OnRep_TeamId`), `Kills`, `Deaths`, `Assists`, `TeamKills`, `PowerupsCollected`,
`CurrentKillStreak`, `BestKillStreak`, `DamageDealt`, `DamageReceived`, `LivesRemaining`, `bEliminated`
(`OnRep_Eliminated`), `bReady`, `Placement`, `SelectedVehicleId` — all unconditional, because scoreboards
need every player's stats.

`LastDeathServerTime` is `COND_OwnerOnly`: only you need to know precisely when you died, and broadcasting
it to everyone would leak respawn timing.

### AKCFPlayerController

`RespawnAvailableServerTime` is `COND_OwnerOnly`. `GetRespawnCountdown()` turns it into a number for your
HUD without the server pushing a per-frame value.

### AKCFKartPawn

| Property | Condition | Why |
| --- | --- | --- |
| `VehicleData` | `COND_InitialOnly` | A kart's vehicle never changes mid-life. Sending it once is enough. |
| `bDrivingEnabled` | — | `OnRep_DrivingEnabled` locks movement during countdown and after the match |
| `ReplicatedInput` | `COND_SkipOwner` | Simulated proxies need it for visual steering and drift; the owner already has it locally |

`COND_SkipOwner` on `ReplicatedInput` matters: without it the server would echo every client's own input
back at them, wasting bandwidth and risking visual fighting with their local prediction.

### Components

| Component | Replicated | Condition |
| --- | --- | --- |
| `UKCFHealthComponent` | `Health`, `MaxHealth`, `Shield` (`OnRep_Shield`), `MaxShield`, `bDead`, `InvulnerabilityStacks` (`OnRep_Invulnerability`), `ActiveTags` | All unconditional — enemy health bars are a genre expectation |
| `UKCFInventoryComponent` | `Slots`, `SelectedSlot` (`OnRep_SelectedSlot`) | **`COND_OwnerOnly`** |
| `UKCFWeaponComponent` | `NextFireTime` | `COND_OwnerOnly` |
| `UKCFBoostComponent` | `bBoosting` | Unconditional — other players see the boost VFX |
| `UKCFBoostComponent` | `Charges`, `MaxCharges` | `COND_OwnerOnly` |
| `UKCFPowerupComponent` | `ActivePowerups` | Unconditional — shields and nitro are visible |
| `UKCFPowerupComponent` | `CollectedCount` | `COND_OwnerOnly` |
| `UKCFRaceProgressComponent` | `CompletedLaps`, `NextCheckpointOrdinal`, `TotalCheckpointsPassed`, `BestLapSeconds`, `LastLapSeconds`, `TotalRaceSeconds`, `bFinished` | Unconditional — position tables need everyone's progress |

**Inventory is owner-only on purpose.** If every client knew every other client's held item, a modified
client would have a free wallhack-equivalent: perfect information about incoming threats. Other players
learn what you have when you use it.

### World actors

| Actor | Replicated |
| --- | --- |
| `AKCFPowerupPickup` | `Powerup` (`COND_InitialOnly`), `bCollected` (`OnRep_Collected`) |
| `AKCFProjectile` | `WeaponData`, `TeamId` (both `COND_InitialOnly`) — flight is driven by `UProjectileMovementComponent` replication |
| `AKCFDeployable` | `WeaponData`, `TeamId` (`COND_InitialOnly`), `bArmed` |
| `AKCFPlayZone` | `ZoneCenter`, `StartRadius`, `TargetRadius`, `ShrinkStartServerTime`, `ShrinkEndServerTime`, `CurrentPhase`, `bZoneActive` |

The play zone is the clearest example of the timestamp pattern: seven values written at each phase change
carry a continuously shrinking circle, instead of a radius replicated every frame.

## Every RPC

### Server RPCs (client → server)

| RPC | Reliability | `_Validate` checks | `_Implementation` re-derives |
| --- | --- | --- | --- |
| `AKCFKartPawn::ServerSendInput(FKCFKartInputState)` | Unreliable | All floats finite, `\|Steering\| ≤ 1`, `Throttle` and `Brake` in `[0,1]` | Nothing — the server runs its own simulation from the input |
| `UKCFWeaponComponent::ServerFireWeapon()` | Reliable | Nothing to check (no parameters) | Selected weapon from the server's inventory, cooldown vs server time, ammo, match state, spawn protection |
| `UKCFCombatComponent::ServerUsePowerup()` | Reliable | — | Selected slot, whether it holds a powerup, cooldown, whether the kart can act |
| `UKCFCombatComponent::ServerCycleSlot()` | Reliable | — | Next occupied slot in the server's inventory |
| `UKCFInventoryComponent::ServerRequestSelectSlot(int32)` | Reliable | `NewSlot ∈ [KCF_NO_SLOT, 16)` | Whether that index exists on *this* inventory |
| `UKCFBoostComponent::ServerRequestBoost()` | Reliable | — | Authority, charges remaining, cooldown vs server time, kart can act |
| `AKCFPlayerController::ServerSetReady(bool)` | Reliable | A bool has no invalid value | Player state exists, match is in a state where readiness means anything |
| `AKCFPlayerController::ServerRequestTeamChange(int32)` | Reliable | `DesiredTeamId ∈ [KCF_NO_TEAM, 64)` | Game mode exists, mode is a team mode, team selection allowed, resulting imbalance within `MaxTeamSizeImbalance` |
| `AKCFPlayerController::ServerRequestRespawn()` | Reliable | — | Dead, respawn delay elapsed, lives remaining, respawn enabled in this state |
| `AKCFPlayerController::ServerRequestVehicleSelection(FName)` | Reliable | — | The id exists in the vehicle catalogue and is unlocked; applied at next spawn, never mid-life |
| `AKCFPlayerController::ServerRequestKartReset()` | Reliable | — | Kart exists, is stuck or allowed to reset, match is live |

The split between `_Validate` and `_Implementation` is deliberate and consistent:

- `_Validate` does **structural** checks only — NaN, range, array bounds. Returning `false` disconnects the
  client, which is the right response to a malformed packet but the wrong response to a player who pressed
  fire 10 ms early.
- `_Implementation` does **gameplay** checks and silently ignores failures (logging through
  `LogRejectedRequest` when `UKCFDeveloperSettings::bLogRejectedRPCs` is on). Latency and packet ordering
  make early requests normal; they are not evidence of cheating.

### Client RPCs (server → one client)

| RPC | Reliability | Carries |
| --- | --- | --- |
| `ClientNotifyKill(AKCFPlayerState* Victim, FName WeaponId)` | Reliable | Kill confirmation for your HUD. The score was already replicated; this is the *notification* |
| `ClientNotifyHit(float DamageDealt, bool bWasLethal)` | Unreliable | Hit marker feedback. Unreliable because a dropped hit marker is cosmetic |
| `ClientNotifyRespawnAvailable(float SecondsUntilRespawn)` | Reliable | Prompts the respawn UI |
| `UKCFWeaponComponent::ClientFireRejected()` | Reliable | Lets the client clear a predicted fire animation the server refused |

### Multicasts (server → everyone)

Only three, and all three are cosmetic:

| Multicast | Reliability | Carries |
| --- | --- | --- |
| `UKCFWeaponComponent::MulticastWeaponFired(UKCFWeaponDataAsset*, FVector)` | Unreliable | Muzzle flash and sound |
| `AKCFProjectile::MulticastExplode(FVector, FVector)` | Reliable | Explosion VFX location and normal |
| `AKCFDeployable::MulticastTriggered(FVector)` | Reliable | Trigger VFX |

**No persistent state is ever carried by a multicast.** A multicast is not delivered to clients that join
afterwards or that were not net-relevant at the time, so anything durable must be a replicated property.
Health, ammo, score, match state and zone radius are all properties for exactly this reason.

The damage *result* of an explosion is applied server-side through `UKCFDamageSubsystem::ApplyRadialDamage`
before the multicast goes out. The multicast draws the explosion; it does not cause it.

## The damage pipeline

Every point of damage in the framework — bullets, explosions, collisions, the out-of-bounds zone, kill
volumes, environmental hazards — goes through one function:

```cpp
FKCFDamageResult UKCFDamageSubsystem::ApplyDamage(AActor* Target, const FKCFDamageInfo& DamageInfo);
```

There is no second path. The ordered checks, each with its own rejection reason:

| Order | Check | Rejection |
| --- | --- | --- |
| 1 | Called on the server | `NoAuthority` |
| 2 | `DamageInfo` is structurally valid and non-zero | `ZeroDamage` |
| 3 | Target implements `IKCFDamageable` | `TargetNotDamageable` |
| 4 | Target is alive | `TargetAlreadyDead` |
| 5 | Match is in a live state | `MatchNotLive` |
| 6 | Friendly fire allowed (or scaled by `FriendlyFireScale`) | `FriendlyFireDisabled` |
| 7 | Target not invulnerable, unless the damage ignores invulnerability | `TargetInvulnerable` |
| 8 | Per-category multiplier from `UKCFVehicleDataAsset::DamageMultipliers` | — |
| 9 | Result is still non-zero after scaling | `ZeroDamage` |
| 10 | Shield absorbs first, then health | — |
| 11 | Knockback applied through `IKCFVehicleMovement::ApplyKnockback` | — |
| 12 | Assist ledger updated, statistics credited | — |

Calling `ApplyDamage` on a client returns `FKCFDamageResult::Rejected(NoAuthority)` and does nothing. That
is the single check that makes the whole model hold: there is no way to route damage around it, because
`UKCFHealthComponent` has no public "set health" entry point that skips the pipeline.

`FKCFDamageResult` tells the caller exactly what happened — `ShieldDamage`, `HealthDamage`, whether it was
lethal, and `RejectReason` — so UI and AI can react without guessing.

### The assist ledger

The subsystem keeps a per-victim history of who damaged them and when. It is bounded two ways: entries
older than `UKCFDeveloperSettings::AssistWindowSeconds` (default 10 s) are pruned on access, and the whole
ledger for a victim is dropped when they die. Without both, a long match leaks memory.

`CollectAssisters()` returns players who contributed at least `AssistDamageFraction` (default 20%) within
the window, excluding the killer and the victim.

## Relevancy, frequency and dormancy

Configured through `UKCFDeveloperSettings` so a project can tune them without recompiling:

| Setting | Default | Effect |
| --- | --- | --- |
| `KartNetUpdateFrequency` | 60 | Kart `NetUpdateFrequency` |
| `KartMinNetUpdateFrequency` | 15 | Floor when adaptive net update frequency throttles a distant kart |
| `GameStateNetUpdateFrequency` | 10 | The game state changes rarely; 10 Hz is generous |
| `KartNetCullDistance` | 60000 | 600 m. Karts beyond this are not relevant |

Static world actors (`AKCFPowerupSpawner`, `AKCFRaceCheckpoint`) do not replicate at all — they are
placed-in-level actors that exist identically on every machine. `AKCFPowerupPickup` replicates only its
identity and its collected flag.

### Replication Graph

The framework does not install a replication graph, but it is written to be compatible with one. Karts are
spatially distributed and cullable; the game state and player states are always-relevant; pickups are
spatial and low-frequency. A project adding `UReplicationGraph` would put them in those three buckets with
no changes to plugin code.

## Joining, leaving and reconnecting

**Join in progress** is controlled by `UKCFGameModeDataAsset::bAllowJoinInProgress`. When enabled,
`PostLogin` assigns a team (via the overridable `AssignTeamForPlayer`) and queues a spawn; when disabled,
the player is held as a spectator until the next match.

**Leaving** removes the player's state and, if the remaining count drops below `AbortBelowPlayerCount`,
ends the match with `EKCFMatchEndReason::NotEnoughPlayers`.

**Reconnecting** relies on `AKCFPlayerState::CopyProperties` / `OverrideWith`, which carry score, kills,
team and lives across a seamless travel or a re-login. Both are overridden to include the framework's own
fields — the engine's base implementation would silently drop them.

**Seamless travel** is on (`bUseSeamlessTravel = true`) so a match-to-match transition keeps connections
alive rather than forcing a full reconnect.

## What the framework does not do

Stated plainly so nobody discovers it the hard way:

- **No rollback prediction.** The owning client simulates its kart immediately and the server corrects it
  with smoothed interpolation. There is no replay of buffered inputs. At typical arcade latencies this
  feels fine; at 200 ms in a competitive racer it will not. The `IKCFVehicleMovement` interface is the
  seam where you would add it.
- **No lag compensation for hitscan.** Server-side traces run against present-time positions, not rewound
  ones. For projectile weapons — which is what most kart combat uses — this is unnoticeable. If you build a
  precision hitscan weapon, you will want to add rewind.
- **No anti-cheat beyond authority.** Server authority stops a client from *asserting* a result. It does
  not stop aimbots, which only make legitimate requests. That is a different problem with a different
  solution.
