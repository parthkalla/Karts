# Blueprint API

Everything a game needs is reachable from Blueprint. This is the reference, grouped by what you are
building rather than by class, because when you are making a health bar you do not care which header the
function is in.

Two conventions throughout:

- **Delegates** (`BlueprintAssignable`) are how you react to change. Bind once, usually in `BeginPlay`.
- **Pure getters** (`BlueprintPure`) are how you read current state. Safe to call every frame.

Prefer binding a delegate over polling a getter on Tick. Every delegate below fires on the client that
needs it, driven by an OnRep or an explicit client RPC.

---

## Global helpers — `UKCFBlueprintLibrary`

Static, available anywhere, all take a world context (Blueprint fills it in automatically).

### Finding things

```
GetKCFGameState(WorldContext)             → AKCFGameState*
GetLocalKartPawn(WorldContext)            → AKCFKartPawn*
GetLocalKCFPlayerState(WorldContext)      → AKCFPlayerState*
```

### Content lookup

```
FindWeaponById(WorldContext, Id)          → UKCFWeaponDataAsset*
FindPowerupById(WorldContext, Id)         → UKCFPowerupDataAsset*
FindVehicleById(WorldContext, Id)         → UKCFVehicleDataAsset*
```

Use these to turn a replicated `FName` into something you can draw an icon from.

### Match state

```
GetMatchState(WorldContext)               → EKCFMatchState
IsMatchActive(WorldContext)               → bool
GetRemainingMatchTime(WorldContext)       → float
GetServerTimeSeconds(WorldContext)        → float
```

`GetServerTimeSeconds` is the clock everything else in the framework uses. If you are building your own
timed mechanic, use it rather than `GetGameTimeSinceCreation` so your timing agrees with the server's.

### Scoreboard and teams

```
GetScoreboard(WorldContext)                     → TArray<AKCFPlayerState*>   (sorted)
GetPlayerPosition(WorldContext, PlayerState)    → int32                      (1-based)
GetTeamAttitude(ActorA, ActorB)                 → EKCFTeamAttitude
GetTeamId(Actor)                                → int32
GetTeamColor(WorldContext, TeamId)              → FLinearColor
GetTeamDisplayName(WorldContext, TeamId)        → FText
```

`GetTeamAttitude` works on any pair of actors that implement `IKCFTeamAgent` — karts, projectiles,
deployables, player states — so a single node answers "should this be red or blue" for all of them.

### Formatting and platform

```
FormatTimeMinutesSeconds(Seconds)         → FText      "2:35"
SpeedToKilometresPerHour(SpeedCmPerSec)   → int32
SpeedToMilesPerHour(SpeedCmPerSec)        → int32
IsMobilePlatform()                        → bool
IsDedicatedServer(WorldContext)           → bool
```

`IsMobilePlatform()` returns true on Android and iOS. On desktop it returns true when the console variable
`KCF.ForceTouchControls` is non-zero, so you can test a touch HUD in PIE:

```
KCF.ForceTouchControls 1
```

### Debug

```
DebugLogKartState(Kart)
DebugLogMatchState(WorldContext)
```

Both dump to `LogKCF`. Useful when a PIE client is behaving differently from the server and you want a
timestamped snapshot from each.

---

## Match UI — `AKCFGameState`

### Delegates

| Delegate | Signature | Use |
| --- | --- | --- |
| `OnMatchStateChanged` | `(EKCFMatchState Old, EKCFMatchState New)` | Switch HUD layers |
| `OnCountdownStarted` | `(float SecondsRemaining)` | Start the 3-2-1 animation |
| `OnMatchStarted` | `()` | Hide the countdown, show the HUD |
| `OnSuddenDeathStarted` | `()` | Banner, music change |
| `OnMatchFinished` | `(EKCFMatchEndReason Reason, int32 WinningTeamId)` | Show the results screen |
| `OnKillFeedEntryAdded` | `(const FKCFKillFeedEntry& Entry)` | Append a kill feed line |
| `OnTeamScoreChanged` | `(int32 TeamId)` | Update one team's score |

