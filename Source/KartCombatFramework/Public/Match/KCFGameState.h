// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/GameStateBase.h"
#include "KCFGameState.generated.h"

class AKCFPlayerState;
class UKCFGameModeDataAsset;

/** One line of the kill feed. Small, bounded, and replicated as part of a ring buffer. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFKillFeedEntry
{
	GENERATED_BODY()

	/**
	 * Monotonically increasing id assigned by the server.
	 * Clients use it to tell which entries are new after a replication update, which is what makes
	 * a plain replicated array behave correctly for late joiners without needing a multicast.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	int32 EntryId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	TObjectPtr<AKCFPlayerState> Killer = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	TObjectPtr<AKCFPlayerState> Victim = nullptr;

	/** Id of the weapon data asset responsible. NAME_None for environmental deaths. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	FName WeaponId;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	bool bWasTeamKill = false;

	/** Killer and victim are the same player. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	bool bWasSuicide = false;

	/** Server time the kill occurred, so clients can age entries out consistently. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Match")
	float ServerTime = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFMatchStateChangedSignature, EKCFMatchState, OldState, EKCFMatchState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFCountdownSignature, float, SecondsRemaining);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFKillFeedSignature, const FKCFKillFeedEntry&, Entry);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFSimpleMatchSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFMatchFinishedSignature, EKCFMatchEndReason, Reason, int32, WinningTeamId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFTeamScoreChangedSignature, int32, TeamId);

/**
 * Match-level state, replicated to every connected client including spectators and late joiners.
 *
 * This is the only class a HUD needs for match information. The game mode exists solely on the
 * server and is never referenced by client code; everything the client is allowed to know about the
 * match arrives here as a replicated property, which is what makes a mid-match join produce a
 * correct HUD with no special-case code.
 *
 * Timers are replicated as absolute server timestamps rather than countdown values. A countdown
 * float would have to be resent every tick to stay accurate; an end-time is sent once and every
 * client derives a smooth, drift-free countdown from it locally.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AKCFGameState();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	// --- Delegates ------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFMatchStateChangedSignature OnMatchStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFCountdownSignature OnCountdownStarted;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFSimpleMatchSignature OnMatchStarted;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFSimpleMatchSignature OnSuddenDeathStarted;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFMatchFinishedSignature OnMatchFinished;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFKillFeedSignature OnKillFeedEntryAdded;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Match")
	FKCFTeamScoreChangedSignature OnTeamScoreChanged;

	// --- Queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	EKCFMatchState GetMatchState() const { return MatchState; }

	/** True while damage, scoring and driving input are all live. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	bool IsMatchActive() const;

	/**
	 * Seconds left in the current phase, derived locally from the replicated end timestamp.
	 * Returns 0 when the current phase has no time limit.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	float GetRemainingStateTime() const;

	/** Seconds remaining in the match proper. Equivalent to GetRemainingStateTime while live. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	float GetRemainingMatchTime() const;

	/** Formatted M:SS for direct display. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	FText GetFormattedRemainingTime() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	const TArray<FKCFTeamScore>& GetTeamScores() const { return TeamScores; }

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	int32 GetTeamScore(int32 TeamId) const;

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	const TArray<FKCFKillFeedEntry>& GetKillFeed() const { return KillFeed; }

	/** Player states sorted by score, then kills, then fewest deaths. Allocates; for UI use. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	TArray<AKCFPlayerState*> GetSortedPlayerStates() const;

	/** Players currently connected and participating (excludes pure spectators). */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	int32 GetParticipantCount() const;

	/** Participants who have not been eliminated. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	int32 GetAlivePlayerCount() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	int32 GetWinningTeamId() const { return WinningTeamId; }

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	AKCFPlayerState* GetWinningPlayer() const { return WinningPlayer; }

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	EKCFMatchEndReason GetMatchEndReason() const { return MatchEndReason; }

	/** The rule set this match is running. May be null on a client until it replicates. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	UKCFGameModeDataAsset* GetGameModeConfig() const { return GameModeConfig; }

	// --- Server-only mutators -----------------------------------------------------------------------
	// Called exclusively by AKCFGameMode. None of these is reachable from a client.

	/** Applies a validated state transition and stamps the phase end time. */
	void SetMatchState(EKCFMatchState NewState, float StateDurationSeconds);

	/** Publishes the rule set so clients can configure their HUD. */
	void SetGameModeConfig(UKCFGameModeDataAsset* InConfig);

	/** Creates the per-team score lines for a team match. Clears them for free-for-all. */
	void InitialiseTeams(int32 TeamCount);

	void AddTeamScore(int32 TeamId, int32 Delta);

	/** Refreshes the player/alive counts on each team line. */
	void RefreshTeamPopulations();

	/** Appends a kill feed entry, trimming the oldest when the buffer is full. */
	void AddKillFeedEntry(AKCFPlayerState* Killer, AKCFPlayerState* Victim, FName WeaponId, bool bTeamKill);

	void SetMatchResult(EKCFMatchEndReason Reason, int32 InWinningTeamId, AKCFPlayerState* InWinningPlayer);

protected:
	/** Entries beyond this are dropped from the front. Bounds both memory and replication cost. */
	static constexpr int32 MaxKillFeedEntries = 8;

	UPROPERTY(ReplicatedUsing = OnRep_MatchState, BlueprintReadOnly, Category = "KCF|Match")
	EKCFMatchState MatchState = EKCFMatchState::WaitingForPlayers;

	/** Previous state, kept so OnRep can report a meaningful transition rather than just a value. */
	EKCFMatchState PreviousMatchState = EKCFMatchState::WaitingForPlayers;

	/**
	 * Server world time at which the current phase ends. Negative means the phase is open-ended.
	 * Clients compare against GetServerWorldTimeSeconds(), which the engine already keeps in sync.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Match")
	float StateEndServerTime = -1.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Match")
	TObjectPtr<UKCFGameModeDataAsset> GameModeConfig = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_TeamScores, BlueprintReadOnly, Category = "KCF|Match")
	TArray<FKCFTeamScore> TeamScores;

	/**
	 * Ring buffer of recent kills. A plain replicated array is used rather than a fast array: the
	 * buffer is capped at eight small entries, so delta-serialisation bookkeeping would cost more
	 * than it saves, and a plain array means a late joiner receives the current feed for free.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_KillFeed, BlueprintReadOnly, Category = "KCF|Match")
	TArray<FKCFKillFeedEntry> KillFeed;

	/** Highest EntryId the local client has already broadcast, so OnRep only reports new kills. */
	int32 LastBroadcastKillFeedId = 0;

	/** Next id to hand out. Server-only. */
	int32 NextKillFeedId = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Match")
	int32 WinningTeamId = KCF_NO_TEAM;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Match")
	TObjectPtr<AKCFPlayerState> WinningPlayer = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Match")
	EKCFMatchEndReason MatchEndReason = EKCFMatchEndReason::Unknown;

	UFUNCTION() void OnRep_MatchState();
	UFUNCTION() void OnRep_TeamScores();
	UFUNCTION() void OnRep_KillFeed();

	/** Raises the delegate appropriate to the state that was just entered. */
	void BroadcastStateEntered(EKCFMatchState OldState, EKCFMatchState NewState);
};
