# Plugin content

This folder is intentionally empty.

The framework ships **no** meshes, materials, textures, Niagara systems, sounds, animations, levels or
widgets. That is a deliberate licensing and design decision, not an omission:

- **Originality.** The plugin is original work with no proprietary or third-party assets. Shipping art
  would mean shipping something whose provenance a user would have to audit before release.
- **Dedicated servers.** Every cosmetic reference in the framework is a `TSoftObjectPtr` on a Data Asset,
  so a server build resolves none of them. Bundled default assets would be hard references waiting to
  happen.
- **Your art direction is yours.** A placeholder kart mesh in a plugin becomes the kart mesh in three
  shipped games.

`CanContainContent` is `true` in the descriptor so the folder is available if *you* want to put shared
content here — but in a project, game content belongs in the project's own `Content/`, not in a plugin
you intend to update.

## Where content actually goes

Author these in your project and point the framework at them:

| What | Where it is referenced |
| --- | --- |
| Kart meshes, materials, wheels | `UKCFVehicleDataAsset` |
| Weapon and impact VFX, sounds | `UKCFWeaponDataAsset` |
| Powerup pickup meshes and VFX | `UKCFPowerupDataAsset` |
| Engine, skid, impact and UI audio | `UKCFAudioDataAsset` |
| Arenas | `UKCFArenaDataAsset` and the arena catalogue |
| HUD and menu widgets | Your own UMG, bound to the delegates in `../Documentation/BlueprintAPI.md` |
| Input mapping contexts and actions | `UKCFInputConfigDataAsset` |

See [../Documentation/QuickStart.md](../Documentation/QuickStart.md) to go from an empty project to a
playable match, and [../Documentation/DataAssets.md](../Documentation/DataAssets.md) for every field.