### Getters

```
GetMatchState()               → EKCFMatchState
IsMatchActive()               → bool
GetRemainingStateTime()       → float      time left in the current state
GetRemainingMatchTime()       → float      time left in the match
GetFormattedRemainingTime()   → FText      "2:35", ready to bind to a text block
GetTeamScores()               → TArray<FKCFTeamScore>
GetTeamScore(TeamId)          → int32
GetKillFeed()                 → TArray<FKCFKillFeedEntry>
GetSortedPlayerStates()       → TArray<AKCFPlayerState*>
GetParticipantCount()         → int32
GetAlivePlayerCount()         → int32
GetWinningTeamId()            → int32
GetWinningPlayer()            → AKCFPlayerState*
GetMatchEndReason()           → EKCFMatchEndReason
GetGameModeConfig()           → UKCFGameModeDataAsset*
```

`GetKillFeed()` returns the full bounded history, so a widget created mid-match can populate itself
immediately and then bind `OnKillFeedEntryAdded` for subsequent entries.

### State machine queries — `UKCFMatchStateMachine`

Static, no world context needed:

```
IsTransitionAllowed(From, To)   → bool
IsLiveState(State)              → bool    InProgress or SuddenDeath
ShouldPlayersBeSpawned(State)   → bool
IsTerminalState(State)          → bool    Finished or ReturningToLobby
GetStateDisplayName(State)      → FText
```

`IsLiveState` is what to gate "is the player allowed to do things" UI on, rather than comparing against
`InProgress` directly — that comparison silently breaks the moment sudden death is enabled.

---

## Player HUD — `AKCFPlayerController`

### Delegates

| Delegate | Signature | Use |
| --- | --- | --- |
| `OnKillConfirmed` | `(AKCFPlayerState* Victim, FName WeaponId)` | "You eliminated X" |
| `OnHitConfirmed` | `(float DamageDealt, bool bWasLethal)` | Hit marker |
| `OnPossessedKartChanged` | `(AKCFKartPawn* Kart)` | Rebind HUD to a new kart after respawn |
| `OnRespawnAvailable` | `(float SecondsRemaining)` | Show the respawn prompt |
| `OnPauseRequested` | `(bool bPaused)` | Open or close the pause menu |

`OnPossessedKartChanged` is the important one. Every component delegate you bind belongs to a *specific
kart*, and respawning gives the player a new one. Rebind here or your health bar stops updating after the
first death.

### Getters and requests

```
GetKartInput()            → UKCFKartInputComponent*
GetKCFPlayerState()       → AKCFPlayerState*
GetKartPawn()             → AKCFKartPawn*
GetKCFGameState()         → AKCFGameState*
GetRespawnCountdown()     → float
CanRequestRespawn()       → bool

RequestReady(bool bNewReady)
RequestTeamChange(int32 DesiredTeamId)
RequestRespawn()
RequestVehicleSelection(FName VehicleId)
RequestPause(bool bPaused)
RequestKartReset()
```

Every `Request*` sends a validated server RPC. They are requests, not commands: the server may decline, and
nothing on the client changes until the resulting replicated state arrives. Design your UI around that —
grey the button and wait for the state, do not optimistically update it.

---

## Scoreboard — `AKCFPlayerState`

### Delegates

`OnScoreChanged(PlayerState, NewScore)`, `OnTeamChanged(PlayerState, NewTeamId)`,
`OnStatsChanged(PlayerState)`, `OnReadyStateChanged(PlayerState, bIsReady)`,
`OnEliminated(PlayerState)`.

### Getters

