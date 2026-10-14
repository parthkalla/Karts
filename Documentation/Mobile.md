# Mobile

Android and iOS are first-class targets, not an afterthought. The framework assumes a touch player will be
driving with a thumb on a screen that might be 4.7" or 13", and it ships no widget of its own so that your
touch HUD is entirely yours.

Two principles shape everything below:

1. **The C++ never knows what your HUD looks like.** `UKCFKartInputComponent` exposes a Blueprint-callable
   API; a widget calls it. There is no required widget class, no required layout, no hard-coded resolution
   or safe-area assumption.
2. **Touch input is still just input.** It goes through exactly the same validated path as a gamepad, so
   nothing about touch weakens the authority model in [Multiplayer.md](Multiplayer.md).

## The input API

Everything is `BlueprintCallable` on `UKCFKartInputComponent`, reachable via
`GetPlayerController() → GetKartInput()`.

### Axes

```
SetMobileSteering(float AxisValue)        // -1 (full left) .. 1 (full right)
SetMobileThrottle(float AxisValue)        //  0 .. 1
SetMobileMovement(FVector2D AxisValues)   // X = steering, Y = throttle; for a virtual stick
```

Call these from your widget's drag handler. Values are clamped and sanitised — out-of-range or NaN input
is corrected rather than propagated.

### Held actions

```
StartMobileBrake()   StopMobileBrake()
StartDrift()         StopDrift()
StartBoost()         StopBoost()
```

Paired start/stop so a button can be press-and-hold. When `bDriftIsToggle` or `bBoostIsToggle` is set on
the mobile settings, the input component interprets the pair as a toggle instead, and your widget does not
change.

### One-shot actions

```
FireWeapon()         UsePowerup()        UseSelectedSlot()      CycleSlot()
AddLookInput(FVector2D Delta)            RecenterCamera()       RequestKartReset()
ClearInput()
```

`UseSelectedSlot()` is the single-action-button design: one button that fires a weapon or triggers a
powerup depending on what is held. Pair it with `UKCFCombatComponent::IsSelectedSlotAWeapon()` to pick the
icon.

`ClearInput()` zeroes everything. Call it when a menu opens, or the kart keeps the last steering value
while the player is reading the pause screen.

---

## Steering modes

`EKCFSteeringMode` on the mobile settings asset. All four are implemented; pick in a settings screen and
let players choose.

| Mode | How it reads | Suits |
| --- | --- | --- |
| `Direct` | Absolute: where the thumb is *is* the steering value | Virtual stick, steering wheel widget |
| `Relative` | Delta from where the thumb first touched, over `RelativeSteeringTravel` of screen width | Touch-anywhere, no fixed control position |
| `Tilt` | Device roll, linear from `TiltNeutralDegrees` to `TiltFullLockDegrees` | Accelerometer steering |
| `ScreenHalves` | Left half of the screen is left, right half is right | Simplest possible, two-thumb play |

`Relative` is usually the best default. The thumb never has to find a fixed widget, which matters when the
player is looking at the track rather than the controls.

For `Tilt`, read the device accelerometer in your Blueprint (`Get Input Motion State`) and feed the roll
through `SetMobileSteering`. The framework does not poll the sensor itself — sensor access policy and
calibration UI are project decisions.

### The steering curve

```cpp
float UKCFMobileSettingsDataAsset::ProcessSteeringAxis(float RawAxis) const;
```

Applied in order:

1. **Deadzone** (`SteeringDeadzone`, 0.08). Below it, output is zero. Thumbs rest; this stops drift.
2. **Exponent** (`SteeringExponent`, 1.6). `pow(|x|, exp)` preserving sign. Values above 1 make small
   movements finer and keep full lock reachable at the edge.
3. **Sensitivity** (`SteeringSensitivity`, 1.0), then clamp to [-1, 1].

`SteeringReturnSpeed` (6.0) controls how fast steering recentres when the thumb lifts. Instant recentring
feels twitchy; this interpolates.

The function is public so a settings screen can draw the curve live as the player moves the sliders.

---

## Auto-acceleration

```
bAutoAccelerate            true    throttle is held automatically
AutoAccelerateThrottle     1.0     the value to hold
bAutoAccelerateYieldsToBrake true  braking overrides it
```

On by default, because a mobile player has two thumbs and steering plus fire plus boost already uses them.
`IsAutoAccelerationEnabled()` tells your HUD whether to show a throttle control at all.

With `bAutoAccelerateYieldsToBrake`, the brake button both brakes and reverses, which is the behaviour
players expect from a single pedal.

---

## Toggles and buffering

| Setting | Default | Effect |
| --- | --- | --- |
| `bDriftIsToggle` | false | Tap to start, tap to stop, instead of holding |
| `bBoostIsToggle` | true | Tap to boost; boost has a fixed duration anyway |
| `FireInputBufferSeconds` | 0.25 | A tap during cooldown fires when the cooldown ends |
| `bFireRepeatsWhileHeld` | true | Holding fire repeats at the weapon's fire interval |

`FireInputBufferSeconds` is worth understanding. A player taps fire 100 ms before the cooldown expires; without
buffering that tap is lost and the game feels unresponsive. With it, the input component remembers the tap
and sends the request the moment firing becomes legal. The *server* still validates the cooldown — the
buffer only delays the client's request, it does not let it fire early.

