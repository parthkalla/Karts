// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "KCFPlayerState.generated.h"

class AKCFPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFScoreChangedSignature, AKCFPlayerState*, PlayerState, int32, NewScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFTeamChangedSignature, AKCFPlayerState*, PlayerState, int32, NewTeamId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPlayerStatsChangedSignature, AKCFPlayerState*, PlayerState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFReadyStateChangedSignature, AKCFPlayerState*, PlayerState, bool, bIsReady);

/**
 * Per-player match state, replicated to every client.
 *
 * Everything here is written on the server only. The setters are all authority-guarded and there is
 * no RPC on this class at all, because there is nothing here a client has any business changing -
 * score, kills and team assignment are exactly the values a cheating client would want to write.
 *
 * Replicated statistics are deliberately grouped so a single kill produces one property bunch rather
 * than six. Scoreboards poll these values; they do not need to be up to date within a frame, which
 * is why this class replicates at a low frequency.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPlayerState : public APlayerState, public IKCFTeamAgent
{
	GENERATED_BODY()

public:
	AKCFPlayerState();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	//~ Begin APlayerState
	/** Carries framework statistics across seamless travel and across a reconnect. */
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;
	//~ End APlayerState

	//~ Begin IKCFTeamAgent
	virtual int32 GetKCFTeamId_Implementation() const override { return TeamId; }
	virtual void SetKCFTeamId_Implementation(int32 NewTeamId) override;
	//~ End IKCFTeamAgent

	// --- Delegates ------------------------------------------------------------------------------

	/** Fires on the server and on every client that receives the new score. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFScoreChangedSignature OnScoreChanged;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFTeamChangedSignature OnTeamChanged;

	/** Fires when kills, deaths, assists or damage totals change. Scoreboards bind to this. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFPlayerStatsChangedSignature OnStatsChanged;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFReadyStateChangedSignature OnReadyStateChanged;

	/** Fires when the player is eliminated for the rest of the match. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFPlayerStatsChangedSignature OnEliminated;

	// --- Read-only accessors ----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetKCFScore() const { return KCFScore; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetKills() const { return Kills; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetDeaths() const { return Deaths; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetAssists() const { return Assists; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetTeamKills() const { return TeamKills; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") float GetDamageDealt() const { return DamageDealt; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") float GetDamageReceived() const { return DamageReceived; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetPowerupsCollected() const { return PowerupsCollected; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetCurrentKillStreak() const { return CurrentKillStreak; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetBestKillStreak() const { return BestKillStreak; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetPlacement() const { return Placement; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetLivesRemaining() const { return LivesRemaining; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") bool IsEliminated() const { return bEliminated; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") bool IsReady() const { return bReady; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") int32 GetTeamId() const { return TeamId; }
	UFUNCTION(BlueprintPure, Category = "KCF|Player") FName GetSelectedVehicleId() const { return SelectedVehicleId; }

	/** Kills divided by deaths, counting a zero-death player as having one death. */
	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	float GetKillDeathRatio() const;

	/** Server time at which this player last died. Negative when they have not died. */
	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	float GetLastDeathServerTime() const { return LastDeathServerTime; }

	// --- Server-only mutators -----------------------------------------------------------------------
	// Every one of these asserts authority and is a no-op with a warning on a client. There is no
	// client path to any of them: an RPC that let a client call AddScore would be the single most
	// valuable exploit in the game.

	/** Adds (or removes, for a negative delta) score. */
	void AddScore(int32 Delta);

	/** Records a kill against this player's statistics and updates their streak. */
	void RecordKill(bool bWasTeamKill);

	/** Records a death, resets the kill streak, and decrements lives when lives are in use. */
	void RecordDeath();

	void RecordAssist();
	void AddDamageDealt(float Amount);
	void AddDamageReceived(float Amount);
	void RecordPowerupCollected();

	/** Sets remaining lives. A value of 0 with elimination enabled eliminates the player. */
	void SetLivesRemaining(int32 NewLives);

	/** Marks the player out for the remainder of the match. */
	void SetEliminated(bool bNewEliminated);

	/** Final or live placement, 1-based. 0 means unranked. */
	void SetPlacement(int32 NewPlacement);

	/** Lobby ready flag. Set from AKCFPlayerController::ServerSetReady after validation. */
	void SetReady(bool bNewReady);

	/**
	 * Records the vehicle the player asked for. This is a *request*: the server validates the id
	 * against the catalogue before spawning anything, so an edited client can only ever select a
	 * vehicle that genuinely exists.
	 */
	void SetSelectedVehicleId(FName NewVehicleId);

	/** Clears per-match counters. Called by the game mode when a new match begins. */
	void ResetMatchStats(int32 StartingLives);

	/** Stamps the server time of death, used by the respawn queue and the assist window. */
	void MarkDeathTime(float ServerTime);

protected:
	// --- Replicated state --------------------------------------------------------------------------

	/**
	 * Framework score. Deliberately separate from APlayerState::Score: that field is a float on a
	 * class the engine also writes to, and an int32 of our own avoids both float drift on a
	 * scoreboard and any ambiguity about who owns the value.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_KCFScore, BlueprintReadOnly, Category = "KCF|Player")
	int32 KCFScore = 0;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId, BlueprintReadOnly, Category = "KCF|Player")
	int32 TeamId = KCF_NO_TEAM;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 Kills = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 Deaths = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 Assists = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 TeamKills = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 PowerupsCollected = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 CurrentKillStreak = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	int32 BestKillStreak = 0;

	/**
	 * Damage totals are quantised to whole points before replication. A scoreboard never shows
	 * fractional damage, and rounding here stops a trickle of 0.01 deltas from waking the actor's
	 * replication every tick during a sustained burn effect.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	float DamageDealt = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Stats, BlueprintReadOnly, Category = "KCF|Player")
	float DamageReceived = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Player")
	int32 LivesRemaining = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Eliminated, BlueprintReadOnly, Category = "KCF|Player")
	bool bEliminated = false;

	UPROPERTY(ReplicatedUsing = OnRep_Ready, BlueprintReadOnly, Category = "KCF|Player")
	bool bReady = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Player")
	int32 Placement = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Player")
	FName SelectedVehicleId;

	/**
	 * Server timestamp of the most recent death. Only the owning client needs it (to drive a
	 * respawn countdown), so it is owner-only rather than broadcast to the whole lobby.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Player")
	float LastDeathServerTime = -1.f;

	// --- OnRep handlers ------------------------------------------------------------------------------

	UFUNCTION() void OnRep_KCFScore();
	UFUNCTION() void OnRep_TeamId();
	UFUNCTION() void OnRep_Stats();
	UFUNCTION() void OnRep_Eliminated();
	UFUNCTION() void OnRep_Ready();

	/** Single place that raises OnStatsChanged, used by both the server setters and OnRep_Stats. */
	void BroadcastStatsChanged();
};