```
GetKCFScore()           GetTeamId()            GetKills()
GetDeaths()             GetAssists()           GetTeamKills()
GetDamageDealt()        GetDamageReceived()    GetPowerupsCollected()
GetCurrentKillStreak()  GetBestKillStreak()    GetLivesRemaining()
IsEliminated()          IsReady()              GetPlacement()
GetSelectedVehicleId()  GetKillDeathRatio()    GetLastDeathServerTime()
```

`GetPlacement()` is 0 until the match finishes, then holds the final position (1 = winner).

---

## Kart HUD — `AKCFKartPawn`

```
GetHealthComponent()     GetInventoryComponent()   GetWeaponComponent()
GetPowerupComponent()    GetBoostComponent()       GetCombatComponent()
GetCollisionBox()        GetVehicleData()

GetCurrentSpeed()        → float   cm/s, signed
GetSpeedPercent()        → float   0..1
IsDrifting()             IsBoosting()      IsAirborne()
CanAct()                 IsSpawnProtected()  IsDrivingEnabled()
GetMuzzleTransform()     GetDropTransform()
```

Delegates: `OnDriftStarted`, `OnDriftEnded`, `OnBecameAirborne`, `OnLanded`, `OnVehicleDataApplied`,
`OnSignificantImpact(FVector Location, float Speed)`.

Blueprint events to override in a subclass: `BP_OnDriftChanged(bool)`, `BP_OnBoostChanged(bool)`,
`BP_OnImpact(FVector, float)`, `BP_OnVehicleVisualsReady()`.

---

## Health and shields — `UKCFHealthComponent`

### Delegates

| Delegate | Signature |
| --- | --- |
| `OnHealthChanged` | `(float NewHealth, float MaxHealth, float Delta)` |
| `OnShieldChanged` | `(float NewShield, float MaxShield, float Delta)` |
| `OnDamageTaken` | `(const FKCFDamageInfo& Info, const FKCFDamageResult& Result)` |
| `OnDeath` | `(AActor* Victim, AActor* Killer, const FKCFDamageInfo& FatalDamage)` |
| `OnInvulnerabilityChanged` | `(bool bIsInvulnerable)` |
| `OnRevive` | `()` |

`Delta` is signed, so one handler covers damage and healing — a negative delta flashes the screen red, a
positive one plays the repair effect.

### Getters

```
GetHealth()    GetMaxHealth()   GetHealthPercent()
GetShield()    GetMaxShield()   GetShieldPercent()
GetCombinedDurabilityPercent()      health + shield as one 0..1 bar
IsDead()       IsInvulnerable()  GetActiveTags()
```

### Server-only operations

`Heal`, `RestoreShield`, `AddTemporaryShield`, `PushInvulnerability`, `PopInvulnerability`,
`GrantTimedInvulnerability`, `ClearAllInvulnerability`, `Revive`, `KillImmediately`,
`InitialiseHealth`. All check authority and do nothing on a client.

There is deliberately **no `SetHealth`**. Damage goes through `UKCFDamageSubsystem::ApplyDamage` and
nowhere else; healing goes through `Heal`, which clamps. A direct setter would be the hole in the pipeline.

Push/pop invulnerability rather than a boolean: two overlapping sources each push and each pop, so the
first to expire does not strip the protection the second is still providing.

---

## Items — `UKCFInventoryComponent`

Delegates: `OnInventoryChanged()`, `OnSelectedSlotChanged(int32 SlotIndex)`.

```
GetSlotCount()            → int32
GetInventoryItems()       → TArray<FKCFInventorySlot>
GetItem(SlotIndex)        → FKCFInventorySlot
GetCurrentItem()          → FKCFInventorySlot
GetSelectedSlot()         → int32
HasWeapon(WeaponId)       → bool
HasPowerup(PowerupId)     → bool
IsFull()                  → bool
FindFirstEmptySlot()      → int32
RequestSelectSlot(NewSlot)
```

`FKCFInventorySlot` carries `WeaponId`, `PowerupId` and `Count`. Resolve the id with
`FindWeaponById` / `FindPowerupById` to get an icon.

