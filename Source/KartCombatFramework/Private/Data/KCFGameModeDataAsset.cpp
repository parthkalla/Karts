// Original work, MIT licensed.

#include "Data/KCFGameModeDataAsset.h"

#define LOCTEXT_NAMESPACE "KCFGameModeDataAsset"

namespace
{
	/** Distinct, high-contrast fallback colours so an unauthored team is still readable in-game. */
	const TArray<FLinearColor>& GetFallbackTeamColors()
	{
		static const TArray<FLinearColor> Colors = {
			FLinearColor(0.10f, 0.45f, 1.00f),	// blue
			FLinearColor(1.00f, 0.20f, 0.15f),	// red
			FLinearColor(0.20f, 0.85f, 0.30f),	// green
			FLinearColor(1.00f, 0.80f, 0.10f),	// amber
			FLinearColor(0.70f, 0.25f, 0.95f),	// violet
			FLinearColor(0.10f, 0.85f, 0.85f),	// cyan
			FLinearColor(1.00f, 0.45f, 0.75f),	// pink
			FLinearColor(0.55f, 0.55f, 0.55f)	// grey
		};
		return Colors;
	}
}

FKCFTeamDefinition UKCFGameModeDataAsset::GetTeamDefinition(int32 TeamId) const
{
	for (const FKCFTeamDefinition& Definition : Teams)
	{
		if (Definition.TeamId == TeamId)
		{
			return Definition;
		}
	}

	// Unauthored team: generate something usable rather than returning a blank white entry that
	// makes every team look identical on the HUD.
	FKCFTeamDefinition Generated;
	Generated.TeamId = TeamId;
	Generated.DisplayName = FText::Format(LOCTEXT("DefaultTeamName", "Team {0}"), FText::AsNumber(TeamId + 1));

	const TArray<FLinearColor>& Colors = GetFallbackTeamColors();
	Generated.TeamColor = (TeamId >= 0) ? Colors[TeamId % Colors.Num()] : FLinearColor::White;

	return Generated;
}

bool UKCFGameModeDataAsset::ValidateConfiguration(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();
	Super::ValidateConfiguration(OutErrors);

	if (MinPlayers > MaxPlayers)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("MinAboveMax", "'{0}': MinPlayers ({1}) exceeds MaxPlayers ({2}); the match can never start."),
			FText::FromString(GetName()),
			FText::AsNumber(MinPlayers),
			FText::AsNumber(MaxPlayers)));
	}

	if (MatchDurationSeconds <= 0.f && ScoreLimit <= 0 && LivesPerPlayer <= 0)
	{
		OutErrors.Add(FText::Format(
			LOCTEXT("NoWinCondition", "'{0}': no time limit, no score limit and no life limit. This match has no way to end."),
			FText::FromString(GetName())));
	}

	if (IsTeamMode())
	{
		for (const FKCFTeamDefinition& Definition : Teams)
		{
			if (Definition.TeamId < 0 || Definition.TeamId >= TeamCount)
			{
				OutErrors.Add(FText::Format(
					LOCTEXT("TeamOutOfRange", "Team definition has TeamId {0}, which is outside the configured TeamCount of {1}."),
					FText::AsNumber(Definition.TeamId),
					FText::AsNumber(TeamCount)));
			}
		}

		if (MaxPlayers < TeamCount)
		{
			OutErrors.Add(LOCTEXT("FewerPlayersThanTeams",
				"MaxPlayers is lower than TeamCount, so at least one team can never have a player."));
		}
	}
	else if (Teams.Num() > 0)
	{
		OutErrors.Add(LOCTEXT("TeamsWithoutTeamMode",
			"Team definitions are authored but TeamCount is below 2, so this is a free-for-all and they will be ignored."));
	}

	if (bFriendlyFire && !IsTeamMode())
	{
		OutErrors.Add(LOCTEXT("FriendlyFireFFA",
			"bFriendlyFire has no effect in a free-for-all: every other player is already hostile."));
	}

	if (!bRespawnEnabled && LivesPerPlayer > 0)
	{
		OutErrors.Add(LOCTEXT("LivesWithoutRespawn",
			"LivesPerPlayer is set but respawning is disabled, so every player effectively has exactly one life."));
	}

	if (LivesPerPlayer > 0 && bRespawnEnabled && MatchDurationSeconds <= 0.f && ScoreLimit <= 0)
	{
		// Reachable end state, but worth flagging: the match relies entirely on eliminations.
		OutErrors.Add(LOCTEXT("EliminationOnly",
			"This match can only end by elimination. Confirm the game mode class implements a last-standing win condition."));
	}

	return OutErrors.Num() == ErrorsBefore;
}

#undef LOCTEXT_NAMESPACE