---

## Camera

| Setting | Default | Effect |
| --- | --- | --- |
| `LookSensitivity` | 1.0 | Multiplier on `AddLookInput` |
| `bInvertLookY` | false | |
| `bAutoCenterCamera` | true | Swing back behind the kart when idle |
| `AutoCenterDelaySeconds` | 1.2 | Idle time before it does |

Auto-centre matters more on touch than on a gamepad: a player who swipes to look behind has no stick to
return, so without it the camera stays where they left it.

Look input is cosmetic. It never affects aiming or weapon direction — see
[VehicleSystem.md](VehicleSystem.md#camera).

---

## Haptics

`bHapticsEnabled` (true) and `HapticIntensity` (0.6) are stored for you to read; the framework does not
call the haptics API itself, because the right trigger set and intensity curve are design decisions and the
platform APIs differ. Bind to `OnDamageTaken`, `OnSignificantImpact`, `OnPowerupCollected` and
`OnBoostStarted` and call `Play Haptic Effect` scaled by `HapticIntensity`.

---

## Building a touch HUD

A minimal but complete layout. Everything here is standard UMG; none of it is framework-specific beyond
the calls.

### 1. Cache the input component

In your widget's `Construct`:

```
Get Owning Player  →  Cast to KCFPlayerController  →  Get Kart Input  →  promote to variable
```

Re-cache on `OnPossessedKartChanged`, because respawning hands the player a new pawn.

### 2. Steering

Add a `Border` covering the left half, with `Visibility = Visible` so it receives input. Override:

- `OnTouchStarted` — record the start position (for `Relative` mode)
- `OnTouchMoved` — compute the axis, call `SetMobileSteering`
- `OnTouchEnded` — call `SetMobileSteering(0)`

Normalise by viewport width (`Get Viewport Size`), never by a pixel constant. A layout tuned to one phone
is wrong on every other one.

### 3. Action buttons

Right side, thumb-reachable. Minimum useful set:

| Button | Calls |
| --- | --- |
| Fire / Use | `UseSelectedSlot()` |
| Boost | `StartBoost()` / `StopBoost()` |
| Drift | `StartDrift()` / `StopDrift()` |
| Brake | `StartMobileBrake()` / `StopMobileBrake()` |

Use `OnTouchStarted` / `OnTouchEnded` rather than `OnClicked` — `OnClicked` only fires on release, which
makes every action feel late.

Sizing: 15% of the shorter viewport dimension is a reasonable thumb target, with at least 8 px between
adjacent buttons so a slightly-off tap does not hit the wrong one.

### 4. Status readouts

Health from `OnHealthChanged`, item icon from `OnInventoryChanged` + `GetCurrentItem()`, boost charges from
`OnChargesChanged`, position from `GetPlayerPosition`, timer bound to `GetFormattedRemainingTime()`.
All listed in [BlueprintAPI.md](BlueprintAPI.md).

### 5. Safe areas

Wrap the whole thing in a `Safe Zone` widget. Notches, punch-holes and home indicators eat the exact screen
regions where thumbs naturally rest.

### 6. Show it only on touch

```
Branch (UKCFBlueprintLibrary::IsMobilePlatform())  →  Add to Viewport
```

Then test it on desktop with `KCF.ForceTouchControls 1` in the console, which makes `IsMobilePlatform()`
return true in PIE. That console variable exists precisely so you do not have to deploy to a device to
iterate on the layout.

---

## Performance

The framework is built so that a twelve-kart mobile match is not dominated by gameplay code:

- **Six ticking classes total**, and only three of them run on a client that is not the local player's
  (see [Architecture.md](Architecture.md#the-tick-budget)). Everything else is timers and delegates.
- **The arcade movement backend** is a kinematic integrator, not a wheeled vehicle solver. Chaos is
  available but is the more expensive choice — profile before shipping it on mobile.
- **Actor pooling** is available (`UKCFActorPoolSubsystem`) for actors your own code spawns frequently.
  Read the caveat in [Architecture.md](Architecture.md#actor-pooling) before reaching for it — the
  framework's own projectiles deliberately do not use it.
- **Owner-only replication** on inventory, ammo, boost charges and respawn timing cuts bandwidth on
  cellular connections where it is scarce.
- **Net cull distance** (`KartNetCullDistance`, 600 m) drops karts a mobile client cannot see.

Settings worth lowering for mobile, in `Project Settings > Game > Kart Combat Framework`:

| Setting | Desktop | Mobile suggestion |
| --- | --- | --- |
| `KartNetUpdateFrequency` | 60 | 30 |
| `KartMinNetUpdateFrequency` | 15 | 10 |
| `MaxLiveProjectilesPerPlayer` | 8 | 4 |
| `KartNetCullDistance` | 60000 | 40000 |

---

## Testing on device

1. **Iterate in PIE** with `KCF.ForceTouchControls 1`. Mouse drag stands in for a thumb.
2. **Launch on device** (`Launch > Android/iOS`) early. Touch targets that look fine on a monitor are
   routinely too small in a hand.
3. **Test on the smallest screen you support.** Layout problems appear there first.
4. **Test on cellular, not Wi-Fi.** Higher latency and jitter is where a client-authoritative mistake in
   *your* code would show up.
5. **Test with the device warm.** Thermal throttling after ten minutes is the real frame rate.
