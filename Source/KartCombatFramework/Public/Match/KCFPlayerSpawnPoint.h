// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "GameplayTagContainer.h"
#include "KCFPlayerSpawnPoint.generated.h"

/**
 * A kart spawn location.
 *
 * Extends APlayerStart rather than replacing it so that an existing level's player starts keep
 * working and a project can mix both. The extra fields are what the scoring spawn selector needs:
 * which team may use the point, whether it is currently enabled, and when it was last used.
 *
 * This actor never replicates. Spawn selection is a server-only decision and clients only ever see
 * its result as a pawn transform.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPlayerSpawnPoint : public APlayerStart
{
	GENERATED_BODY()

public:
	AKCFPlayerSpawnPoint(const FObjectInitializer& ObjectInitializer);

	/** Team allowed to use this point. KCF_NO_TEAM means any team may use it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Spawn")
	int32 TeamId = INDEX_NONE;

	/** Classification used by game modes to filter points (grid positions, safe zones, and so on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Spawn")
	FGameplayTagContainer SpawnTags;

	/** Disabled points are skipped entirely. Useful for mode-specific or phase-specific spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Spawn")
	bool bEnabled = true;

	/**
	 * Ordering hint for Sequential selection and for race grids. Lower spawns first.
	 * Ties fall back to the order the level streamed the actors in, which is stable within a map.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Spawn")
	int32 SpawnOrder = 0;

	/** Bonus added to this point's selection score, letting a designer favour certain spots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Spawn")
	float ScoreBias = 0.f;

	/** True when this point may be used by the given team. */
	UFUNCTION(BlueprintPure, Category = "KCF|Spawn")
	bool IsUsableByTeam(int32 InTeamId) const;

	/** World time this point was last spawned on. Negative when never used. Server-only state. */
	float GetLastUsedTime() const { return LastUsedTime; }

	/** Stamps the point as just used so the selector can avoid reusing it immediately. */
	void MarkUsed(float WorldTime) { LastUsedTime = WorldTime; }

private:
	float LastUsedTime = -1000.f;
};
