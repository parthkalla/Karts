# Vehicle System

Karts, movement, the swappable physics backend, and the camera.

## AKCFKartPawn

The pawn is a shell that owns components. It implements `IKCFDamageable` (so the damage subsystem can
target it) and `IKCFTeamAgent` (so friendly fire and team colouring work), and nothing else about it is
gameplay logic.

```
AKCFKartPawn
├── CollisionBox        UBoxComponent               root for the arcade backend
├── SkeletalBody        USkeletalMeshComponent      cosmetic
├── StaticBody          UStaticMeshComponent        cosmetic alternative
├── CameraBoom          UKCFKartCameraComponent     local client only
├── Camera              UCameraComponent            local client only
├── KartMovement        UPawnMovementComponent      concrete class is swappable
├── HealthComponent     UKCFHealthComponent
├── InventoryComponent  UKCFInventoryComponent
├── WeaponComponent     UKCFWeaponComponent
├── PowerupComponent    UKCFPowerupComponent
├── BoostComponent      UKCFBoostComponent
└── CombatComponent     UKCFCombatComponent
```

### State queries

All `BlueprintPure`, all safe to call from a HUD every frame:

```cpp
float GetCurrentSpeed() const;      // cm/s, signed (negative when reversing)
float GetSpeedPercent() const;      // 0..1 against the movement data's MaxSpeed
bool  IsDrifting() const;
bool  IsBoosting() const;
bool  IsAirborne() const;
bool  CanAct() const;               // alive, driving enabled, match live, not mid-respawn
bool  IsSpawnProtected() const;
bool  IsDrivingEnabled() const;
```

`CanAct()` is the one to gate UI on. It folds together death, the countdown lock, and match state, so a
fire button that checks it will not light up during the pre-match countdown.

### Transforms for spawning things

```cpp
FTransform GetMuzzleTransform() const;   // VehicleData->MuzzleSocket, or the nose of the box
FTransform GetDropTransform() const;     // VehicleData->DropSocket, or behind the kart
```

Both fall back to a sensible offset from the collision box when the socket is missing or the mesh is not
set, so a kart with a placeholder cube mesh still fires from roughly the right place.

### Cosmetic hooks

`BlueprintImplementableEvent`s a Blueprint subclass can override, and multicast delegates a HUD can bind:

| Hook | Fires when |
| --- | --- |
| `BP_OnDriftChanged(bool)` | Drift starts or stops |
| `BP_OnBoostChanged(bool)` | Boost starts or stops |
| `BP_OnImpact(FVector, float)` | A collision above the impact threshold |
| `BP_OnVehicleVisualsReady()` | After `VehicleData` has been applied and meshes/materials set |
| `OnDriftStarted` / `OnDriftEnded` / `OnBecameAirborne` / `OnLanded` / `OnVehicleDataApplied` | Delegate equivalents |
| `OnSignificantImpact(FVector, float)` | Impact location and speed, for camera shake or audio |

None of these run on a dedicated server.

### Applying vehicle data

```cpp
void SetVehicleData(UKCFVehicleDataAsset* InVehicleData);
```

Server-side. It pushes the data outward: movement data to the movement component, health and shield maxima
to the health component, boost charges and duration to the boost component, inventory slot count to the
inventory, and then calls the virtual `ApplyVehicleVisuals()` on clients.

`VehicleData` replicates `COND_InitialOnly`, so a kart's identity is sent once at spawn. Changing it
mid-life is not supported — the framework respawns a kart to change its vehicle, which is also what the
genre expects.

`ApplyVehicleVisuals()` is `virtual` so a backend subclass can reroute it; `AKCFChaosKartPawn` overrides it
because its mesh hierarchy is different (see below).

## The arcade movement backend

`UKCFKartMovementComponent` is a `UPawnMovementComponent` that implements `IKCFVehicleMovement`. It is a
kinematic arcade controller, not a physics simulation: it integrates velocity directly and sweeps the
updated component, which gives predictable, tunable handling and costs far less than a wheeled vehicle
solver.

