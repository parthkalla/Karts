// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/GameModeBase.h"
#include "KCFGameMode.generated.h"

class AKCFGameState;
class AKCFKartPawn;
class AKCFPlayerState;
class UDataTable;
class UKCFGameModeDataAsset;
class UKCFRespawnComponent;
class UKCFVehicleDataAsset;

/**
 * Base match authority for the framework. Exists only on the server.
 *
 * Nothing in the framework holds a pointer to the game mode from client-reachable code. Everything
 * a client is allowed to know arrives through AKCFGameState, which is what makes "the client cannot
 * see the rules engine" a structural property rather than a convention.
 *
 * The match clock is a single 1 Hz repeating timer, not Tick. Per-frame precision buys nothing for
 * a clock displayed to the second and would cost a tick on a server already running the physics for
 * every kart.
 *
 * Subclasses customise rules by overriding the virtuals in the "Rules" section. The shipped
 * subclasses (free-for-all, team deathmatch, elimination) are small for exactly that reason.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKCFGameMode();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	//~ Begin AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	//~ End AGameModeBase

	// --- Configuration --------------------------------------------------------------------------

	/**
	 * The rule set for this match. Set it on the game mode Blueprint, or leave it unset to fall back
	 * to the project default from UKCFDeveloperSettings.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Configuration")
	TObjectPtr<UKCFGameModeDataAsset> GameModeConfig;

	/** Kart pawn class spawned for players. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Configuration")
	TSubclassOf<AKCFKartPawn> KartPawnClass;

	/**
	 * FKCFVehicleRow table of vehicles players may select.
	 * This is the authority on what a client is allowed to ask for; a vehicle id not in this table
	 * is rejected outright.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Configuration")
	TObjectPtr<UDataTable> VehicleCatalogue;

	/** Used when a player has selected nothing, or selected something that failed validation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Configuration")
	TObjectPtr<UKCFVehicleDataAsset> FallbackVehicle;

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	UKCFGameModeDataAsset* GetGameModeConfig() const { return GameModeConfig; }

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	AKCFGameState* GetKCFGameState() const;

	// --- Match flow ------------------------------------------------------------------------------

	/** Forces the match to begin even if MinPlayers is not met. For hosts and for testing. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Match")
	void ForceStartMatch();

	/** Ends the match immediately with the given reason. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Match")
	void EndMatch(EKCFMatchEndReason Reason);

	/** Transitions to a new state, obeying the state machine's transition table. */
	void RequestMatchState(EKCFMatchState NewState);

	// --- Gameplay entry points (server only) -----------------------------------------------------------

	/**
	 * Reports a kart death. Called by UKCFHealthComponent through the damage pipeline.
	 * Handles scoring, the kill feed, elimination, lives, win conditions and the respawn queue.
	 */
	void HandleKartDeath(AKCFPlayerState* Victim, AKCFPlayerState* Killer, FName WeaponId, const TArray<AKCFPlayerState*>& Assisters);

	/** Spawns and possesses a kart for a controller at a transform. Returns false on failure. */
	bool SpawnKartForController(AController* Controller, const FTransform& SpawnTransform);

	/** True when this controller is currently entitled to a respawn. */
	bool ShouldRespawnController(AController* Controller) const;

	/** Honours a validated client respawn request. */
	void RequestImmediateRespawn(AController* Controller);

	/** Honours a validated client team-change request, applying mode rules and balance limits. */
	void RequestTeamChange(AController* Controller, int32 DesiredTeamId);

	/** True when a vehicle id exists in the selectable catalogue. */
	bool IsVehicleSelectable(FName VehicleId) const;

	/** Resolves the vehicle a player should spawn in, falling back when their choice is unusable. */
	UKCFVehicleDataAsset* ResolveVehicleForPlayer(const AKCFPlayerState* PlayerState) const;

	// --- Rules (override in subclasses) ----------------------------------------------------------------

	/**
	 * Returns true when the match should end now, filling in the reason and the winner.
	 * The base implementation handles the score limit and the time limit; subclasses add their own.
	 */
	virtual bool CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer);

	/** Applies score for one elimination. Base implementation uses the configured score values. */
	virtual void ScoreElimination(AKCFPlayerState* Victim, AKCFPlayerState* Killer, const TArray<AKCFPlayerState*>& Assisters);

	/** Chooses a team for a newly joined player. Base implementation fills the smallest team. */
	virtual int32 AssignTeamForPlayer(AKCFPlayerState* PlayerState) const;

	/** Hook for subclasses that need to react to a player being permanently eliminated. */
	virtual void OnPlayerEliminated(AKCFPlayerState* PlayerState);

	/**
	 * Called after the state change has been applied and the base class's phase work has run.
	 * The C++ counterpart of BP_OnMatchStateChanged, which fires immediately after it.
	 */
	virtual void OnMatchStateChanged(EKCFMatchState OldState, EKCFMatchState NewState);

	/** Computes and stores final placements. Called once when the match finishes. */
	virtual void AssignFinalPlacements();

	/**
	 * Lets a mode place a respawning player somewhere the generic spawn-point selector would never
	 * choose - a race returns a kart to the last gate it passed, a zone mode puts it back inside
	 * the safe area. Return false to use the normal selection.
	 *
	 * Consulted by UKCFRespawnComponent before it scores spawn points.
	 */
	virtual bool GetRespawnTransformOverride(AController* Controller, FTransform& OutTransform) const;

	// --- Blueprint hooks -----------------------------------------------------------------------------------
	// Server-side events a project can extend without subclassing in C++.

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Match", meta = (DisplayName = "On Match State Changed"))
	void BP_OnMatchStateChanged(EKCFMatchState OldState, EKCFMatchState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Match", meta = (DisplayName = "On Kart Death"))
	void BP_OnKartDeath(AKCFPlayerState* Victim, AKCFPlayerState* Killer);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Match", meta = (DisplayName = "On Player Joined"))
	void BP_OnPlayerJoined(AKCFPlayerState* PlayerState);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Match", meta = (DisplayName = "On Player Left"))
	void BP_OnPlayerLeft(AKCFPlayerState* PlayerState);

protected:
	/** The respawn queue. Created in the constructor; server-only by virtue of living here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Match")
	TObjectPtr<UKCFRespawnComponent> RespawnComponent;

	/** The single match clock. Fires once per second; see the class comment for why it is not Tick. */
	FTimerHandle MatchClockHandle;

	/** World time at which the lobby grace period ends. Negative when not counting down. */
	float LobbyGraceEndTime = -1.f;

	/** Prevents the win check from running after the result is already decided. */
	bool bMatchResultDecided = false;

	/** Runs once per second. Drives every phase transition. */
	void TickMatchClock();

	/** Evaluates whether the lobby may advance to Preparing. */
	void UpdateWaitingForPlayers();

	/** Applies the state change, starts phase timers and spawns or freezes pawns as appropriate. */
	void EnterMatchState(EKCFMatchState NewState);

	/** Spawns a kart for every participant who does not have one. */
	void SpawnAllPlayers();

	/** Enables or disables driving input on every live kart, used by the countdown and the results. */
	void SetAllKartsMovementEnabled(bool bEnabled);

	/**
	 * Starts or stops every auto-start powerup spawner in the level.
	 * Crates are tied to the live phases of the match so that a lobby is not already littered with
	 * weapons before the countdown, and so the arena is clear on the results screen.
	 */
	void SetPowerupSpawnersEnabled(bool bEnabled, bool bRemoveExistingPickups = false);

	/**
	 * Tells the installed session backend the match is live or over.
	 * Routed through UKCFSessionSubsystem, so the game mode stays free of any online subsystem: a
	 * listen server stops advertising, an EOS backend calls StartSession, a standalone game does
	 * nothing at all.
	 */
	void NotifySessionMatchStarted();
	void NotifySessionMatchEnded();

	/** Returns the effective rule set, never null: falls back to a transient default when unset. */
	const UKCFGameModeDataAsset* GetEffectiveConfig() const;

	/** Number of players on a team right now. */
	int32 GetTeamPlayerCount(int32 TeamId) const;

private:
	/**
	 * Lazily created stand-in used when no configuration asset is supplied anywhere.
	 * Its existence is what lets the plugin be dropped into a project and played immediately, and
	 * it is reported once as a warning so nobody ships on it by accident.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UKCFGameModeDataAsset> FallbackConfig;

	const UKCFGameModeDataAsset* GetOrCreateFallbackConfig() const;
};
