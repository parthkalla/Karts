# Architecture

This document explains how the plugin is put together and, more usefully, *why*. If you only want to ship
a game, read [QuickStart.md](QuickStart.md) instead. Read this one before you extend the framework or
before you decide it is doing something wrong.

## Module layout

Three modules, with a deliberate dependency direction. Nothing in the core module knows that the other two
exist.

```
KartCombatFramework              Runtime, PreDefault
  Core, CoreUObject, Engine, NetCore, GameplayTags, EnhancedInput   (public)
  PhysicsCore, DeveloperSettings, InputCore                         (private)

KartCombatFrameworkChaosVehicles Runtime, Default
  depends on KartCombatFramework + ChaosVehicles

KartCombatFrameworkEditor        Editor, PostEngineInit
  depends on KartCombatFramework + DataValidation + editor modules
```

`PreDefault` on the core module matters: gameplay types must exist before project modules that subclass
them are loaded. The Chaos module loads at `Default` because it depends on the core module being up.

### What the core module deliberately does *not* depend on

| Not a dependency | Why |
| --- | --- |
| `UMG`, `Slate`, `SlateCore` | The framework ships no widget and draws nothing. HUDs are project Blueprints that bind to delegates. This is what makes a dedicated-server build provably free of widget construction. |
| `ChaosVehicles` | Isolated in its own module so projects using arcade movement never link the vehicle physics solver. |
| `OnlineSubsystem`, `OnlineSubsystemEOS`, `OnlineSubsystemSteam` | Reached only through `IKCFSessionService`. Hard-coding a backend into gameplay would make the plugin un-shippable on any platform you did not pick. |
| `Niagara`, `AudioMixer` | Effects and sounds are `TSoftObjectPtr` fields on Data Assets, spawned through Engine-level helpers on cosmetic paths only. |

If you find yourself adding `UMG` to `KartCombatFramework.Build.cs`, stop: the thing you want almost
certainly belongs in your game module, bound to one of the delegates listed in
[BlueprintAPI.md](BlueprintAPI.md).

## The ownership rule

Every piece of state has exactly one owner, and that owner is on the server.

```
AKCFGameMode        server only, never replicated. Rules, scoring, win conditions, respawn queue.
AKCFGameState       server writes, all clients read. Match state, scores, kill feed, mode config.
AKCFPlayerState     server writes, all clients read. Per-player score, kills, team, lives, placement.
AKCFPlayerController one per client. Owns input; relays player intent to the server as RPCs.
AKCFKartPawn        server writes gameplay state; clients own only their raw input values.
```

A client never writes a gameplay-critical value. It sends *intent* ("I pressed fire") and the server
decides what that means. See [Multiplayer.md](Multiplayer.md) for the complete list of RPCs and the
validation each one performs.

## Composition over inheritance

`AKCFKartPawn` is a shell. The behaviour lives in components, so that a project can subclass the pawn in
Blueprint and still get every system, or attach the same components to something that is not a kart.

```
AKCFKartPawn
├── UBoxComponent                 CollisionBox       collision + movement root (arcade backend)
├── USkeletalMeshComponent        SkeletalBody       cosmetic, hidden on dedicated server
├── UStaticMeshComponent          StaticBody         cosmetic alternative
├── UKCFKartCameraComponent       CameraBoom         local client only, not created on server
├── UCameraComponent              Camera             local client only
├── UPawnMovementComponent        KartMovement       typed as the base; the concrete class is swappable
├── UKCFHealthComponent           HealthComponent    health, shield, invulnerability, death
├── UKCFInventoryComponent        InventoryComponent slots holding weapon/powerup ids
├── UKCFWeaponComponent           WeaponComponent    firing, ammo, cooldown, projectile spawning
├── UKCFPowerupComponent          PowerupComponent   collection, activation, timed effects
├── UKCFBoostComponent            BoostComponent     charges, duration, cooldown
└── UKCFCombatComponent           CombatComponent    routes one "use" intent to weapon or powerup
```

`UKCFCombatComponent` exists so that UI and input have one thing to talk to. A touch button calls
`UseSelectedSlot()`; the combat component works out whether the selected inventory slot holds a weapon or a
powerup and forwards to the right component. Without it, every HUD would have to reimplement that decision.

### Swapping the movement backend

`KartMovement` is declared as `TObjectPtr<UPawnMovementComponent>` and created with the two-argument form:

```cpp
const FName AKCFKartPawn::KartMovementComponentName(TEXT("KartMovement"));

KartMovement = CreateDefaultSubobject<UPawnMovementComponent, UKCFKartMovementComponent>(
    KartMovementComponentName);
```

A subclass changes the concrete class without touching the pawn:

```cpp
AKCFChaosKartPawn::AKCFChaosKartPawn(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UKCFChaosKartMovementComponent>(
          AKCFKartPawn::KartMovementComponentName))
```

