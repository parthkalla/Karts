// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Match/KCFGameMode.h"
#include "KCFGameModes.generated.h"

/**
 * Free-for-all. Every player is hostile to every other player and the highest score wins.
 *
 * The class is almost empty on purpose: the base game mode already treats KCF_NO_TEAM players as
 * mutually hostile and already implements a score limit, so free-for-all needs no rules of its own.
 * It exists as a distinct class so a project can select it by name and extend it in Blueprint.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "KCF Free For All"))
class KARTCOMBATFRAMEWORK_API AKCFFreeForAllGameMode : public AKCFGameMode
{
	GENERATED_BODY()

public:
	AKCFFreeForAllGameMode();
};

/**
 * Team deathmatch. Score accrues to teams; the leading team at the limit or at time wins.
 *
 * Overrides team assignment only to re-balance on join, and the win condition only to resolve a
 * team tie by total kills.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "KCF Team Deathmatch"))
class KARTCOMBATFRAMEWORK_API AKCFTeamDeathmatchGameMode : public AKCFGameMode
{
	GENERATED_BODY()

public:
	AKCFTeamDeathmatchGameMode();

	//~ Begin AKCFGameMode
	virtual bool CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer) override;
	//~ End AKCFGameMode

protected:
	/** Total kills for a team, used as the tiebreak when two teams share the top score. */
	int32 GetTeamKillTotal(int32 TeamId) const;
};

/**
 * Elimination. Players have a fixed number of lives; the last player or team standing wins.
 *
 * This is the mode that justifies the elimination plumbing in the base class: it overrides the win
 * condition to end the match the moment only one side remains, and it keeps eliminated players
 * connected as spectators rather than removing them.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "KCF Elimination"))
class KARTCOMBATFRAMEWORK_API AKCFEliminationGameMode : public AKCFGameMode
{
	GENERATED_BODY()

public:
	AKCFEliminationGameMode();

	//~ Begin AKCFGameMode
	virtual bool CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer) override;
	virtual void OnPlayerEliminated(AKCFPlayerState* PlayerState) override;
	virtual void AssignFinalPlacements() override;
	//~ End AKCFGameMode

protected:
	/**
	 * Elimination order, earliest first. Placement is this list reversed: the last player standing
	 * is first, the first player out is last. Without recording the order, two players eliminated in
	 * the same match would be indistinguishable on the results screen.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AKCFPlayerState>> EliminationOrder;

	/** Counts sides still in the match. Returns teams for a team mode, players otherwise. */
	int32 CountRemainingSides(int32& OutLastRemainingTeamId, AKCFPlayerState*& OutLastRemainingPlayer) const;
};
