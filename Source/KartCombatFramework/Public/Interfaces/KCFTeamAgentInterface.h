// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "UObject/Interface.h"
#include "KCFTeamAgentInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UKCFTeamAgent : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that belongs to a team: pawns, controllers, player states, turrets, projectiles.
 *
 * The framework never assumes two teams. KCF_NO_TEAM means "free-for-all participant", and two
 * KCF_NO_TEAM actors are hostile to each other, which is what makes FFA fall out of the same code
 * path as team modes instead of needing a parallel one.
 */
class KARTCOMBATFRAMEWORK_API IKCFTeamAgent
{
	GENERATED_BODY()

public:
	/** Team index, or KCF_NO_TEAM for free-for-all / unassigned. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Team")
	int32 GetKCFTeamId() const;

	/** Server-side assignment. Implementations replicate the value themselves. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "KCF|Team")
	void SetKCFTeamId(int32 NewTeamId);

	/**
	 * Resolves the team id of any actor by walking Actor -> Pawn -> Controller -> PlayerState and
	 * testing each for this interface. Returns KCF_NO_TEAM when nothing in the chain has a team.
	 */
	static int32 ResolveTeamId(const AActor* Actor);

	/**
	 * Relationship between two actors. Two KCF_NO_TEAM actors are Hostile (free-for-all);
	 * an actor is always Friendly to itself.
	 */
	static EKCFTeamAttitude GetAttitude(const AActor* From, const AActor* To);
};
