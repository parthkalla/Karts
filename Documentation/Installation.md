# Installation

## Requirements

- Unreal Engine 5.8
- A C++ project. The plugin itself is C++; your game code can be entirely Blueprint, but the project must
  be able to compile a plugin, which means a `Source/` folder and an IDE toolchain.
- Visual Studio 2022 (Windows), Xcode 15+ (macOS/iOS), or clang 16+ (Linux).

If your project is Blueprint-only, add any C++ class from the editor (`Tools > New C++ Class > None`).
That converts the project and generates the `Source/` folder; you can delete the class afterwards.

## Step 1: Copy the plugin

```
YourProject/
├── YourProject.uproject
├── Source/
└── Plugins/
    └── KartCombatFramework/        <- copy the whole folder here
        ├── KartCombatFramework.uplugin
        ├── Config/
        ├── Content/
        ├── Resources/
        └── Source/
```

Create `Plugins/` if it does not exist.

## Step 2: Enable the engine plugins it depends on

The descriptor declares these, so enabling the framework pulls them in. Verify them under
`Edit > Plugins` if a build complains:

| Plugin | Needed for | Optional? |
| --- | --- | --- |
| Enhanced Input | All keyboard/gamepad input | No |
| Gameplay Tags Editor | Editing the tag list in the editor | Editor only |
| Chaos Vehicles Plugin | The `KartCombatFrameworkChaosVehicles` module | Yes — see below |

### Dropping the Chaos module

If you are using the arcade movement backend (most projects), you do not need Chaos Vehicles at all.
Delete `Source/KartCombatFrameworkChaosVehicles/` and remove these two blocks from
`KartCombatFramework.uplugin`:

```json
{ "Name": "KartCombatFrameworkChaosVehicles", "Type": "Runtime", "LoadingPhase": "Default" }
```

```json
{ "Name": "ChaosVehiclesPlugin", "Enabled": true }
```

Nothing in the core module references it.

## Step 3: Add the module dependency

Open `Source/YourProject/YourProject.Build.cs` and add the runtime module:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "InputCore",
    "KartCombatFramework",
});
```

Add `"KartCombatFrameworkChaosVehicles"` only if you subclass `AKCFChaosKartPawn` in C++. Blueprint
subclasses need no build change.

You only need this step if your game has C++ that references framework types. A Blueprint-only game can
skip it: enabling the plugin is enough for Blueprint to see the classes.

## Step 4: Regenerate and build

```bash
# Windows
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
    YourProjectEditor Win64 Development -Project="C:\Path\YourProject.uproject" -WaitMutex

# Linux
"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    YourProjectEditor Linux Development -Project="$PWD/YourProject.uproject"

# macOS
"$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" \
    YourProjectEditor Mac Development -Project="$PWD/YourProject.uproject"
```

On Windows you can right-click the `.uproject` and choose *Generate Visual Studio project files* first.

Expected result: three modules compile (`KartCombatFramework`, `KartCombatFrameworkChaosVehicles` if kept,
`KartCombatFrameworkEditor`). If the first build fails, go to
[Troubleshooting.md](Troubleshooting.md#build-failures) before changing anything.

## Step 5: Verify it loaded

Open the project and check three things:

1. `Edit > Plugins > Gameplay > Kart Combat Framework` shows as enabled.
2. `Edit > Project Settings > Game > Kart Combat Framework` shows the developer settings page.
3. Right-click in the Content Browser: `Miscellaneous > Data Asset` lists the `KCF*` data asset classes.

If the settings page is missing, the runtime module did not load — check the Output Log for
`LogPluginManager` errors.

## Step 6: Point the framework at your content

`Edit > Project Settings > Game > Kart Combat Framework` configures project-wide defaults. Nothing here is
mandatory, but the catalogue tables are what make `FName` ids resolve at runtime, so set them as soon as
you have content:

| Setting | Purpose |
| --- | --- |
| Default Vehicle | Used when a game mode has no vehicle catalogue or a player has picked nothing |
| Default Game Mode Config | Used when a game mode actor has no `GameModeConfig` assigned |
| Default Mobile Settings | Used by the input component when a project has not overridden it |
| Vehicle / Weapon / Powerup / Game Mode / Arena Catalogue | The Data Tables the content registry reads |

These are `TSoftObjectPtr`, so an unset entry costs nothing and a set entry is loaded on demand.

See [DataTables.md](DataTables.md) for the row struct each catalogue expects.

## Step 7: Gameplay tags

The plugin adds its own tag source automatically. `Config/Tags/KartCombatFrameworkTags.ini` holds the
*content* tags (weapon categories, powerup rarities, arena traits, vehicle classes). The tags that C++
compares against are declared natively in `KCFGameplayTags.h`, so they exist whether or not the ini loads.

**Add your project's tags to your project's own ini**, not to the plugin's. Editing the plugin file means
every plugin update is a merge conflict. In the editor: `Project Settings > Project > Gameplay Tags > Add
New Gameplay Tag`, with the source set to a file under your project's `Config/Tags/`.

## Packaging

### Dedicated server

Add a server target in `Source/YourProjectServer.Target.cs`:

```csharp
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

The framework needs no changes for server builds — see [DedicatedServer.md](DedicatedServer.md).

### Android / iOS

The plugin has no platform-specific code and no platform `.ini` requirements. Configure the project as you
would any UE mobile project. [Mobile.md](Mobile.md) covers the input side, which is the part that actually
differs.

## Upgrading the plugin

Replace the `Plugins/KartCombatFramework/` folder wholesale and rebuild. Because the rule is that you never
edit plugin source, that is safe. If you *did* edit plugin source, diff before overwriting — and consider
whether the change belongs in a subclass in your own module instead
([Architecture.md](Architecture.md#where-to-extend) has the table).
