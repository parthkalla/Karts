// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectMacros.h"
#include "KCFTypes.generated.h"

class AActor;
class AKCFPlayerState;

/** Team identifier used for "no team" (free-for-all, or a player not yet assigned). */
#define KCF_NO_TEAM INDEX_NONE

/** Inventory slot index meaning "nothing selected". */
#define KCF_NO_SLOT INDEX_NONE

// ---------------------------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------------------------

/**
 * Lifecycle of a single match. Transitions are validated centrally by
 * UKCFMatchStateMachine::IsTransitionAllowed so that an illegal transition is logged rather than
 * silently corrupting the match.
 */
UENUM(BlueprintType)
enum class EKCFMatchState : uint8
{
	/** Not enough players connected yet, or waiting for the host to start. */
	WaitingForPlayers	UMETA(DisplayName = "Waiting For Players"),

	/** Player count satisfied. Pawns are being spawned and the arena is being prepared. */
	Preparing			UMETA(DisplayName = "Preparing"),

	/** Everyone is spawned and frozen; the pre-match countdown is running. */
	Countdown			UMETA(DisplayName = "Countdown"),

	/** Normal play. Input, damage, scoring and powerups are all live. */
	InProgress			UMETA(DisplayName = "In Progress"),

	/** Optional tie-breaker phase. Rules differ per game mode. */
	SuddenDeath			UMETA(DisplayName = "Sudden Death"),

	/** A winner exists. Results are replicated; input is ignored. */
	Finished			UMETA(DisplayName = "Finished"),

	/** Travelling back to the lobby/menu. */
	ReturningToLobby	UMETA(DisplayName = "Returning To Lobby"),

	MAX					UMETA(Hidden)
};

/** Why a match ended. Drives the results screen without the UI needing to infer it. */
UENUM(BlueprintType)
enum class EKCFMatchEndReason : uint8
{
	Unknown				UMETA(DisplayName = "Unknown"),
	TimeLimit			UMETA(DisplayName = "Time Limit Reached"),
	ScoreLimit			UMETA(DisplayName = "Score Limit Reached"),
	LastStanding		UMETA(DisplayName = "Last Player Or Team Standing"),
	NotEnoughPlayers	UMETA(DisplayName = "Not Enough Players"),
	HostEnded			UMETA(DisplayName = "Ended By Host"),

	/** A mode-specific goal was met: the race distance was completed, an objective was captured. */
	ObjectiveComplete	UMETA(DisplayName = "Objective Complete")
};

/**
 * Broad damage category. Deliberately coarse: fine-grained classification belongs in the
 * FGameplayTag carried by FKCFDamageInfo. This enum exists so that per-category multipliers can be
 * authored as a simple map in a Data Asset.
 */
UENUM(BlueprintType)
enum class EKCFDamageCategory : uint8
{
	Direct			UMETA(DisplayName = "Direct"),
	Projectile		UMETA(DisplayName = "Projectile"),
	Explosion		UMETA(DisplayName = "Explosion"),
	Collision		UMETA(DisplayName = "Collision"),
	Environmental	UMETA(DisplayName = "Environmental"),
	Trap			UMETA(DisplayName = "Trap"),
	Melee			UMETA(DisplayName = "Melee"),
	MAX				UMETA(Hidden)
};

/** How a weapon delivers its effect. Chosen in the weapon Data Asset, not by subclassing in C++. */
UENUM(BlueprintType)
enum class EKCFWeaponDelivery : uint8
{
	/** Spawns an AKCFProjectile that travels and resolves its own hit on the server. */
	Projectile		UMETA(DisplayName = "Projectile"),

	/** Immediate server-side line trace from the muzzle. */
	Hitscan			UMETA(DisplayName = "Hitscan"),

	/** Places an AKCFDeployable (mine, trap) at or behind the firing kart. */
	Deployable		UMETA(DisplayName = "Deployable"),

	/** Applies a gameplay effect to the firing kart itself (shield, repair, nitro). */
	Self			UMETA(DisplayName = "Self"),

	/** Immediate radial effect centred on the firing kart (EMP, shockwave). */
	AreaEffect		UMETA(DisplayName = "Area Effect")
};

/** How a powerup behaves once collected. Drives scheduling in UKCFPowerupComponent. */
UENUM(BlueprintType)
enum class EKCFPowerupType : uint8
{
	/** Applied immediately and finished (heal, instant score). */
	Instant			UMETA(DisplayName = "Instant"),

	/** Applied, held for Duration, then reverted (speed, invulnerability). */
	Timed			UMETA(DisplayName = "Timed"),

	/** Places a weapon into the inventory instead of applying an effect. */
	Weapon			UMETA(DisplayName = "Weapon"),

	/** Applied until death or match end. */
	Passive			UMETA(DisplayName = "Passive"),

