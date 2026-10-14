# Data Assets

Every Data Asset derives from `UKCFDataAssetBase`, which derives from `UPrimaryDataAsset`. Create them with
`Add > Miscellaneous > Data Asset` and pick the class.

## UKCFDataAssetBase

Shared by all of them.

| Field | Type | Purpose |
| --- | --- | --- |
| `Id` | `FName` | Unique identifier. This is what replicates and what Data Table rows key on. `AssetRegistrySearchable`, so the editor validator can check uniqueness without loading every asset. |
| `DisplayName` | `FText` | Localisable name for UI |
| `Description` | `FText` | Localisable description |
| `Icon` | `TSoftObjectPtr<UTexture2D>` | Soft, so the texture is only loaded when something draws it |
| `Tags` | `FGameplayTagContainer` | Classification. See `Config/Tags/KartCombatFrameworkTags.ini` |

`Id` is not optional. Leave it blank and the asset cannot be resolved at runtime, which the validator
reports as an error.

```cpp
virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const;   // override in subclasses
bool K2_ValidateConfiguration(TArray<FText>& OutErrors) const;        // Blueprint-callable
virtual FName GetPrimaryAssetType() const;                            // e.g. "KCFVehicle"
```

Every subclass overrides `ValidateConfiguration`. `UKCFDataAssetValidator` in the editor module runs it
through the engine's Data Validation framework, so failures surface on save and in `Validate Assets in
Folder`.

---

## UKCFVehicleDataAsset

Primary asset type `KCFVehicle`. Defines one kart.

### Handling

| Field | Default | Notes |
| --- | --- | --- |
| `Movement` | none | `UKCFMovementDataAsset`. **Required.** Hard pointer: the kart cannot drive without it. |
| `Mass` | 220 | kg. Used by the Chaos backend and by knockback scaling. |
| `CollisionExtent` | (90, 55, 40) | Half-extents of the collision box in cm |

### Durability

| Field | Default | Notes |
| --- | --- | --- |
| `MaxHealth` | 100 | |
| `MaxShield` | 0 | Absorbed before health |
| `ShieldRegenPerSecond` | 0 | 0 disables regeneration |
| `ShieldRegenDelay` | 5 | Seconds after the last damage before regen starts |
| `DamageMultipliers` | empty | `TMap<EKCFDamageCategory, float>`. Missing entries mean 1.0. |

`DamageMultipliers` is how vehicle classes get character. A heavy kart with `Collision: 0.6` and
`Explosion: 0.8` genuinely shrugs off ramming without any special-case code — the damage subsystem applies
the multiplier at step 8 of the pipeline.

### Boost

| Field | Default |
| --- | --- |
| `BoostMultiplier` | 1.6 |
| `BoostDuration` | 1.8 s |
| `BoostCooldown` | 6 s |
| `BoostCharges` | 1 |

### Combat

| Field | Default | Notes |
| --- | --- | --- |
| `InventorySlots` | 1 | How many items the kart can carry |
| `DefaultWeapon` | none | Soft. Granted on spawn; leave empty for pickup-only combat |
| `MuzzleSocket` | `Muzzle` | Socket on the mesh. Falls back to the nose of the collision box |
| `DropSocket` | `Drop` | Where deployables are placed. Falls back to behind the kart |

### Visuals

All soft pointers, all optional, none loaded on a dedicated server.

| Field | Notes |
| --- | --- |
| `VehicleMesh` | `USkeletalMesh`. Required by the Chaos backend |
| `StaticVehicleMesh` | `UStaticMesh`. Simpler and cheaper for the arcade backend |
| `MaterialOverrides` | Applied by index |
| `TeamColorMaterialIndex` | Which slot receives the team colour. `INDEX_NONE` disables it |
| `TeamColorParameterName` | Vector parameter name, default `TeamColor` |
| `Wheels` | `TArray<FKCFWheelVisualConfig>` — socket, mesh, radius, steered, driven, max steer angle |
| `BoostEffect` / `DriftEffect` / `DestructionEffect` | Niagara systems |
| `Audio` | A `UKCFAudioDataAsset` |

`HasVisualMesh()` returns whether either mesh is set, which is what `ApplyVehicleVisuals` checks before
doing any work.

### Camera

| Field | Default | Notes |
| --- | --- | --- |
| `CameraDistance` | 520 | Spring arm length in cm |
| `CameraOffset` | (0, 0, 150) | Socket offset |
| `BaseFOV` | 85 | At rest |
| `MaxSpeedFOV` | 100 | At top speed |
| `BoostFOVBonus` | 8 | Added while boosting |

The FOV widening is most of the perceived speed. Tuning `MaxSpeedFOV` changes how fast the game *feels*
far more than changing `MaxSpeed` does.

---

## UKCFMovementDataAsset

Primary asset type `KCFMovement`. Shared between vehicles — several karts can reference one movement
profile, or each can have its own.

### Speed

| Field | Default | Notes |
| --- | --- | --- |
| `MaxSpeed` | 1800 | cm/s. ~65 km/h |
| `MaxReverseSpeed` | 600 | |
| `Acceleration` | 2400 | cm/s² |
| `BrakingDeceleration` | 4000 | |
| `CoastingDeceleration` | 900 | With no throttle and no brake |
| `AccelerationBySpeedCurve` | — | `FRuntimeFloatCurve` mapping speed percent to an acceleration multiplier |

An empty curve means a flat 1.0. A curve that starts at 1.5 and falls to 0.4 gives sharp launches and a
gradual approach to top speed, which is the classic arcade feel.

### Steering

| Field | Default | Notes |
| --- | --- | --- |
| `TurnRate` | 160 | Degrees/second at full lock |
| `TurnRateBySpeedCurve` | — | Speed percent → turn rate multiplier |
| `SteeringInterpSpeed` | 8 | How fast steering reaches the input. Lower is heavier |
| `AirControl` | 0.25 | Fraction of normal control while airborne |

`TurnRateBySpeedCurve` is the one that stops a kart pirouetting on the spot. A curve of 0.3 at zero speed
rising to 1.0 by 40% and easing to 0.7 at top speed is a good starting shape.

### Grip and drift

| Field | Default | Notes |
| --- | --- | --- |
| `Grip` | 0.92 | Lateral velocity retained per second. Higher is more planted |
| `DriftGrip` | 0.35 | Substituted while drifting. This single swap *is* the drift model |
| `DriftTurnMultiplier` | 1.45 | Extra turn authority while drifting |
| `MinDriftSpeedPercent` | 0.35 | Below this, drift input does nothing |
| `DriftVisualYawOffset` | 22 | Degrees of visual lean. Cosmetic only |

### Physics

| Field | Default |
| --- | --- |
| `Gravity` | 2400 |
| `TerminalVelocity` | 4000 |
| `GroundTraceDistance` | 60 |
| `MaxDrivableSlopeAngle` | 50 |
| `GroundAlignSpeed` | 360 |
| `WallRestitution` | 0.25 |
| `KnockbackDecayRate` | 3.5 |
| `MaxKnockbackSpeed` | 2500 |

```cpp
float GetAccelerationMultiplier(float SpeedPercent) const;
float GetTurnRateMultiplier(float SpeedPercent) const;
```

Both return 1.0 when the corresponding curve is empty.

---

## UKCFWeaponDataAsset

Primary asset type `KCFWeapon`. Full behavioural explanation in
[WeaponSystem.md](WeaponSystem.md); this is the field list.

**Delivery:** `Delivery` (`EKCFWeaponDelivery`), `ProjectileClass`, `DeployableClass`, `SelfEffectClass`,
`HitscanRange` (8000), `AreaEffectRadius` (1200), `DeployBehindDistance` (220).

**Damage:** `Damage` (40), `DamageCategory` (`Projectile`), `ExplosionRadius` (450),
`MinimumFalloffDamageFraction` (0.25), `Knockback` (900), `bCanDamageInstigator` (false), `DamageTags`.

**Projectile:** `ProjectileSpeed` (4200), `ProjectileLifetime` (6), `ProjectileGravityScale` (0),
`bHoming` (false), `HomingStrength` (9000), `HomingAcquisitionRange` (9000), `HomingAcquisitionAngle` (45),
`bBounceOffWorld` (false), `Bounciness` (0.6).

**Deployable:** `DeployableLifetime` (25), `DeployableArmDelay` (0.75), `DeployableTriggerRadius` (180).

**Ammo:** `Ammo` (1), `MaxAmmo` (1), `FireInterval` (0.45), `PostFireCooldown` (0),
`ProjectilesPerShot` (1), `SpreadAngle` (0), `bRemoveWhenEmpty` (true),
`bUsableDuringSpawnProtection` (false).

**Presentation:** `EquippedMesh`, `MuzzleEffect`, `ImpactEffect`, `FireSound`, `ImpactSound`,
`KillFeedVerb` (an `FText` like "obliterated", used by your kill feed widget).

---

## UKCFPowerupDataAsset

Primary asset type `KCFPowerup`.

| Field | Default | Notes |
| --- | --- | --- |
| `Type` | `Weapon` | `EKCFPowerupType` — see [PowerupSystem.md](PowerupSystem.md#powerup-types) |
| `GrantedWeapon` | none | Soft `UKCFWeaponDataAsset`. Required when `Type == Weapon` |
| `EffectClass` | none | Soft `UKCFPowerupEffect` subclass. Required for non-weapon types |
| `Duration` | 6 | Seconds. Ignored for `Instant` and `Passive` |
| `Cooldown` | 0 | Per-kart, per-id, against server time |
| `bStackable` | false | |
| `MaxStacks` | 3 | |
| `bAutoActivateOnPickup` | false | |
| `Magnitude` | 1 | Passed to `ApplyEffect`. The number the effect actually means |
| `GrantedTags` | empty | Added to the kart's `ActiveTags` while active. Immunity tags go here |
| `PickupMesh` | none | Soft `UStaticMesh` for the world pickup |
| `PickupEffect` / `ActiveEffect` | none | Niagara for the pickup and for the active state |
| `PickupSound` / `ActivateSound` | none | |

`GrantedTags` is how a designer builds "this shield also makes you immune to knockback" without a
programmer: add `KCF.Immunity.Knockback` to the container, and the damage pipeline reads it at step 7.

---

## UKCFGameModeDataAsset

Primary asset type `KCFGameMode`. The rules of a match.

**Players:** `MinPlayers` (2), `MaxPlayers` (8), `LobbyGraceSeconds` (10),
`bAllowJoinInProgress` (true), `AbortBelowPlayerCount` (1).

**Timing:** `PreparingSeconds` (3), `CountdownSeconds` (3), `MatchDurationSeconds` (300),
`PostMatchSeconds` (12).

**Scoring:** `ScoreLimit` (15), `ScorePerKill` (1), `ScorePerAssist` (0), `ScorePerDeath` (0),
`ScorePerTeamKill` (-1).

**Sudden death:** `bUseSuddenDeath` (true), `SuddenDeathSeconds` (90),
`bDisableRespawnDuringSuddenDeath` (true).

**Respawn:** `bRespawnEnabled` (true), `RespawnDelaySeconds` (3), `LivesPerPlayer` (0 = unlimited),
`SpawnProtectionSeconds` (2.5), `bSpawnProtectionBreaksOnFire` (true),
`SpawnSelection` (`FarthestFromEnemies`), `MinSpawnDistanceFromEnemies` (2500).

**Teams:** `TeamCount` (0 = free-for-all; ≥2 = team mode), `Teams` (`TArray<FKCFTeamDefinition>`),
`bFriendlyFire` (false), `FriendlyFireScale` (0.5), `bAllowTeamSelection` (true),
`MaxTeamSizeImbalance` (1).

**Gameplay:** `bPowerupsEnabled` (true), `PowerupSpawnIntervalScale` (1.0),
`bClearInventoryOnDeath` (true), `bCollisionDamageEnabled` (true), `ModeTags`.

```cpp
bool IsTeamMode() const { return TeamCount >= 2; }
FKCFTeamDefinition GetTeamDefinition(int32 TeamId) const;
```

`FKCFTeamDefinition` carries `TeamId`, `DisplayName`, `TeamColor` and `SpawnTag`. `SpawnTag` is matched
against `AKCFPlayerSpawnPoint::SpawnTags` so each team spawns in its own zone.

This asset replicates on `AKCFGameState`, so clients can read the score limit, team colours and respawn
rules for their HUD without a parallel copy.

---

## UKCFArenaDataAsset

Primary asset type `KCFArena`. Describes a playable level.

| Field | Default | Notes |
| --- | --- | --- |
| `Map` | none | `TSoftObjectPtr<UWorld>` |
| `PreviewImage` | none | For a level-select screen |
| `SupportedGameModes` | empty | Soft refs to `UKCFGameModeDataAsset`; drives mode/arena filtering |
| `RecommendedMinPlayers` | 2 | |
| `RecommendedMaxPlayers` | 8 | |
| `PlayspaceOrigin` | (0,0,0) | Centre of the playable volume |
| `PlayspaceExtent` | (25000, 25000, 10000) | Half-extents in cm |
| `KillZ` | -5000 | Below this, a kart dies |
| `OutOfBoundsDamagePerSecond` | 25 | Applied outside the playspace |
| `OutOfBoundsGraceSeconds` | 2 | Before the damage starts |

```cpp
bool IsInsidePlayspace(const FVector& WorldLocation) const;
```

The playspace is deliberately a box rather than a volume actor: it is data, so a mode can reason about
bounds before the level is loaded (`AKCFBattleRoyaleGameMode` uses it to pick the initial zone centre).

---

## UKCFAudioDataAsset

Primary asset type `KCFAudio`. Referenced by a vehicle. Every field is a `TSoftObjectPtr<USoundBase>` and
none of them is loaded on a dedicated server.

**Engine:** `EngineLoop`, `EngineIdlePitch` (0.8), `EngineMaxPitch` (2.1). Pitch interpolates with speed
percent.

**Driving:** `TireSkidLoop`, `BoostStart`, `BoostLoop`, `Landing`.

**Impacts:** `LightImpact`, `HeavyImpact`, `HeavyImpactSpeedThreshold` (1500 cm/s).

**Combat and state:** `DamageTaken`, `ShieldBreak`, `Destruction`, `Respawn`, `PowerupCollected`.

The plugin does not play these. It gives you a structured place to author them and a delegate for each
event that should trigger one — your Blueprint decides attenuation, concurrency and mixing, which are
project-level audio decisions.

---

## UKCFMobileSettingsDataAsset

Primary asset type `KCFMobileSettings`. Covered in detail in [Mobile.md](Mobile.md).

**Steering:** `SteeringMode` (`Direct`), `SteeringDeadzone` (0.08), `SteeringSensitivity` (1.0),
`SteeringExponent` (1.6), `SteeringReturnSpeed` (6.0), `RelativeSteeringTravel` (0.18),
`TiltFullLockDegrees` (30), `TiltNeutralDegrees` (0).

**Throttle:** `bAutoAccelerate` (true), `AutoAccelerateThrottle` (1.0),
`bAutoAccelerateYieldsToBrake` (true).

**Actions:** `bDriftIsToggle` (false), `bBoostIsToggle` (true), `FireInputBufferSeconds` (0.25),
`bFireRepeatsWhileHeld` (true).

**Camera:** `LookSensitivity` (1.0), `bInvertLookY` (false), `bAutoCenterCamera` (true),
`AutoCenterDelaySeconds` (1.2).

**Haptics:** `bHapticsEnabled` (true), `HapticIntensity` (0.6).

```cpp
float ProcessSteeringAxis(float RawAxis) const;
```

Applies deadzone, exponent and sensitivity in that order. Public so you can preview the curve in a settings
screen.

---

## UKCFInputConfigDataAsset

Primary asset type `KCFInputConfig`. Binds Enhanced Input assets.

`MappingContext`, `MappingPriority` (0), then one `TSoftObjectPtr<UInputAction>` per action: `MoveAction`,
`SteerAction`, `ThrottleAction`, `BrakeAction`, `DriftAction`, `BoostAction`, `FireAction`,
`UsePowerupAction`, `CycleSlotAction`, `LookAction`, `ResetCameraAction`, `PauseAction`, `ResetKartAction`.

Every action is optional — bind only what your control scheme uses. A pure touch build can leave all of
them empty and drive the kart entirely through the mobile API.