Gameplay code never casts to either concrete class. It calls through `IKCFVehicleMovement`:

```cpp
SetThrottleInput / SetSteeringInput / SetBrakeInput / SetDriftInput
SetSpeedMultiplier / ClearSpeedMultiplier    (named sources, so boost and nitro compose)
ApplyKnockback / SetMovementEnabled / ResetVehicle
GetForwardSpeed / GetSpeedPercent / IsGrounded / IsDrifting / IsMovementEnabled
```

`SetSpeedMultiplier` takes an `FName SourceId` rather than a raw float for a specific reason: boost, a nitro
powerup, and a slow-field hazard can all be active at once, and each must be able to remove *its own*
contribution when it expires without clobbering the others.

## Subsystems

| Subsystem | Scope | Responsibility |
| --- | --- | --- |
| `UKCFDamageSubsystem` | World (server) | The only path by which damage is applied. Owns the assist ledger. |
| `UKCFActorPoolSubsystem` | World | Recycles short-lived actors (projectiles, pickups) via `IKCFPoolableActor`. |
| `UKCFContentRegistrySubsystem` | Game instance | Resolves `FName` ids to Data Assets through the catalogue Data Tables. |
| `UKCFSessionSubsystem` | Game instance | Holds the active `IKCFSessionService` and exposes it to Blueprint. |
| `UKCFPlayerSettingsSubsystem` | Local player | Local-only settings: steering mode, sensitivity, volumes, quality. |

World subsystems that are server-side (`UKCFDamageSubsystem`) override `DoesSupportWorldType` so they are
not created in editor-preview or inactive worlds.

### Why ids instead of object pointers on the wire

Inventory slots, kill feed entries and damage records carry `FName WeaponId` / `FName PowerupId`, not
`UKCFWeaponDataAsset*`. Replicating an object pointer forces the asset to be loaded and net-relevant on
every client that receives it; replicating a name costs a few bytes and lets each client resolve lazily
through `UKCFContentRegistrySubsystem::FindWeapon()` only if it actually needs to draw an icon. A dedicated
server never resolves them at all.

## Actor pooling

`UKCFActorPoolSubsystem` recycles actors instead of spawning and destroying them:

```cpp
AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform,
                     AActor* Owner, APawn* InstigatorPawn);
void    ReleaseActor(AActor* Actor);
void    PrewarmPool(TSubclassOf<AActor> ActorClass, int32 Count);
```

Actors can implement `IKCFPoolableActor` to get `OnAcquiredFromPool` / `OnReturnedToPool` and reset their
own state. Both operations degrade safely: with pooling disabled in Project Settings, `AcquireActor` is a
normal spawn and `ReleaseActor` is a destroy, so correctness never depends on it.

**The framework's own projectiles and pickups deliberately do not use it**, and the reason is worth
understanding before you decide they should.

`AKCFProjectile` and `AKCFPowerupPickup` replicate their defining data (`WeaponData`, `Powerup`, `TeamId`)
with `COND_InitialOnly`. The engine sends an initial-only property once per actor channel. A recycled
actor keeps its net identity, so a client that still holds that channel would never be told the projectile
is now a different weapon — it would keep rendering the previous one.

The fix is not a cleverer pool; it is giving up `COND_InitialOnly` and replicating the configuration
unconditionally with a RepNotify. Paying that replication cost on every projectile, forever, to save a
spawn is the wrong trade for the common case.

So the pool is the right tool for actors you spawn yourself that are either not replicated (cosmetic
debris, decals, local audio emitters) or that carry no initial-only state. If you do want pooled
projectiles, subclass `AKCFProjectile`, replicate its configuration unconditionally with a RepNotify that
re-applies visuals, and route spawning through `AcquireActor` / `ReleaseActor`.

Projectile *count* is bounded separately and unconditionally by
`UKCFDeveloperSettings::MaxLiveProjectilesPerPlayer` (default 8), which is a denial-of-service guard rather
than a performance optimisation. It applies whether or not pooling is on.

## Synchronised time

Cooldowns, countdowns, respawn timers and the battle royale zone are all expressed as *server timestamps*,
read through `AGameStateBase::GetServerWorldTimeSeconds()` and exposed as
`UKCFBlueprintLibrary::GetServerTimeSeconds()`.

This is why `AKCFGameState` replicates `StateEndServerTime` rather than a ticking `RemainingSeconds`. One
replicated float, written once per state change, gives every client a countdown that is correct to within
clock sync — instead of a value updated ten times a second that is still wrong between updates.

The same trick carries the zone in `AKCFPlayZone`: two timestamps and two radii, and every client computes
the current radius itself.

