# Dedicated Server

The framework runs on a dedicated server without modification. This document says what it skips, how it
skips it, and how to prove it on your own build.

## The guarantee

A dedicated server constructs no camera, loads no mesh, plays no sound, spawns no Niagara system, and
builds no widget. Three mechanisms get there:

1. **Module dependencies.** `KartCombatFramework.Build.cs` does not list `UMG`, `Slate`, `SlateCore`,
   `Niagara` or any audio module. The framework *cannot* create a widget, because the symbols are not
   linked.
2. **Construction guards.** Cosmetic components are not created when `GetNetMode() == NM_DedicatedServer`.
   Skipped, not created-and-hidden.
3. **Soft references.** Every mesh, material, sound and effect on a Data Asset is a `TSoftObjectPtr`.
   Unresolved soft pointers cost a path string. A dedicated server never resolves them, so the art never
   enters memory.

## Where the guards are

| Location | Skipped on a dedicated server |
| --- | --- |
| `AKCFKartPawn::BeginPlay` | Camera boom and camera component construction |
| `AKCFKartPawn::ApplyVehicleVisuals` | Mesh, material and team-colour application — returns immediately |
| `AKCFKartPawn` cosmetic paths | Impact, landing and drift effect spawning |
| `UKCFKartCameraComponent::BeginPlay` | Activation and ticking; it is never constructed in the first place |
| `UKCFWeaponComponent::MulticastWeaponFired_Implementation` | Muzzle flash and fire sound |
| `AKCFProjectile` | Impact effect and sound on explosion |
| `AKCFDeployable` | Trigger effect and sound |
| `AKCFPowerupPickup` | Mesh setup, pickup VFX, and its own cosmetic tick |

The pattern is consistently an early return, so the authoritative work above it still happens:

```cpp
// Damage was already applied by the caller. This function is only the presentation.
if (World && World->GetNetMode() == NM_DedicatedServer)
{
    return;
}
```

## Net mode, not authority

Two different questions, and conflating them is the most common source of dedicated-server bugs:

```cpp
HasAuthority()                           // "may I change gameplay state?"
GetNetMode() == NM_DedicatedServer       // "is there a screen?"
```

On a **listen server** both the server and a local player exist in one process: `HasAuthority()` is true
*and* there is a camera to position and a sound to play. Guarding cosmetics with `!HasAuthority()` would
break the listen-server host's own game. Guarding gameplay with a net mode check would let clients run
authoritative logic.

So: gameplay asks `HasAuthority()`. Presentation asks the net mode.

The framework uses `NM_DedicatedServer` — rather than the broader `IsRunningDedicatedServer()` — because
PIE's *Play As Dedicated Server* mode reports the former on the server world while the editor process still
has a renderer. That makes the guards testable without packaging.

## Building a server target

`Source/YourProjectServer.Target.cs`:

```csharp
using UnrealBuildTool;

public class YourProjectServerTarget : TargetRules
{
    public YourProjectServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("YourProject");
    }
}
```

Build it:

```bash
# Windows
Engine\Build\BatchFiles\Build.bat YourProjectServer Linux Development ^
    -Project="C:\Path\YourProject.uproject"

# Linux
"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    YourProjectServer Linux Development -Project="$PWD/YourProject.uproject"
```

Package with `-server -noclient`:

```bash
RunUAT BuildCookRun -project="$PWD/YourProject.uproject" \
    -noP4 -platform=Linux -clientconfig=Development -serverconfig=Development \
    -cook -server -noclient -build -stage -pak -archive \
    -archivedirectory="$PWD/Packaged"
```

Run it:

```bash
./YourProjectServer.sh /Game/Maps/Arena01?game=/Game/Modes/BP_GameMode_FFA.BP_GameMode_FFA_C \
    -log -port=7777
```

Clients connect with `open <address>:7777`.

## Cooking