	/** Timed, but tagged defensive so UI and AI can reason about it. */
	Defensive		UMETA(DisplayName = "Defensive"),

	/** Timed, but tagged offensive. */
	Offensive		UMETA(DisplayName = "Offensive")
};

/** Steering interpretation for touch input. */
UENUM(BlueprintType)
enum class EKCFSteeringMode : uint8
{
	/** Axis value maps straight to steering. Virtual stick / wheel widgets use this. */
	Direct			UMETA(DisplayName = "Direct"),

	/** Finger movement relative to the touch-down point. Forgiving on small screens. */
	Relative		UMETA(DisplayName = "Relative Drag"),

	/** Device accelerometer tilt. */
	Tilt			UMETA(DisplayName = "Device Tilt"),

	/** Left/right screen halves act as discrete steer buttons. */
	ScreenHalves	UMETA(DisplayName = "Screen Halves")
};

/** How the respawn system picks a spawn point. */
UENUM(BlueprintType)
enum class EKCFSpawnSelectionMode : uint8
{
	/** Uniform random among enabled points. */
	Random				UMETA(DisplayName = "Random"),

	/** Scores points by distance to the nearest hostile and picks the best. */
	FarthestFromEnemies	UMETA(DisplayName = "Farthest From Enemies"),

	/** Restricted to points tagged with the player's team, then scored. */
	TeamZone			UMETA(DisplayName = "Team Zone"),

	/** Round-robin. Useful for deterministic tests and for race grids. */
	Sequential			UMETA(DisplayName = "Sequential")
};

/** Relationship between two actors, used for friendly-fire and targeting decisions. */
UENUM(BlueprintType)
enum class EKCFTeamAttitude : uint8
{
	Neutral		UMETA(DisplayName = "Neutral"),
	Friendly	UMETA(DisplayName = "Friendly"),
	Hostile		UMETA(DisplayName = "Hostile")
};

/** Reason a damage request produced no damage. Returned so callers can react without guessing. */
UENUM(BlueprintType)
enum class EKCFDamageRejectReason : uint8
{
	None					UMETA(DisplayName = "Not Rejected"),
	NoAuthority				UMETA(DisplayName = "No Authority"),
	InvalidTarget			UMETA(DisplayName = "Invalid Target"),
	TargetNotDamageable		UMETA(DisplayName = "Target Not Damageable"),
	TargetAlreadyDead		UMETA(DisplayName = "Target Already Dead"),
	FriendlyFireDisabled	UMETA(DisplayName = "Friendly Fire Disabled"),
	TargetInvulnerable		UMETA(DisplayName = "Target Invulnerable"),
	MatchNotLive			UMETA(DisplayName = "Match Not Live"),
	ZeroDamage				UMETA(DisplayName = "Zero Damage")
};

// ---------------------------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------------------------

/**
 * Everything the damage pipeline needs to resolve one damage event.
 *
 * Built exclusively on the server. There is no code path in the framework that constructs one of
 * these from client-supplied values; see UKCFDamageSubsystem::ApplyDamage.
 */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFDamageInfo
{
	GENERATED_BODY()

	/** Raw damage before shield absorption and category multipliers. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage", meta = (ClampMin = "0.0"))
	float DamageAmount = 0.f;

	/** Who gets credit for a kill. May be null for environmental damage. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	TObjectPtr<AKCFPlayerState> InstigatorPlayerState = nullptr;

	/** The actor that physically caused the damage (projectile, mine, the other kart). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	TObjectPtr<AActor> DamageCauser = nullptr;

	/** Coarse category, used to look up a multiplier on the target's vehicle Data Asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	EKCFDamageCategory Category = EKCFDamageCategory::Direct;

	/** Fine-grained classification, e.g. KCF.Damage.Explosion. Free for projects to extend. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	FGameplayTagContainer DamageTags;

	/** World-space impact point. Used for knockback direction and hit cosmetics. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	FVector HitLocation = FVector::ZeroVector;

	/** Surface normal at the impact point. Zero when not available. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	FVector HitNormal = FVector::ZeroVector;

	/** Impulse magnitude applied to the target through IKCFVehicleMovement::ApplyKnockback. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage", meta = (ClampMin = "0.0"))
	float KnockbackForce = 0.f;

	/** Normalised knockback direction. If zero, the pipeline derives it from HitLocation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	FVector KnockbackDirection = FVector::ZeroVector;

	/** Id of the weapon Data Asset responsible, for the kill feed and statistics. NAME_None if n/a. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	FName WeaponId = NAME_None;

	/** Bypasses shields entirely (used by EMP-style weapons). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	bool bIgnoreShield = false;

	/** Bypasses invulnerability and spawn protection. Reserved for out-of-bounds / kill volumes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	bool bIgnoreInvulnerability = false;

	/** Skips the friendly-fire filter. Used by self-inflicted damage. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "KCF|Damage")
	bool bIgnoreFriendlyFireRules = false;

	FKCFDamageInfo() = default;

	explicit FKCFDamageInfo(float InDamage, EKCFDamageCategory InCategory)
		: DamageAmount(InDamage)
		, Category(InCategory)
	{
	}

	/** Rejects structurally impossible values. Called before any damage is applied. */
	bool IsStructurallyValid() const
	{
		return DamageAmount >= 0.f
			&& FMath::IsFinite(DamageAmount)
			&& !HitLocation.ContainsNaN()
			&& !HitNormal.ContainsNaN()
			&& !KnockbackDirection.ContainsNaN()
			&& FMath::IsFinite(KnockbackForce);
	}
};

