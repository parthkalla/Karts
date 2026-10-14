// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KCFRaceProgressComponent.generated.h"

class AKCFRaceCheckpoint;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFLapCompletedSignature, int32, CompletedLap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFRaceProgressChangedSignature);

/**
 * One player's progress around a circuit.
 *
 * Lives on the player state, but is added at runtime by AKCFRaceGameMode rather than being a
 * permanent part of it. Lap counters have no meaning in a deathmatch, and a permanent component
 * would mean every arena mode replicating five race fields to every client for nothing.
 *
 * Written on the server only. The whole component replicates to everyone because a race HUD shows
 * every competitor's lap and position, not just the local player's.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFRaceProgressComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFRaceProgressComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	UPROPERTY(BlueprintAssignable, Category = "KCF|Race")
	FKCFLapCompletedSignature OnLapCompleted;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Race")
	FKCFRaceProgressChangedSignature OnProgressChanged;

	// --- Queries ---------------------------------------------------------------------------------

	/** Laps fully completed. A racer on their first lap reports 0. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") int32 GetCompletedLaps() const { return CompletedLaps; }

	/** Index into the sorted checkpoint list that this racer must cross next. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") int32 GetNextCheckpointOrdinal() const { return NextCheckpointOrdinal; }

	/** Gates crossed in total, across every lap. The primary sort key for race position. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") int32 GetTotalCheckpointsPassed() const { return TotalCheckpointsPassed; }

	/** Fastest completed lap, seconds. Negative until a lap is finished. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") float GetBestLapSeconds() const { return BestLapSeconds; }

	/** Most recently completed lap, seconds. Negative until a lap is finished. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") float GetLastLapSeconds() const { return LastLapSeconds; }

	/** Total race time for a racer who has finished, seconds. Negative while still racing. */
	UFUNCTION(BlueprintPure, Category = "KCF|Race") float GetTotalRaceSeconds() const { return TotalRaceSeconds; }

	UFUNCTION(BlueprintPure, Category = "KCF|Race") bool HasFinished() const { return bFinished; }

	/** The gate this racer respawns at. Null before the first gate is crossed. */
	AKCFRaceCheckpoint* GetLastCheckpoint() const { return LastCheckpoint.Get(); }

	// --- Server-only mutators -----------------------------------------------------------------------

	/** Resets to the grid. Called when the race starts. */
	void BeginRace(float ServerTime);

	/**
	 * Records a gate crossing. Returns true when this completed a lap.
	 * @param Ordinal          Position of the gate in the sorted list.
	 * @param CheckpointCount  Number of gates on the circuit.
	 */
	bool RecordCheckpoint(AKCFRaceCheckpoint* Checkpoint, int32 Ordinal, int32 CheckpointCount, float ServerTime);

	/** Marks the racer as finished and stamps their total time. */
	void MarkFinished(float ServerTime);

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Progress, BlueprintReadOnly, Category = "KCF|Race")
	int32 CompletedLaps = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progress, BlueprintReadOnly, Category = "KCF|Race")
	int32 NextCheckpointOrdinal = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Progress, BlueprintReadOnly, Category = "KCF|Race")
	int32 TotalCheckpointsPassed = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Race")
	float BestLapSeconds = -1.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Race")
	float LastLapSeconds = -1.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Race")
	float TotalRaceSeconds = -1.f;

	UPROPERTY(ReplicatedUsing = OnRep_Progress, BlueprintReadOnly, Category = "KCF|Race")
	bool bFinished = false;

	/** Server-only: the gate a dead racer comes back at. Clients have no use for it. */
	TWeakObjectPtr<AKCFRaceCheckpoint> LastCheckpoint;

	/** Server time the current lap began. */
	float CurrentLapStartTime = -1.f;

	/** Server time the race began, for the total. */
	float RaceStartTime = -1.f;

	/** Last lap number OnLapCompleted was raised for, so a mid-lap update does not re-raise it. */
	int32 LastBroadcastLap = 0;

	UFUNCTION() void OnRep_Progress();
};