Add a server-specific cook setting so you are not shipping textures to a machine with no renderer. In
`DefaultGame.ini`:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToNeverCookForServer=(Path="/Game/UI")
+DirectoriesToNeverCookForServer=(Path="/Game/VFX")
```

The framework's own `Content/` folder contains no cosmetic assets, so there is nothing of its to exclude.

## Verifying it

Do this before you believe the guarantee on your own build.

### 1. PIE

`Play > Advanced Settings > Net Mode: Play As Dedicated Server`, 2 players. Both clients should play
normally. If a client's camera is missing, the problem is in your pawn subclass, not the framework —
cosmetic components must be created on clients.

### 2. Memory

Run the packaged server and check what loaded:

```
obj list class=StaticMesh
obj list class=SkeletalMesh
obj list class=SoundWave
obj list class=UserWidget
obj list class=NiagaraSystem
```

Each should be empty or contain only engine defaults. Anything of yours in those lists came from a hard
reference. Find it with the Reference Viewer and make it soft.

### 3. Headless sanity

```bash
./YourProjectServer.sh /Game/Maps/Arena01 -log -nullrhi
```

`-nullrhi` removes the RHI entirely. Any code path that touches rendering crashes immediately, which is
exactly what you want in a test.

### 4. Tick cost

Under load, the server should be ticking kart pawns and movement components and essentially nothing else:

```
stat game
stat unit
```

See [Architecture.md](Architecture.md#the-tick-budget) for the expected set.

## Writing server-safe code

Rules for your own game module.

**Never assume a player controller is local.**

```cpp
// Wrong on a dedicated server: GetFirstLocalPlayerController() returns null.
APlayerController* PC = GetWorld()->GetFirstPlayerController();
PC->ClientMessage(TEXT("hi"));

// Right: iterate controllers, send to each.
for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
{
    if (AKCFPlayerController* KCFPC = Cast<AKCFPlayerController>(It->Get()))
    {
        KCFPC->ClientNotifySomething();
    }
}
```

**Never create a widget in shared code.** Widget creation belongs in the player controller's client-side
path, guarded by `IsLocalController()`.

**Keep asset references soft.** A `UStaticMesh*` on a replicated actor forces the mesh to load on the
server. `TSoftObjectPtr<UStaticMesh>` does not.

**Put gameplay in the game mode.** `AKCFGameMode` exists only on the server, so logic there cannot
accidentally run on a client.

**Use server time.** `UKCFBlueprintLibrary::GetServerTimeSeconds()` for anything timed.

## Scaling

| Setting | Effect on server cost |
| --- | --- |
| `KartNetUpdateFrequency` | Linear in replication cost. 30 Hz is fine for most arcade play |
| `KartNetCullDistance` | Quadratic-ish in a large arena: culling distant karts removes whole relevancy pairs |
| `MaxLiveProjectilesPerPlayer` | Caps worst-case actor count. Also a DoS guard |
| `bEnableActorPooling` | Master switch for `UKCFActorPoolSubsystem`. Affects actors *your* code pools; the framework's replicated projectiles do not use it — see [Architecture.md](Architecture.md#actor-pooling) |

A twelve-player match is comfortable on a modest server instance. The dominant costs are kart movement
ticks (twelve of them) and replication; nothing in the framework does per-frame work that scales
quadratically with player count.

For many concurrent matches, run one process per match. The framework holds no global state outside the
world and game instance, so processes are fully independent.

## Match lifecycle for an orchestrator

If a matchmaker is starting and stopping your servers:

```
AKCFGameMode::NotifySessionMatchStarted()   // called on entering InProgress
AKCFGameMode::NotifySessionMatchEnded()     // called on entering Finished
```

Both forward to the active `IKCFSessionService`, which is where you report "match started" / "match ended"
to your backend. Override them in your game mode subclass, or implement the session service — see
[OnlineIntegration.md](OnlineIntegration.md).

`EKCFMatchEndReason` tells the orchestrator *why* it ended: `TimeLimit`, `ScoreLimit`, `LastStanding`,
`NotEnoughPlayers`, `HostEnded` or `ObjectiveComplete`. `NotEnoughPlayers` in particular is the signal to
recycle the instance rather than wait for a rematch.
