// Original work, MIT licensed.

#include "Match/KCFGameModes.h"

#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Match/KCFGameState.h"
#include "Player/KCFPlayerState.h"

// -------------------------------------------------------------------------------------------------
// Free-for-all
// -------------------------------------------------------------------------------------------------

AKCFFreeForAllGameMode::AKCFFreeForAllGameMode()
{
	// No construction-time configuration: every rule comes from the game mode Data Asset. Setting
	// defaults here would create a second place to look when a value is wrong.
}

// -------------------------------------------------------------------------------------------------
// Team deathmatch
// -------------------------------------------------------------------------------------------------

AKCFTeamDeathmatchGameMode::AKCFTeamDeathmatchGameMode()
{
}

int32 AKCFTeamDeathmatchGameMode::GetTeamKillTotal(int32 TeamId) const
{
	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return 0;
	}

	int32 Total = 0;
	for (const APlayerState* Entry : KCFGameState->PlayerArray)
	{
		const AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (KCFPlayer && KCFPlayer->GetTeamId() == TeamId)
		{
			Total += KCFPlayer->GetKills();
		}
	}
	return Total;
}

bool AKCFTeamDeathmatchGameMode::CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer)
{
	// The base class handles the score limit and records the leading player. Everything below only
	// refines the *tied* case, which the base deliberately reports as "no winner yet".
	if (Super::CheckWinCondition(OutReason, OutWinningTeamId, OutWinningPlayer))
	{
		return true;
	}

	const AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState || OutWinningTeamId != KCF_NO_TEAM)
	{
		return false;
	}

	// Resolve a score tie by total kills so a time-limit finish still produces a winner rather than
	// an unsatisfying draw.
	int32 BestTeam = KCF_NO_TEAM;
	int32 BestScore = MIN_int32;
	int32 BestKills = MIN_int32;
	bool bStillTied = false;

	for (const FKCFTeamScore& Entry : KCFGameState->GetTeamScores())
	{
		const int32 Kills = GetTeamKillTotal(Entry.TeamId);

		if (Entry.Score > BestScore || (Entry.Score == BestScore && Kills > BestKills))
		{
			BestScore = Entry.Score;
			BestKills = Kills;
			BestTeam = Entry.TeamId;
			bStillTied = false;
		}
		else if (Entry.Score == BestScore && Kills == BestKills)
		{
			bStillTied = true;
		}
	}

	OutWinningTeamId = bStillTied ? KCF_NO_TEAM : BestTeam;

	// A tiebreak never *ends* the match early; it only improves the answer the base class gives when
	// the clock runs out.
	return false;
}

// -------------------------------------------------------------------------------------------------
// Elimination
// -------------------------------------------------------------------------------------------------

AKCFEliminationGameMode::AKCFEliminationGameMode()
{
}

int32 AKCFEliminationGameMode::CountRemainingSides(int32& OutLastRemainingTeamId, AKCFPlayerState*& OutLastRemainingPlayer) const
{
	OutLastRemainingTeamId = KCF_NO_TEAM;
	OutLastRemainingPlayer = nullptr;

	const AKCFGameState* KCFGameState = GetKCFGameState();
	const UKCFGameModeDataAsset* Config = GetGameModeConfig();
	if (!KCFGameState)
	{
		return 0;
	}

	if (Config && Config->IsTeamMode())
	{
		int32 TeamsAlive = 0;
		for (const FKCFTeamScore& Entry : KCFGameState->GetTeamScores())
		{
			if (Entry.AlivePlayerCount > 0)
			{
				++TeamsAlive;
				OutLastRemainingTeamId = Entry.TeamId;
			}
		}
		return TeamsAlive;
	}

	int32 PlayersAlive = 0;
	for (APlayerState* Entry : KCFGameState->PlayerArray)
	{
		AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (KCFPlayer && !KCFPlayer->IsOnlyASpectator() && !KCFPlayer->IsEliminated())
		{
			++PlayersAlive;
			OutLastRemainingPlayer = KCFPlayer;
		}
	}
	return PlayersAlive;
}

bool AKCFEliminationGameMode::CheckWinCondition(EKCFMatchEndReason& OutReason, int32& OutWinningTeamId, AKCFPlayerState*& OutWinningPlayer)
{
	int32 LastTeam = KCF_NO_TEAM;
	AKCFPlayerState* LastPlayer = nullptr;
	const int32 SidesRemaining = CountRemainingSides(LastTeam, LastPlayer);

	// Zero remaining is a genuine outcome, not an error: a final mutual explosion eliminates
	// everyone. Ending the match is the only sensible response; the result is a draw.
	if (SidesRemaining <= 1)
	{
		const AKCFGameState* KCFGameState = GetKCFGameState();
		const int32 Participants = KCFGameState ? KCFGameState->GetParticipantCount() : 0;

		// Guard against ending a match that has not really started: one connected player in a lobby
		// is "the last side standing" only in a trivial sense.
		if (Participants >= 2)
		{
			OutReason = EKCFMatchEndReason::LastStanding;
			OutWinningTeamId = LastTeam;
			OutWinningPlayer = LastPlayer;
			return true;
		}
	}

	return Super::CheckWinCondition(OutReason, OutWinningTeamId, OutWinningPlayer);
}

void AKCFEliminationGameMode::OnPlayerEliminated(AKCFPlayerState* PlayerState)
{
	Super::OnPlayerEliminated(PlayerState);

	if (PlayerState && !EliminationOrder.Contains(PlayerState))
	{
		EliminationOrder.Add(PlayerState);
		UE_LOG(LogKCFMatch, Log, TEXT("'%s' eliminated (%d out so far)."), *PlayerState->GetPlayerName(), EliminationOrder.Num());
	}
}

void AKCFEliminationGameMode::AssignFinalPlacements()
{
	AKCFGameState* KCFGameState = GetKCFGameState();
	if (!KCFGameState)
	{
		return;
	}

	// Survivors rank above everyone eliminated, ordered by score. Eliminated players rank in reverse
	// elimination order, so the player who lasted longest places highest among them.
	TArray<AKCFPlayerState*> Survivors;
	for (APlayerState* Entry : KCFGameState->PlayerArray)
	{
		AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (KCFPlayer && !KCFPlayer->IsOnlyASpectator() && !KCFPlayer->IsEliminated())
		{
			Survivors.Add(KCFPlayer);
		}
	}

	Survivors.Sort([](const AKCFPlayerState& A, const AKCFPlayerState& B)
	{
		return A.GetKCFScore() > B.GetKCFScore();
	});

	int32 Placement = 1;
	for (AKCFPlayerState* Survivor : Survivors)
	{
		Survivor->SetPlacement(Placement++);
	}

	for (int32 Index = EliminationOrder.Num() - 1; Index >= 0; --Index)
	{
		if (AKCFPlayerState* Eliminated = EliminationOrder[Index])
		{
			Eliminated->SetPlacement(Placement++);
		}
	}
}