/** Outcome of one damage request. Returned by the pipeline so callers never have to re-query. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFDamageResult
{
	GENERATED_BODY()

	/** True when any health or shield was actually removed. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Damage")
	bool bWasApplied = false;

	/** True when this damage event reduced the target's health to zero. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Damage")
	bool bWasLethal = false;

	/** Damage absorbed by the shield. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Damage")
	float ShieldDamage = 0.f;

	/** Damage applied to health after shield absorption and multipliers. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Damage")
	float HealthDamage = 0.f;

	/** Why nothing happened, when bWasApplied is false. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Damage")
	EKCFDamageRejectReason RejectReason = EKCFDamageRejectReason::None;

	float GetTotalDamage() const { return ShieldDamage + HealthDamage; }

	static FKCFDamageResult Rejected(EKCFDamageRejectReason Reason)
	{
		FKCFDamageResult Result;
		Result.RejectReason = Reason;
		return Result;
	}
};

/**
 * The complete driving intent for one kart for one frame.
 *
 * This struct is the only thing UKCFKartMovementComponent reads. Enhanced Input, the mobile touch
 * API, and (in a project) an AI controller all write the same struct, which is why no gameplay code
 * anywhere needs to know which input device is in use.
 */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFKartInputState
{
	GENERATED_BODY()

	/** -1 full left .. +1 full right. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	float Steering = 0.f;

	/** 0 .. 1 forward throttle. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	float Throttle = 0.f;

	/** 0 .. 1 brake / reverse. Reverse engages once the kart is near standstill. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	float Brake = 0.f;

	/** Drift/handbrake held. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	bool bDrift = false;

	/** Boost held. The boost component decides whether it is actually granted. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	bool bBoost = false;

	/** Camera look delta (yaw, pitch) in degrees per second. Cosmetic only. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Input")
	FVector2D Look = FVector2D::ZeroVector;

	void Reset()
	{
		Steering = 0.f;
		Throttle = 0.f;
		Brake = 0.f;
		bDrift = false;
		bBoost = false;
		Look = FVector2D::ZeroVector;
	}

	/** Clamps every channel into its legal range. Applied before the struct is ever consumed. */
	void Sanitize()
	{
		Steering = FMath::IsFinite(Steering) ? FMath::Clamp(Steering, -1.f, 1.f) : 0.f;
		Throttle = FMath::IsFinite(Throttle) ? FMath::Clamp(Throttle, 0.f, 1.f) : 0.f;
		Brake = FMath::IsFinite(Brake) ? FMath::Clamp(Brake, 0.f, 1.f) : 0.f;
		if (Look.ContainsNaN())
		{
			Look = FVector2D::ZeroVector;
		}
	}
};

/** One entry of a weighted random table, used by powerup spawners and loot-style selection. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFWeightedPowerupEntry
{
	GENERATED_BODY()

	/** Powerup definition to spawn. Soft so a spawner does not pull every powerup into memory. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Powerup")
	TSoftObjectPtr<class UKCFPowerupDataAsset> Powerup;

	/** Relative weight. Entries with weight <= 0 are never selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Powerup", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

/** Replicated per-team score line. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFTeamScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	int32 TeamId = KCF_NO_TEAM;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	int32 Score = 0;

	/** Number of connected players currently assigned to this team. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	int32 PlayerCount = 0;

	/** Players on this team still alive / not eliminated. Elimination modes use this. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	int32 AlivePlayerCount = 0;

	bool operator==(const FKCFTeamScore& Other) const { return TeamId == Other.TeamId; }
};

/** Display-facing team definition. Authored in UKCFGameModeDataAsset. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFTeamDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Team")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Team")
	FText DisplayName;

	/** Used by project UI and by material parameter driving on the kart mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Team")
	FLinearColor TeamColor = FLinearColor::White;

	/** Spawn points tagged with this tag are preferred for this team. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Team")
	FGameplayTag SpawnTag;
};
