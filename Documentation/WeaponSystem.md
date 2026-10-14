# Weapon System

Weapons are data, not code. A `UKCFWeaponDataAsset` describes what a weapon does; `UKCFWeaponComponent`
executes it; `AKCFProjectile` and `AKCFDeployable` are the two actors it can put into the world.

## Why there is no `AKCFWeapon` actor

A per-weapon actor class is the obvious design, and it is the wrong one here. Every weapon in an arcade
kart game is a one-shot or few-shot consumable that exists for a second between pickup and firing. Giving
each one an actor means:

- a replicated actor per held item, for state that is already in the owner's inventory;
- a class per weapon, so adding a weapon is a code change and a recompile rather than a data asset;
- a second place where damage can originate, which weakens the single-pipeline guarantee in
  [Multiplayer.md](Multiplayer.md#the-damage-pipeline).

Instead: the *held* weapon is an `FName` in an inventory slot, the *definition* is a Data Asset resolved
through `UKCFContentRegistrySubsystem`, and the *effect in the world* is a projectile or deployable actor
that only exists while it is actually flying or armed. A designer adds a weapon by making a data asset.

The one case where you would add a weapon actor is a persistently attached, independently simulating
turret — something that aims, has its own collision, and survives across firings. That is a different
genre of object, and the right way to build it is an actor attached to the kart that *calls*
`UKCFDamageSubsystem::ApplyDamage`, not a replacement for this system.

## UKCFWeaponDataAsset

Create with `Add > Miscellaneous > Data Asset > KCFWeaponDataAsset`. Full field reference in
[DataAssets.md](DataAssets.md#ukcfweapondataasset); this covers how the fields combine.

### Delivery modes

`EKCFWeaponDelivery` selects which code path `ExecuteFire` takes. Fields outside the chosen mode are
ignored, and the editor validator warns about the ones you left set by mistake.

| Mode | What happens | Key fields |
| --- | --- | --- |
| `Projectile` | Spawns `ProjectileClass` at the muzzle | `ProjectileSpeed`, `ProjectileLifetime`, `ProjectileGravityScale`, `bHoming`, `bBounceOffWorld`, `ProjectilesPerShot`, `SpreadAngle` |
| `Hitscan` | Traces forward from the muzzle, damages the first valid hit | `HitscanRange` |
| `Deployable` | Places `DeployableClass` behind the kart | `DeployBehindDistance`, `DeployableLifetime`, `DeployableArmDelay`, `DeployableTriggerRadius` |
| `Self` | Instantiates `SelfEffectClass` on the firing kart | `SelfEffectClass` |
| `AreaEffect` | Radial damage centred on the firing kart | `AreaEffectRadius`, `MinimumFalloffDamageFraction` |

### Damage

`Damage` is the base number. `DamageCategory` picks which entry of the target vehicle's
`DamageMultipliers` map applies, so an armoured kart can resist explosions without resisting bullets.
`DamageTags` ride along in `FKCFDamageInfo` so effects and immunities can key off them.

`ExplosionRadius > 0` makes any delivery mode explode on impact, falling off from full damage at the centre
to `MinimumFalloffDamageFraction` at the rim. `Knockback` is an impulse magnitude applied through
`IKCFVehicleMovement::ApplyKnockback`, which is what makes a hit *feel* like a hit even when the damage is
small.

`bCanDamageInstigator` defaults to false. Turn it on for a weapon where blowing yourself up is part of the
design.

### Ammo and cooldown

| Field | Meaning |
| --- | --- |
| `Ammo` / `MaxAmmo` | Shots per pickup |
| `FireInterval` | Minimum seconds between shots |
| `PostFireCooldown` | Extra lockout after the last shot, for a heavy weapon |
| `ProjectilesPerShot` | Pellets per trigger pull |
| `SpreadAngle` | Cone half-angle in degrees for multi-pellet shots |
| `bRemoveWhenEmpty` | Clear the inventory slot when ammo hits zero |
| `bUsableDuringSpawnProtection` | Whether firing it is allowed while spawn-protected (firing normally breaks protection) |

All cooldowns are evaluated against `GetServerWorldTimeSeconds()`, never against a locally accumulated
timer, so a client with a modified clock gains nothing.

## Firing, end to end

```
Client:  UKCFKartInputComponent::FireWeapon()
      →  UKCFCombatComponent::FireCurrentWeapon()
      →  UKCFWeaponComponent::FireCurrentWeapon()
      →  local CanFireWeapon() check (avoids a wasted RPC; not a security check)
      →  ServerFireWeapon()                       <- NO PARAMETERS

Server:  ServerFireWeapon_Validate()  → true (nothing structural to check)
      →  ServerFireWeapon_Implementation()
         → RefreshCurrentWeapon()   reads the SERVER's inventory, not the client's claim
         → CanFireWeapon()          cooldown vs server time, ammo, match live, spawn protection
         → ExecuteFire(Weapon)      switches on Delivery
         → ConsumeAmmoAfterFiring() decrements, clears the slot if empty
         → MulticastWeaponFired()   cosmetic only: weapon id + muzzle location
      or → ClientFireRejected()     so the client can clear a predicted animation
```

`RefreshCurrentWeapon()` is the important line. The server does not read a weapon out of the RPC, because
there is none in it; it looks at the inventory it owns and replicated. A client that edits its local
inventory changes nothing the server will act on.

## Projectiles

`AKCFProjectile` implements `IKCFTeamAgent` and carries a `UProjectileMovementComponent`.

Spawned server-side with `WeaponData` and `TeamId` replicated `COND_InitialOnly` — one send at spawn, then
the movement component's own replication carries the flight. On hit, the server resolves damage (single
target, or `ApplyRadialDamage` when `ExplosionRadius > 0`) and then multicasts `MulticastExplode` purely so
every client can play the VFX. The explosion is already resolved before the multicast leaves.

`BP_OnExplode(FVector Location, FVector Normal)` is the Blueprint hook for your Niagara system.

### Homing

Set `bHoming`, then:

| Field | Effect |
| --- | --- |
| `HomingStrength` | Acceleration toward the target |
| `HomingAcquisitionRange` | How far it looks |
| `HomingAcquisitionAngle` | Cone half-angle in front of the muzzle it will consider |

Target selection is server-side and respects team attitude — a homing missile will not lock a teammate when
friendly fire is off. The acquisition cone is what stops a missile fired forwards from turning around and
chasing someone behind you.

Tag homing weapons with `KCF.Weapon.Trait.Homing` so your HUD can warn the victim. The framework does not
draw that warning, but it gives you the tag to drive it.

### Bouncing

`bBounceOffWorld` plus `Bounciness` makes a projectile ricochet instead of detonating on world geometry.
Karts still take the hit. This is how you build a bomb that rolls down a corridor.

### Projectile limits

`UKCFDeveloperSettings::MaxLiveProjectilesPerPlayer` (default 8) caps how many live projectiles a single
player can have. The weapon component tracks them in a `TArray<TWeakObjectPtr<AActor>>` and refuses to fire
past the cap. This is a denial-of-service guard: without it, a rapid-fire weapon with a long lifetime and a
modified client spamming the fire RPC is a server-side memory and replication problem.

## Deployables

`AKCFDeployable` is for mines, traps and anything placed and triggered later.

Placed at `GetDropTransform()` offset back by `DeployBehindDistance`. It arms after
`DeployableArmDelay` (so you cannot drop one on top of someone and detonate instantly), then watches a
`USphereComponent` of radius `DeployableTriggerRadius`. Overlap by a hostile kart triggers it;
`DeployableLifetime` removes it if nobody does.

`bArmed` replicates so clients can show the armed state, and `BP_OnArmed` / `BP_OnTriggered(FVector)` are
the cosmetic hooks. Team id is `COND_InitialOnly` and drives whether an overlapping kart counts as hostile.

## Hitscan

Traces `HitscanRange` from the muzzle along the kart's forward vector. First valid `IKCFDamageable` hit
takes the damage. `KCF.Weapon.Trait.Piercing` is available as a content tag if you implement pass-through
in a projectile subclass; the base hitscan path stops at the first hit.

Note the caveat in [Multiplayer.md](Multiplayer.md#what-the-framework-does-not-do): traces run against
present-time server positions, with no lag compensation. For a fast-moving kart at 100 ms that is a
noticeable lead requirement. Projectile weapons dodge the problem entirely, which is one reason the genre
uses them.

## Self and area effects

`Self` instantiates `SelfEffectClass` (a `UKCFPowerupEffect` subclass) on the firing kart — repair kits,
temporary shields and speed surges that happen to live in a weapon slot rather than a powerup slot.

`AreaEffect` applies radial damage centred on the kart itself with falloff to
`MinimumFalloffDamageFraction`. This is the shockwave: no projectile, no aiming, just everything nearby.
`bCanDamageInstigator` is almost always false here.

## Recipes

**Basic blaster.** `Projectile`, `KCFProjectile`, Damage 40, Speed 4200, Ammo 3, FireInterval 0.45.

**Homing missile.** `Projectile`, Damage 65, Speed 2800, `bHoming` true, HomingStrength 9000,
AcquisitionRange 9000, AcquisitionAngle 45, ExplosionRadius 450, Knockback 1200, Ammo 1. Tag it
`KCF.Weapon.Trait.Homing`.

**Rear-drop mine.** `Deployable`, `KCFDeployable`, Damage 80, ExplosionRadius 500, DeployBehindDistance
220, ArmDelay 0.75, TriggerRadius 180, Lifetime 25, Ammo 1. Tag it `KCF.Weapon.Trait.Rear`.

**Shotgun.** `Projectile`, Damage 18, ProjectilesPerShot 6, SpreadAngle 9, Speed 5000, Lifetime 1.2,
Ammo 2, PostFireCooldown 0.6.

**Shockwave.** `AreaEffect`, Damage 50, AreaEffectRadius 1200, MinimumFalloffDamageFraction 0.25,
Knockback 1800, `bCanDamageInstigator` false, Ammo 1. The knockback is the point; the damage is a bonus.

**Repair pulse.** `Self`, `SelfEffectClass` = `KCFRepairEffect`, Damage 0, Ammo 1.

## Extending

| You want | Do this |
| --- | --- |
| A new standard weapon | Author a data asset. No code. |
| Novel flight (orbit, split, boomerang) | Subclass `AKCFProjectile`, set it as `ProjectileClass` |
| A trap with custom trigger logic | Subclass `AKCFDeployable` |
| A new self-effect | Subclass `UKCFPowerupEffect`, set it as `SelfEffectClass` |
| A new *category* of delivery | This is the one case that needs `EKCFWeaponDelivery` and `ExecuteFire` extended. Consider first whether a projectile subclass gets you there. |