Inventory is `COND_OwnerOnly`, so these return empty for other players' karts. That is intentional — see
[Multiplayer.md](Multiplayer.md#components).

---

## Firing — `UKCFCombatComponent`

The one component input and UI should talk to.

```
FireCurrentWeapon()       fire, if a weapon is selected
UseCurrentPowerup()       activate, if a powerup is selected
UseSelectedSlot()         whichever it is - for a single-button layout
CycleToNextSlot()
CanFireWeapon()           → bool
CanUsePowerup()           → bool
GetCurrentPowerup()       → UKCFPowerupDataAsset*
IsSelectedSlotAWeapon()   → bool
```

`UseSelectedSlot()` plus `IsSelectedSlotAWeapon()` is what a one-action-button touch HUD needs: one call to
act, one query to pick the icon.

---

## Weapon state — `UKCFWeaponComponent`

Delegates: `OnWeaponChanged(UKCFWeaponDataAsset* NewWeapon)`,
`OnWeaponFired(UKCFWeaponDataAsset* Weapon)`, `OnAmmoChanged(int32 Ammo, int32 MaxAmmo)`.

```
GetCurrentWeapon()      → UKCFWeaponDataAsset*
GetCurrentAmmo()        → int32
CanFireWeapon()         → bool
GetRemainingCooldown()  → float
GetCooldownPercent()    → float   0..1, for a radial fill
```

`BP_OnWeaponFired(Weapon, MuzzleLocation)` is the cosmetic hook for muzzle flash and sound, fired from the
cosmetic multicast on every client.

---

## Powerups — `UKCFPowerupComponent`

Delegates: `OnPowerupCollected(UKCFPowerupDataAsset*)`, `OnPowerupUsed(UKCFPowerupDataAsset*)`,
`OnPowerupExpired(FName PowerupId)`.

```
GetActivePowerups()              → TArray<FKCFActivePowerupInfo>
HasActivePowerup(PowerupId)      → bool
GetRemainingTime(PowerupId)      → float
IsOnCooldown(PowerupId)          → bool
GetCollectedCount()              → int32
```

`BP_OnPowerupActivated(UKCFPowerupDataAsset*)` is the hook for the activation VFX.

### Writing an effect — `UKCFPowerupEffect`

Subclass in Blueprint and override:

```
ApplyEffect(AKCFKartPawn* Target, float Magnitude)
RevertEffect(AKCFKartPawn* Target)
TickEffect(AKCFKartPawn* Target, float DeltaSeconds)    // only when PeriodSeconds > 0
```

`RevertEffect` must undo exactly what `ApplyEffect` did. If you register a speed multiplier, clear the same
named source; if you push invulnerability, pop it.

---

## Boost — `UKCFBoostComponent`

Delegates: `OnBoostStarted()`, `OnBoostEnded()`,
`OnChargesChanged(int32 Charges, int32 MaxCharges)`.

```
GetCharges()            → int32
GetMaxCharges()         → int32
IsBoosting()            → bool
GetChargeProgress()     → float   0..1 toward the next charge
GetBoostTimeRemaining() → float
RequestBoost()
```

---

## Touch input — `UKCFKartInputComponent`

Reach it with `GetKartInput()` on the player controller. Full guide in [Mobile.md](Mobile.md).

```
SetMobileSteering(float AxisValue)      -1..1
SetMobileThrottle(float AxisValue)       0..1
SetMobileMovement(FVector2D AxisValues)  combined stick

StartMobileBrake()   StopMobileBrake()
StartDrift()         StopDrift()
StartBoost()         StopBoost()

FireWeapon()         UsePowerup()        UseSelectedSlot()      CycleSlot()
AddLookInput(FVector2D Delta)            RecenterCamera()       RequestKartReset()
ClearInput()

GetMobileSettings()            → UKCFMobileSettingsDataAsset*
SetMobileSettings(Settings)
IsAutoAccelerationEnabled()    → bool
GetControlledKart()            → AKCFKartPawn*
GetInputState()                → FKCFKartInputState
```

---

## Racing — `UKCFRaceProgressComponent`

On the player state in race modes. Get it with `AKCFRaceGameMode::GetRaceProgress(PlayerState)`.

Delegates: `OnLapCompleted(int32 CompletedLap)`, `OnProgressChanged()`.

```
GetCompletedLaps()          GetNextCheckpointOrdinal()
GetTotalCheckpointsPassed() GetBestLapSeconds()
GetLastLapSeconds()         GetTotalRaceSeconds()
HasFinished()               GetLastCheckpoint()
```

`AKCFRaceGameMode::GetRaceOrder()` returns player states sorted by race position, server-side.

---

## Battle royale — `AKCFPlayZone`

Delegates: `OnPhaseStarted(int32 PhaseIndex, float SecondsUntilShrink)`, `OnZoneChanged()`.

```
GetCurrentRadius()           → float   computed from timestamps, accurate every frame
GetZoneCenter()              → FVector
GetTargetRadius()            → float
GetCurrentPhase()            → int32
GetSecondsUntilShrink()      → float
IsShrinking()                → bool
IsInsideZone(Location)       → bool
GetCurrentDamagePerSecond()  → float
```

`GetCurrentRadius()` interpolates locally from two replicated timestamps, so a minimap ring is smooth
without the server streaming a radius.

---

## Sessions — `UKCFSessionSubsystem`

Delegates: `OnCreateSessionComplete`, `OnFindSessionsComplete`, `OnJoinSessionComplete`,
`OnLeaveSessionComplete`, `OnDestroySessionComplete`.

```
GetServiceName()       → FName
IsServiceAvailable()   → bool
CreateSession(FKCFSessionSettings)
FindSessions(bool bLANOnly, int32 MaxResults)
JoinSession(FKCFSessionSearchResult)
LeaveSession()
DestroySession()
GetLastSearchResults() → TArray<FKCFSessionSearchResult>
```

See [OnlineIntegration.md](OnlineIntegration.md) for what the built-in service does and does not do.

---

## Settings — `UKCFPlayerSettingsSubsystem`

A `ULocalPlayerSubsystem`. Delegate: `OnSettingsChanged()`.

```
GetSettings()                 → UKCFSaveGame*
GetRuntimeMobileSettings()    → UKCFMobileSettingsDataAsset*
SaveSettings()                → bool
ReloadSettings()
ResetToDefaults()
ApplySettings()
```

`UKCFSaveGame` holds steering mode, deadzone and sensitivities, look inversion, auto-accelerate, toggle
drift and boost, haptics, volumes, quality level, frame rate limit, selected vehicle id and display name.

**Local preferences only.** Nothing authoritative is stored here — not score, not unlocks, not currency.
A local save file is trivially editable, so anything that affects a match must live on the server. The
selected vehicle id is stored as a *preference*; the server still validates it against the catalogue before
honouring it.

---

## Server-side extension points

For Blueprint subclasses of `AKCFGameMode`:

| Event | When |
| --- | --- |
| `BP_OnMatchStateChanged(Old, New)` | Every state transition |
| `BP_OnKartDeath(Victim, Killer)` | After scoring is applied |
| `BP_OnPlayerJoined(PlayerState)` | After team assignment |
| `BP_OnPlayerLeft(PlayerState)` | Before cleanup |

And the C++ virtuals worth overriding: `CheckWinCondition`, `ScoreElimination`, `AssignTeamForPlayer`,
`OnPlayerEliminated`, `OnMatchStateChanged`, `AssignFinalPlacements`, `GetRespawnTransformOverride`.

`ForceStartMatch()` and `EndMatch(Reason)` are `BlueprintCallable` for custom flow.