Per frame it:

1. Traces down `GroundTraceDistance` (60 cm) to find ground and its normal.
2. Picks an acceleration from `Acceleration × GetAccelerationMultiplier(SpeedPercent)`, so the curve on the
   movement data can make low-speed pickup snappy and top-end acceleration gradual.
3. Applies steering, interpolated toward the input at `SteeringInterpSpeed`, scaled by
   `TurnRate × GetTurnRateMultiplier(SpeedPercent)` — this is what stops a kart spinning on the spot.
4. Splits velocity into forward and lateral components and bleeds the lateral part toward zero at `Grip`,
   or `DriftGrip` while drifting. That one substitution is the entire drift model.
5. Applies gravity, clamps to `TerminalVelocity`, and reduces control to `AirControl` when not grounded.
6. Aligns the kart to the ground normal at `GroundAlignSpeed`, refusing slopes beyond
   `MaxDrivableSlopeAngle`.
7. Decays accumulated knockback at `KnockbackDecayRate`.
8. Sweeps, and reflects off walls with `WallRestitution`.

### Speed multipliers

```cpp
void SetSpeedMultiplier(FName SourceId, float Multiplier);
void ClearSpeedMultiplier(FName SourceId);
float GetCombinedSpeedMultiplier() const;
```

Named sources, multiplied together. Boost registers under one name, a nitro powerup under another, a tar
patch under a third. Each removes only its own contribution. The alternative — a single float — breaks the
moment two effects overlap, which in kart combat is constantly.

### Drift

Drift engages when the drift input is held *and* speed is at least `MinDriftSpeedPercent` of maximum
(default 35%). While drifting, lateral grip drops to `DriftGrip`, turn rate is multiplied by
`DriftTurnMultiplier`, and `GetDriftVisualYaw()` returns a yaw offset up to `DriftVisualYawOffset` that the
mesh and camera use to lean into the slide. The yaw offset is purely visual — it does not affect where the
kart actually goes.

### Networking

The owning client simulates locally for responsiveness and sends raw input to the server
(`ServerSendInput`, unreliable, validated for NaN and range). The server runs the authoritative
simulation. Simulated proxies receive `ReplicatedInput` (`COND_SkipOwner`) and feed it into their local
movement component so that remote karts steer and drift visually instead of sliding along interpolated
positions.

