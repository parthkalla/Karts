// Original work, MIT licensed.

#include "Match/KCFGameState.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Engine/World.h"
#include "Match/KCFMatchStateMachine.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"

#define LOCTEXT_NAMESPACE "KCFGameState"

AKCFGameState::AKCFGameState()
{
	PrimaryActorTick.bCanEverTick = false;

	// Match timers are derived from an absolute end timestamp, so this actor only needs to replicate
	// when something structural changes. Ten times a second is generous.
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(2.f);
}

void AKCFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKCFGameState, MatchState);
	DOREPLIFETIME(AKCFGameState, StateEndServerTime);
	DOREPLIFETIME(AKCFGameState, GameModeConfig);
	DOREPLIFETIME(AKCFGameState, TeamScores);
	DOREPLIFETIME(AKCFGameState, KillFeed);
	DOREPLIFETIME(AKCFGameState, WinningTeamId);
	DOREPLIFETIME(AKCFGameState, WinningPlayer);
	DOREPLIFETIME(AKCFGameState, MatchEndReason);
}

// -------------------------------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------------------------------

bool AKCFGameState::IsMatchActive() const
{
	return UKCFMatchStateMachine::IsLiveState(MatchState);
}

float AKCFGameState::GetRemainingStateTime() const
{
	if (StateEndServerTime < 0.f)
	{
		return 0.f;
	}

	// GetServerWorldTimeSeconds() is already synchronised by the engine, so every client derives the
	// same countdown from one replicated timestamp instead of receiving a ticking float.
	return FMath::Max(0.f, StateEndServerTime - GetServerWorldTimeSeconds());
}

float AKCFGameState::GetRemainingMatchTime() const
{
	return UKCFMatchStateMachine::IsLiveState(MatchState) ? GetRemainingStateTime() : 0.f;
}

FText AKCFGameState::GetFormattedRemainingTime() const
{
	const int32 TotalSeconds = FMath::CeilToInt(GetRemainingStateTime());
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;

	FNumberFormattingOptions TwoDigits;
	TwoDigits.MinimumIntegralDigits = 2;
	TwoDigits.UseGrouping = false;

	return FText::Format(
		LOCTEXT("TimeFormat", "{0}:{1}"),
		FText::AsNumber(Minutes),
		FText::AsNumber(Seconds, &TwoDigits));
}

int32 AKCFGameState::GetTeamScore(int32 TeamId) const
{
	for (const FKCFTeamScore& Entry : TeamScores)
	{
		if (Entry.TeamId == TeamId)
		{
			return Entry.Score;
		}
	}
	return 0;
}

TArray<AKCFPlayerState*> AKCFGameState::GetSortedPlayerStates() const
{
	TArray<AKCFPlayerState*> Result;
	Result.Reserve(PlayerArray.Num());

	for (APlayerState* Entry : PlayerArray)
	{
		if (AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry))
		{
			if (!KCFPlayer->IsOnlyASpectator())
			{
				Result.Add(KCFPlayer);
			}
		}
	}

	Result.Sort([](const AKCFPlayerState& A, const AKCFPlayerState& B)
	{
		if (A.GetKCFScore() != B.GetKCFScore())
		{
			return A.GetKCFScore() > B.GetKCFScore();
		}
		if (A.GetKills() != B.GetKills())
		{
			return A.GetKills() > B.GetKills();
		}
		// Fewer deaths breaks a kill tie; a stable final tiebreak on name keeps the scoreboard from
		// flickering between two genuinely equal players.
		if (A.GetDeaths() != B.GetDeaths())
		{
			return A.GetDeaths() < B.GetDeaths();
		}
		return A.GetPlayerName() < B.GetPlayerName();
	});

	return Result;
}

int32 AKCFGameState::GetParticipantCount() const
{
	int32 Count = 0;
	for (const APlayerState* Entry : PlayerArray)
	{
		if (Entry && !Entry->IsOnlyASpectator())
		{
			++Count;
		}
	}
	return Count;
}

int32 AKCFGameState::GetAlivePlayerCount() const
{
	int32 Count = 0;
	for (const APlayerState* Entry : PlayerArray)
	{
		const AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (KCFPlayer && !KCFPlayer->IsOnlyASpectator() && !KCFPlayer->IsEliminated())
		{
			++Count;
		}
	}
	return Count;
}

// -------------------------------------------------------------------------------------------------
// Server-only mutators
// -------------------------------------------------------------------------------------------------

void AKCFGameState::SetMatchState(EKCFMatchState NewState, float StateDurationSeconds)
{
	if (!HasAuthority())
	{
		KCF_REPORT_RECOVERABLE(LogKCFMatch, TEXT("SetMatchState ignored on a non-authoritative game state."));
		return;
	}

	if (!UKCFMatchStateMachine::IsTransitionAllowed(MatchState, NewState))
	{
		// Rejecting loudly rather than applying the transition: an illegal transition means a caller
		// has a logic bug, and silently honouring it would hide that bug behind a corrupted match.
		UE_LOG(LogKCFMatch, Error, TEXT("Rejected illegal match state transition %s -> %s."),
			UKCFMatchStateMachine::GetStateName(MatchState),
			UKCFMatchStateMachine::GetStateName(NewState));
		return;
	}

	const EKCFMatchState OldState = MatchState;
	PreviousMatchState = OldState;
	MatchState = NewState;

	StateEndServerTime = (StateDurationSeconds > 0.f)
		? GetServerWorldTimeSeconds() + StateDurationSeconds
		: -1.f;

	UE_LOG(LogKCFMatch, Log, TEXT("Match state %s -> %s (duration %.1fs)."),
		UKCFMatchStateMachine::GetStateName(OldState),
		UKCFMatchStateMachine::GetStateName(NewState),
		StateDurationSeconds);

	// The server runs the same notification path as a client's OnRep so that listen-server-hosted
	// HUD code and client HUD code are identical.
	BroadcastStateEntered(OldState, NewState);

	ForceNetUpdate();
}

