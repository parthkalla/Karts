// Original work, MIT licensed.

#include "Match/KCFPlayerSpawnPoint.h"

#include "Core/KCFTypes.h"

AKCFPlayerSpawnPoint::AKCFPlayerSpawnPoint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// Spawn selection happens entirely on the server; replicating these actors would cost bandwidth
	// for information no client can act on.
	bReplicates = false;
	SetNetDormancy(DORM_DormantAll);
}

bool AKCFPlayerSpawnPoint::IsUsableByTeam(int32 InTeamId) const
{
	if (!bEnabled)
	{
		return false;
	}

	// An untagged point serves everyone, which keeps a free-for-all map working with no setup.
	return TeamId == KCF_NO_TEAM || InTeamId == KCF_NO_TEAM || TeamId == InTeamId;
}
