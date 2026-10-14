// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "KCFRespawnComponent.generated.h"

class AController;
class AKCFGameMode;
class AKCFPlayerSpawnPoint;

/** One queued respawn. Server-only; never replicated. */
USTRUCT()
struct KARTCOMBATFRAMEWORK_API FKCFRespawnRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AController> Controller;

	/** World time at which this request becomes serviceable. */
	UPROPERTY()
	float EarliestRespawnTime = 0.f;
};

/**
 * The respawn queue, owned by the game mode and therefore server-only by construction.
 *
 * A single repeating timer services the whole queue rather than one timer per dead player. With
 * eight players that is a difference of one timer against eight, and more importantly it gives a
 * single deterministic ordering when several players die in the same explosion.
 *
 * Spawn point selection is a scoring pass rather than a filter, so a crowded arena always produces
 * *a* spawn rather than failing to find a "safe" one.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFRespawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFRespawnComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Queues a respawn for a controller after DelaySeconds.
	 * Re-queuing a controller already in the queue updates its time rather than adding a duplicate.
	 */
	void EnqueueRespawn(AController* Controller, float DelaySeconds);

	/** Removes any pending respawn for a controller, e.g. because they were eliminated or left. */
	void CancelRespawn(AController* Controller);

	/** Services a single controller immediately, bypassing the delay. Used by the respawn request. */
	bool RespawnNow(AController* Controller);

	/** Clears the queue. Called when the match ends. */
	void ClearQueue();

	/**
	 * Chooses the best spawn point for a controller under the configured selection mode.
	 * Returns null only when the level contains no usable spawn point at all.
	 */
	AKCFPlayerSpawnPoint* SelectSpawnPoint(AController* Controller) const;

	/** How often the queue is serviced, seconds. Lower is more responsive, higher is cheaper. */
	UPROPERTY(EditDefaultsOnly, Category = "KCF|Respawn", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float QueueServiceInterval = 0.25f;

	/** A point used within this many seconds is penalised, to stop two players sharing a spawn. */
	UPROPERTY(EditDefaultsOnly, Category = "KCF|Respawn", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float RecentUseWindowSeconds = 4.f;

protected:
	/** Owning game mode. Cached at BeginPlay; this component is only ever placed on a game mode. */
	UPROPERTY(Transient)
	TObjectPtr<AKCFGameMode> OwningGameMode;

	UPROPERTY(Transient)
	TArray<FKCFRespawnRequest> Queue;

	/** Round-robin cursor for EKCFSpawnSelectionMode::Sequential. */
	int32 SequentialCursor = 0;

	FTimerHandle QueueTimerHandle;

	/** Runs on the queue timer. Respawns every request whose time has come, in queue order. */
	void ServiceQueue();

	/** Scores one candidate point. Higher is better. */
	float ScoreSpawnPoint(const AKCFPlayerSpawnPoint& Point, AController* Controller, int32 TeamId) const;

	/** Collects every enabled spawn point usable by a team. */
	void GatherCandidates(int32 TeamId, TArray<AKCFPlayerSpawnPoint*>& OutCandidates) const;

	/** Starts or stops the service timer so it only runs while the queue is non-empty. */
	void UpdateTimerState();
};