Correction is smoothed interpolation toward the server position, not rollback. See
[Multiplayer.md](Multiplayer.md#what-the-framework-does-not-do) for what that costs you.

## The Chaos Vehicles backend

`KartCombatFrameworkChaosVehicles` is an optional module containing two classes:

- `UKCFChaosKartMovementComponent` — derives from `UChaosWheeledVehicleMovementComponent` and implements
  `IKCFVehicleMovement`.
- `AKCFChaosKartPawn` — derives from `AKCFKartPawn` and swaps the movement subobject class.

### How the swap works

```cpp
AKCFChaosKartPawn::AKCFChaosKartPawn(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UKCFChaosKartMovementComponent>(
          AKCFKartPawn::KartMovementComponentName))
{
    SkeletalBody->SetupAttachment(nullptr);
    SetRootComponent(SkeletalBody);
    CollisionBox->SetupAttachment(SkeletalBody);
    // CollisionBox becomes query-only; the skeletal mesh's physics asset does the simulating.
}
```

The hierarchy inverts because Chaos needs the skeletal mesh at the root — it drives the body through the
physics asset. The box stays for gameplay queries (pickup overlaps, area effect radii) but no longer
collides.

Every gameplay system keeps working unchanged because it talks to `IKCFVehicleMovement`, not to either
concrete class. `SetThrottleInput` clamps and forwards to the Chaos base; `SetSpeedMultiplier` adjusts the
target top speed rather than scaling velocity directly, because scaling a physics body's velocity fights
the solver.

### What you still have to author

This is the known limitation flagged in the [README](README.md#known-limitations). Chaos needs content the
plugin cannot sensibly ship:

- A skeletal mesh with a wheel bone per wheel and a physics asset.
- `UChaosVehicleWheel` subclasses for each wheel position, with radius, suspension, and friction.
- Engine torque, transmission and steering curves on the movement component.

`ReferenceTopSpeed`, `SpeedMultiplierAcceleration`, `GroundProbeDistance` and
`DriftLateralSpeedThreshold` on the component are the bridge values that map the framework's abstract
notion of "speed percent" and "is drifting" onto the physical vehicle. Tune them after the wheels are set
up.

### Choosing between backends

| | Arcade (`UKCFKartMovementComponent`) | Chaos (`UKCFChaosKartMovementComponent`) |
| --- | --- | --- |
| Handling | Exactly what the curves say | Emergent from physics |
| Tuning | Data asset fields | Physics asset + wheel classes + curves |
| CPU cost | Low | Noticeably higher |
| Mobile | Comfortable at 12 karts | Needs profiling |
| Feel | Arcade, forgiving, consistent | Weighty, surface-sensitive |

Most kart combat games want the arcade backend. Reach for Chaos when suspension travel, weight transfer and
surface behaviour are part of the experience rather than noise.

## Camera

`UKCFKartCameraComponent` derives from `USpringArmComponent` and ticks **only on the locally controlled
client**. It is not constructed at all on a dedicated server.

| Property | Default | Effect |
| --- | --- | --- |
| `AutoCenterDelay` | 1.2 s | Idle time before the camera swings back behind the kart |
| `AutoCenterSpeed` | 3.0 | How fast it swings |
| `PitchLimits` | (-45, 20) | Degrees |
| `FOVInterpSpeed` | 4.0 | Blend rate between base and speed FOV |
| `DriftYawInfluence` | 12.0 | How far the camera leans into a drift |

FOV comes from the vehicle data: `BaseFOV` at rest, interpolating to `MaxSpeedFOV` at top speed, plus
`BoostFOVBonus` while boosting. That widening is most of why boost *feels* fast.

```cpp
void AddLookInput(const FVector2D& LookDelta);  // degrees; cosmetic only
void SnapBehindKart();                           // instant recentre
void SetManagedCamera(UCameraComponent* InCamera);
```

Look input moves the camera and nothing else. It never affects steering, aiming or weapon direction — all
of which the server derives from the kart's own transform. A client cannot gain an advantage by lying about
where it is looking, because nothing reads it.

## Boost

`UKCFBoostComponent` holds charges rather than a meter.

| Vehicle data field | Effect |
| --- | --- |
| `BoostCharges` | How many are held at once |
| `BoostDuration` | Seconds each charge lasts |
| `BoostCooldown` | Seconds to regenerate one charge |
| `BoostMultiplier` | Speed multiplier while active |

`RequestBoost()` on the client sends a parameterless server RPC. The server checks authority, charges and
cooldown against server time, then registers a named speed multiplier with the movement component and sets
a timer to clear it. `bBoosting` replicates unconditionally so every client can show the VFX; `Charges` and
`MaxCharges` are `COND_OwnerOnly` because only you need your own count.

`OnBoostStarted`, `OnBoostEnded` and `OnChargesChanged(int32 Charges, int32 MaxCharges)` are the
delegates to bind a boost HUD to. `GetChargeProgress()` returns 0..1 toward the next charge, for a radial
fill.

## Collision damage

When `UKCFGameModeDataAsset::bCollisionDamageEnabled` is on, impacts above
`UKCFDeveloperSettings::CollisionDamageSpeedThreshold` (900 cm/s) deal
`CollisionDamagePerThousandSpeed` (6) damage per 1000 cm/s of closing speed, routed through the damage
subsystem with `EKCFDamageCategory::Collision`.

Because it uses the same pipeline, a vehicle with a `DamageMultipliers` entry for `Collision` below 1.0 is
genuinely a ramming-resistant heavy — no special case anywhere.
