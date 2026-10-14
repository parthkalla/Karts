// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFGameModeDataAsset.generated.h"

/**
 * The complete rule set for one match.
 *
 * AKCFGameMode and its subclasses read every rule from here. That is what makes "Team Deathmatch,
 * 4 teams, 10 minutes, no friendly fire" a content change rather than a code change, and it is why
 * the same C++ game mode class can back several playlist entries.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFGameModeDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	// --- Population ----------------------------------------------------------------------------

	/** Match will not leave WaitingForPlayers until this many players are connected. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Players", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MinPlayers = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Players", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxPlayers = 8;

	/**
	 * Seconds to wait in WaitingForPlayers once MinPlayers is met, giving stragglers a chance to join.
	 * Set to 0 to start as soon as the minimum is reached.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Players", meta = (ClampMin = "0.0", Units = "s"))
	float LobbyGraceSeconds = 10.f;

	/** Players who connect mid-match join as participants rather than spectators. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Players")
	bool bAllowJoinInProgress = true;

	/** Below this many connected players the match aborts early. 0 disables the abort. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Players", meta = (ClampMin = "0"))
	int32 AbortBelowPlayerCount = 1;

	// --- Timing ----------------------------------------------------------------------------------

	/** Seconds spent in Preparing while pawns spawn and clients finish streaming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float PreparingSeconds = 3.f;

	/** Pre-match countdown, during which karts are spawned but frozen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float CountdownSeconds = 3.f;

	/** Length of the match proper. 0 means no time limit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float MatchDurationSeconds = 300.f;

	/** Seconds spent on the results screen before returning to the lobby. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float PostMatchSeconds = 12.f;

	// --- Win conditions -----------------------------------------------------------------------------

	/** First player or team to this score wins. 0 means score does not end the match. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions", meta = (ClampMin = "0"))
	int32 ScoreLimit = 15;

	/** Points awarded for an elimination. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions")
	int32 ScorePerKill = 1;

	/** Points awarded for an assist. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions")
	int32 ScorePerAssist = 0;

	/** Points removed for dying. Usually 0 or negative-scoring modes only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions")
	int32 ScorePerDeath = 0;

	/** Points lost for killing a team-mate. Only meaningful with friendly fire enabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions")
	int32 ScorePerTeamKill = -1;

	/** On a tie at time expiry, enter SuddenDeath rather than declaring a draw. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions")
	bool bUseSuddenDeath = true;

	/** Hard cap on SuddenDeath so a stalemate cannot run forever. 0 means unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions", meta = (EditCondition = "bUseSuddenDeath", ClampMin = "0.0", Units = "s"))
	float SuddenDeathSeconds = 90.f;

	/** Respawning is disabled during sudden death, making the first elimination decisive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Win Conditions", meta = (EditCondition = "bUseSuddenDeath"))
	bool bDisableRespawnDuringSuddenDeath = true;

	// --- Respawn ---------------------------------------------------------------------------------------

	/** When false, a death is final for the rest of the match (elimination modes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn")
	bool bRespawnEnabled = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn", meta = (EditCondition = "bRespawnEnabled", ClampMin = "0.0", Units = "s"))
	float RespawnDelaySeconds = 3.f;

	/** Lives per player for elimination modes. 0 means unlimited. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn", meta = (ClampMin = "0", ClampMax = "99"))
	int32 LivesPerPlayer = 0;

	/** Seconds of invulnerability granted after each spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn", meta = (ClampMin = "0.0", Units = "s"))
	float SpawnProtectionSeconds = 2.5f;

	/** Spawn protection ends early the first time the player fires. Prevents safe shooting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn")
	bool bSpawnProtectionBreaksOnFire = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn")
	EKCFSpawnSelectionMode SpawnSelection = EKCFSpawnSelectionMode::FarthestFromEnemies;

	/** Spawn points closer than this to a hostile are skipped when alternatives exist, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Respawn", meta = (ClampMin = "0.0", Units = "cm"))
	float MinSpawnDistanceFromEnemies = 2500.f;

	// --- Teams -------------------------------------------------------------------------------------------

	/** 0 or 1 means free-for-all. The framework imposes no upper limit beyond this clamp. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams", meta = (ClampMin = "0", ClampMax = "16"))
	int32 TeamCount = 0;

	/** Per-team display configuration. Teams beyond this array get generated defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams")
	TArray<FKCFTeamDefinition> Teams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams")
	bool bFriendlyFire = false;

	/** Fraction of normal damage applied to team-mates when friendly fire is on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams", meta = (EditCondition = "bFriendlyFire", ClampMin = "0.0", ClampMax = "1.0"))
	float FriendlyFireScale = 0.5f;

	/** Players may pick their own team subject to balance limits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams")
	bool bAllowTeamSelection = true;

	/** Maximum difference in size between the largest and smallest team. 0 disables balancing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teams", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxTeamSizeImbalance = 1;

	// --- Powerups ------------------------------------------------------------------------------------------

	/** Master switch for every powerup spawner in the arena. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Powerups")
	bool bPowerupsEnabled = true;

	/** Scales every spawner's interval. 0.5 makes powerups appear twice as often. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Powerups", meta = (EditCondition = "bPowerupsEnabled", ClampMin = "0.1", ClampMax = "10.0"))
	float PowerupSpawnIntervalScale = 1.f;

	/** Inventory is emptied on death. When false, players keep their weapon through a respawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Powerups")
	bool bClearInventoryOnDeath = true;

	// --- Misc ---------------------------------------------------------------------------------------------------

	/** Kart-to-kart impacts deal damage proportional to closing speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	bool bCollisionDamageEnabled = true;

	/** Classification tags, e.g. KCF.Mode.Ranked. Free for projects to extend. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	FGameplayTagContainer ModeTags;

	// --- Accessors -----------------------------------------------------------------------------------------------

	/** True when this configuration describes a team mode. */
	UFUNCTION(BlueprintPure, Category = "KCF|GameMode")
	bool IsTeamMode() const { return TeamCount >= 2; }

	/** Team definition for an id, generating a sensible default when unauthored. */
	UFUNCTION(BlueprintPure, Category = "KCF|GameMode")
	FKCFTeamDefinition GetTeamDefinition(int32 TeamId) const;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFGameMode"); }
	//~ End UKCFDataAssetBase
};