```cpp
const float Alpha = (Now - ShrinkStartServerTime) / (ShrinkEndServerTime - ShrinkStartServerTime);
return FMath::Lerp(StartRadius, TargetRadius, Alpha);
```

## The tick budget

Tick is off by default everywhere. Exactly six classes turn it on, and each one has a reason that could not
be served by a timer:

| Class | Why it ticks | Where it runs |
| --- | --- | --- |
| `AKCFKartPawn` | Reads input state and drives drift/boost/airborne state transitions per frame | Server and owning client |
| `UKCFKartMovementComponent` | It is a movement simulation; per-frame integration is the job | Server and owning client |
| `UKCFChaosKartMovementComponent` | The Chaos solver requires its component tick | Server and owning client |
| `UKCFKartCameraComponent` | Smooth camera lag, FOV blending and auto-centre | Locally controlled client only; never constructed on a dedicated server |
| `UKCFKartInputComponent` | Applies steering return-to-centre and the fire input buffer | Locally controlled client only |
| `AKCFPowerupPickup` | Cosmetic spin and bob. Disables its own tick on a dedicated server | Clients only |

Everything else is timers and delegates: respawn queueing (`UKCFRespawnComponent`, a 0.25 s serviced
queue), match state advancement (`AKCFGameMode`), shield regeneration (`UKCFHealthComponent`), powerup
expiry (`UKCFPowerupComponent`), boost duration and recharge (`UKCFBoostComponent`), powerup spawn
intervals (`AKCFPowerupSpawner`), deployable arming (`AKCFDeployable`), and zone phases (`AKCFPlayZone`).

A twelve-player match therefore ticks twelve pawns and twelve movement components on the server, and
nothing else.

## Data flow: firing a weapon

Worth following end to end, because every other interaction has the same shape.

```
1. Client   Touch button or Enhanced Input action calls UKCFKartInputComponent::FireWeapon().
2. Client   Input component forwards to UKCFCombatComponent::FireCurrentWeapon().
3. Client   Combat component checks CanFireWeapon() locally - purely to avoid wasted RPCs - and
            calls the server RPC. The RPC takes NO PARAMETERS.
4. Server   UKCFWeaponComponent::ServerFireWeapon_Validate() runs cheap structural checks.
5. Server   ServerFireWeapon_Implementation() re-derives everything: which weapon is selected (from the
            replicated inventory), whether the cooldown has elapsed (from server time), whether ammo
            remains, whether the match is live, whether spawn protection forbids it.
6. Server   Spawns the projectile / runs the trace / places the deployable. Decrements ammo, sets the
            next fire time, consumes the inventory slot if the weapon is now empty.
7. Server   Multicasts a cosmetic-only event (weapon id + muzzle location) for VFX and audio.
8. Clients  Bound Blueprints play the muzzle flash and sound. No gameplay state comes from the multicast.
```

Step 3 is the whole security model in one line. Because the RPC carries no weapon, no direction and no
damage, a modified client has nothing to lie about. The worst it can do is ask to fire more often than it
should, and step 5 rejects that against server time.

## Error handling

The framework never crashes on bad content. Three layers:

1. **Editor validation.** `UKCFDataAssetValidator` and `UKCFDataTableValidator` run inside the engine's
   Data Validation framework, so `ValidateConfiguration()` failures surface on save and in a content audit.
2. **Runtime fallbacks.** A game mode with no `GameModeConfig` builds a transient default rather than
   null-dereferencing. A kart with no `VehicleData` keeps working with engine defaults. Both log a warning
   through the `LogKCF` category.
3. **Rejected RPCs.** Validation failures log at `Verbose` by default and at `Warning` when
   `UKCFDeveloperSettings::bLogRejectedRPCs` is on, so you can see cheating attempts (and your own bugs)
   without spamming shipping logs.

`check()` is reserved for invariants the framework itself guarantees. Content mistakes produce logs and
degraded behaviour, never an assert.

## Where to extend

| You want to | Do this | Don't do this |
| --- | --- | --- |
| New game mode | Subclass `AKCFGameMode` (C++ or BP), override `CheckWinCondition` | Edit `AKCFGameMode` |
| New weapon | Author a `UKCFWeaponDataAsset`; subclass `AKCFProjectile` only for novel flight behaviour | Add an enum case |
| New powerup | Subclass `UKCFPowerupEffect`, author a `UKCFPowerupDataAsset` | Add to `EKCFPowerupType` |
| Different physics | Implement `IKCFVehicleMovement`, use `SetDefaultSubobjectClass` | Replace `AKCFKartPawn` |
| Real matchmaking | Implement `IKCFSessionService` in your game module | Add `OnlineSubsystem` to the core module |
| A HUD | Bind to the delegates in [BlueprintAPI.md](BlueprintAPI.md) | Add `UMG` to the core module |
