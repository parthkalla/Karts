// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Match/KCFGameMode.h"
#include "KCFRaceGameMode.generated.h"

class AKCFKartPawn;
class AKCFRaceCheckpoint;
class UKCFRaceProgressComponent;

/**
 * Lap racing with weapons.
 *
 * The rules this adds to the base game mode are progress, not combat: laps, gate ordering, race
 * position, and finishing. Damage, powerups, respawn and the match clock are unchanged, which is
 * why the class is as small as it is.
 *
 * Progress lives on a UKCFRaceProgressComponent that this mode attaches to each player state when
 * they join, rather than on AKCFPlayerState itself. A deathmatch has no laps, and a permanent set
 * of race fields would be replicated to every client in every mode for nothing.
 *
 * A circuit is authored by placing AKCFRaceCheckpoint actors with increasing CheckpointIndex and
 * marking one of them bIsFinishLine. The mode sorts them once at BeginPlay; from then on a lap is
 * "crossed every gate in order", which is what makes shortcutting worthless.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "KCF Race"))
class KARTCOMBATFRAMEWORK_API AKCFRaceGameMode : public AKCFGameMode
{
	GENERATED_BODY()

public:
	AKCFRaceGameMode();

	//~ Begin AActor
	virtual void BeginPlay() override;
	//~ End AActor

	//~ Begin AGameModeBase
	virtual void PostLogin(APlayerController* NewPlayer) override;
	//~ End AGameModeBase

	//~ Begin AKCFGameMode
	virtual bool CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer) override;
	virtual void AssignFinalPlacements() override;
	virtual bool GetRespawnTransformOverride(AController* Controller, FTransform& OutTransform) const override;
	//~ End AKCFGameMode

	/** Called by a checkpoint when a kart drives through it. Server only. */
	void NotifyCheckpointReached(AKCFKartPawn* Kart, AKCFRaceCheckpoint* Checkpoint);

	/** Laps required to finish. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Race", meta = (ClampMin = "1", ClampMax = "50"))
	int32 LapsToWin = 3;

	/** Score awarded for each completed lap, so the generic scoreboard still means something. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Race")
	int32 ScorePerLap = 10;

	/** Score awarded to the winner on crossing the line. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Race")
	int32 ScoreForFinishing = 50;

	/**
	 * Seconds the remaining racers have to finish once the leader has.
	 * Zero ends the race the instant the leader crosses the line. A short window is usually better:
	 * finishing is most of the reward for the players who were not going to win.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Race", meta = (ClampMin = "0.0", Units = "s"))
	float GraceSecondsAfterLeaderFinishes = 30.f;

	/** A racer who died respawns at their last gate rather than at a generic spawn point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Race")
	bool bRespawnAtLastCheckpoint = true;

	/** The circuit, sorted by CheckpointIndex. Empty when the level has no checkpoints. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race")
	const TArray<AKCFRaceCheckpoint*>& GetCheckpoints() const { return Checkpoints; }

	/** Current race order, leader first. Rebuilt on demand rather than cached. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race")
	TArray<AKCFPlayerState*> GetRaceOrder() const;

	/** Progress component for a player state, or null outside a race. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race")
	static UKCFRaceProgressComponent* GetRaceProgress(AKCFPlayerState* PlayerState);

protected:
	/** Gates in lap order. Built once at BeginPlay; a circuit does not change during a race. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AKCFRaceCheckpoint>> Checkpoints;

	/** Finishing order, earliest first. Drives final placement directly. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AKCFPlayerState>> FinishOrder;

	/** Server time the leader finished, or negative while nobody has. */
	float LeaderFinishServerTime = -1.f;

	/** Collects and sorts the checkpoints, reporting a circuit that cannot work. */
	void BuildCircuit();

	/** Adds the progress component to a player state that does not have one yet. */
	UKCFRaceProgressComponent* EnsureRaceProgress(AKCFPlayerState* PlayerState);

	/** Ordinal of a checkpoint in the sorted list, or INDEX_NONE. */
	int32 FindCheckpointOrdinal(const AKCFRaceCheckpoint* Checkpoint) const;

	/** Handles a racer crossing the final gate of their final lap. */
	void HandleRacerFinished(AKCFPlayerState* PlayerState);
};