void AKCFGameState::SetGameModeConfig(UKCFGameModeDataAsset* InConfig)
{
	if (!HasAuthority())
	{
		return;
	}

	GameModeConfig = InConfig;
	ForceNetUpdate();
}

void AKCFGameState::InitialiseTeams(int32 TeamCount)
{
	if (!HasAuthority())
	{
		return;
	}

	TeamScores.Reset();

	if (TeamCount >= 2)
	{
		TeamScores.Reserve(TeamCount);
		for (int32 Index = 0; Index < TeamCount; ++Index)
		{
			FKCFTeamScore Entry;
			Entry.TeamId = Index;
			TeamScores.Add(Entry);
		}
	}

	OnRep_TeamScores();
	ForceNetUpdate();
}

void AKCFGameState::AddTeamScore(int32 TeamId, int32 Delta)
{
	if (!HasAuthority() || Delta == 0)
	{
		return;
	}

	for (FKCFTeamScore& Entry : TeamScores)
	{
		if (Entry.TeamId == TeamId)
		{
			Entry.Score += Delta;
			OnTeamScoreChanged.Broadcast(TeamId);
			ForceNetUpdate();
			return;
		}
	}

	KCF_REPORT_RECOVERABLE(LogKCFMatch, TEXT("AddTeamScore for unknown team %d; the match may not have been initialised as a team mode."), TeamId);
}

void AKCFGameState::RefreshTeamPopulations()
{
	if (!HasAuthority() || TeamScores.Num() == 0)
	{
		return;
	}

	for (FKCFTeamScore& Entry : TeamScores)
	{
		Entry.PlayerCount = 0;
		Entry.AlivePlayerCount = 0;
	}

	for (const APlayerState* Entry : PlayerArray)
	{
		const AKCFPlayerState* KCFPlayer = Cast<AKCFPlayerState>(Entry);
		if (!KCFPlayer || KCFPlayer->IsOnlyASpectator())
		{
			continue;
		}

		const int32 PlayerTeam = KCFPlayer->GetTeamId();
		for (FKCFTeamScore& TeamEntry : TeamScores)
		{
			if (TeamEntry.TeamId == PlayerTeam)
			{
				++TeamEntry.PlayerCount;
				if (!KCFPlayer->IsEliminated())
				{
					++TeamEntry.AlivePlayerCount;
				}
				break;
			}
		}
	}

	ForceNetUpdate();
}

void AKCFGameState::AddKillFeedEntry(AKCFPlayerState* Killer, AKCFPlayerState* Victim, FName WeaponId, bool bTeamKill)
{
	if (!HasAuthority() || !Victim)
	{
		return;
	}

	FKCFKillFeedEntry Entry;
	Entry.EntryId = NextKillFeedId++;
	Entry.Killer = Killer;
	Entry.Victim = Victim;
	Entry.WeaponId = WeaponId;
	Entry.bWasTeamKill = bTeamKill;
	Entry.bWasSuicide = (Killer == Victim) || (Killer == nullptr);
	Entry.ServerTime = GetServerWorldTimeSeconds();

	KillFeed.Add(Entry);

	// Trim from the front so the array stays bounded. Replicating a growing array for a long match
	// would eventually cost more than the rest of the game state put together.
	while (KillFeed.Num() > MaxKillFeedEntries)
	{
		KillFeed.RemoveAt(0, 1, EAllowShrinking::No);
	}

	OnRep_KillFeed();
	ForceNetUpdate();
}

void AKCFGameState::SetMatchResult(EKCFMatchEndReason Reason, int32 InWinningTeamId, AKCFPlayerState* InWinningPlayer)
{
	if (!HasAuthority())
	{
		return;
	}

	MatchEndReason = Reason;
	WinningTeamId = InWinningTeamId;
	WinningPlayer = InWinningPlayer;

	ForceNetUpdate();
}

// -------------------------------------------------------------------------------------------------
// OnRep handlers
// -------------------------------------------------------------------------------------------------

void AKCFGameState::OnRep_MatchState()
{
	const EKCFMatchState OldState = PreviousMatchState;
	PreviousMatchState = MatchState;
	BroadcastStateEntered(OldState, MatchState);
}

void AKCFGameState::OnRep_TeamScores()
{
	for (const FKCFTeamScore& Entry : TeamScores)
	{
		OnTeamScoreChanged.Broadcast(Entry.TeamId);
	}
}

void AKCFGameState::OnRep_KillFeed()
{
	// Only report entries this machine has not already reported. Without the id comparison a client
	// would replay the whole feed every time any single entry changed.
	for (const FKCFKillFeedEntry& Entry : KillFeed)
	{
		if (Entry.EntryId > LastBroadcastKillFeedId)
		{
			LastBroadcastKillFeedId = Entry.EntryId;
			OnKillFeedEntryAdded.Broadcast(Entry);
		}
	}
}

void AKCFGameState::BroadcastStateEntered(EKCFMatchState OldState, EKCFMatchState NewState)
{
	OnMatchStateChanged.Broadcast(OldState, NewState);

	switch (NewState)
	{
	case EKCFMatchState::Countdown:
		OnCountdownStarted.Broadcast(GetRemainingStateTime());
		break;

	case EKCFMatchState::InProgress:
		OnMatchStarted.Broadcast();
		break;

	case EKCFMatchState::SuddenDeath:
		OnSuddenDeathStarted.Broadcast();
		break;

	case EKCFMatchState::Finished:
		OnMatchFinished.Broadcast(MatchEndReason, WinningTeamId);
		break;

	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
