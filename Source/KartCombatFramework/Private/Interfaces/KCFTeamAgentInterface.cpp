// Original work, MIT licensed.

#include "Interfaces/KCFTeamAgentInterface.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

namespace
{
	/** Tests one object for the team interface without requiring it to be non-null. */
	bool TryGetTeamId(const UObject* Object, int32& OutTeamId)
	{
		if (Object && Object->GetClass()->ImplementsInterface(UKCFTeamAgent::StaticClass()))
		{
			OutTeamId = IKCFTeamAgent::Execute_GetKCFTeamId(const_cast<UObject*>(Object));
			return true;
		}
		return false;
	}
}

int32 IKCFTeamAgent::ResolveTeamId(const AActor* Actor)
{
	int32 TeamId = KCF_NO_TEAM;
	if (!Actor)
	{
		return TeamId;
	}

	// The actor itself wins, so that a projectile can carry the team of whoever fired it even though
	// it has no controller.
	if (TryGetTeamId(Actor, TeamId))
	{
		return TeamId;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (TryGetTeamId(Pawn->GetPlayerState(), TeamId))
		{
			return TeamId;
		}
		if (TryGetTeamId(Pawn->GetController(), TeamId))
		{
			return TeamId;
		}
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		if (TryGetTeamId(Controller->PlayerState, TeamId))
		{
			return TeamId;
		}
	}

	// Fall back to the owning actor chain (projectiles set their owner to the firing kart).
	if (const AActor* Owner = Actor->GetOwner())
	{
		return ResolveTeamId(Owner);
	}

	return KCF_NO_TEAM;
}

EKCFTeamAttitude IKCFTeamAgent::GetAttitude(const AActor* From, const AActor* To)
{
	if (!From || !To)
	{
		return EKCFTeamAttitude::Neutral;
	}

	if (From == To)
	{
		return EKCFTeamAttitude::Friendly;
	}

	const int32 FromTeam = ResolveTeamId(From);
	const int32 ToTeam = ResolveTeamId(To);

	// Free-for-all: everyone without a team is hostile to everyone else without a team, which keeps
	// FFA and team modes on a single code path.
	if (FromTeam == KCF_NO_TEAM || ToTeam == KCF_NO_TEAM)
	{
		return EKCFTeamAttitude::Hostile;
	}

	return (FromTeam == ToTeam) ? EKCFTeamAttitude::Friendly : EKCFTeamAttitude::Hostile;
}
