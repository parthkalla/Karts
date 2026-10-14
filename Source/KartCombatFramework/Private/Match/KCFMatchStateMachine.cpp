// Original work, MIT licensed.

#include "Match/KCFMatchStateMachine.h"

#define LOCTEXT_NAMESPACE "KCFMatchStateMachine"

bool UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState From, EKCFMatchState To)
{
	if (From == To)
	{
		return false;
	}

	switch (From)
	{
	case EKCFMatchState::WaitingForPlayers:
		// A host can abandon a lobby that never fills.
		return To == EKCFMatchState::Preparing
			|| To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::Preparing:
		// Dropping back to WaitingForPlayers covers a player disconnecting during the spawn phase.
		return To == EKCFMatchState::Countdown
			|| To == EKCFMatchState::WaitingForPlayers
			|| To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::Countdown:
		return To == EKCFMatchState::InProgress
			|| To == EKCFMatchState::Finished
			|| To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::InProgress:
		return To == EKCFMatchState::SuddenDeath
			|| To == EKCFMatchState::Finished
			|| To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::SuddenDeath:
		// Deliberately cannot return to InProgress: sudden death only ever resolves.
		return To == EKCFMatchState::Finished
			|| To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::Finished:
		return To == EKCFMatchState::ReturningToLobby;

	case EKCFMatchState::ReturningToLobby:
		// Terminal for this match instance. A rematch is a new match, started from WaitingForPlayers
		// after travel, which keeps every per-match accumulator in a known-clean state.
		return To == EKCFMatchState::WaitingForPlayers;

	default:
		return false;
	}
}

bool UKCFMatchStateMachine::IsLiveState(EKCFMatchState State)
{
	return State == EKCFMatchState::InProgress || State == EKCFMatchState::SuddenDeath;
}

bool UKCFMatchStateMachine::ShouldPlayersBeSpawned(EKCFMatchState State)
{
	return State == EKCFMatchState::Preparing
		|| State == EKCFMatchState::Countdown
		|| State == EKCFMatchState::InProgress
		|| State == EKCFMatchState::SuddenDeath
		|| State == EKCFMatchState::Finished;
}

bool UKCFMatchStateMachine::IsTerminalState(EKCFMatchState State)
{
	return State == EKCFMatchState::Finished || State == EKCFMatchState::ReturningToLobby;
}

FText UKCFMatchStateMachine::GetStateDisplayName(EKCFMatchState State)
{
	switch (State)
	{
	case EKCFMatchState::WaitingForPlayers:	return LOCTEXT("WaitingForPlayers", "Waiting for players");
	case EKCFMatchState::Preparing:			return LOCTEXT("Preparing", "Get ready");
	case EKCFMatchState::Countdown:			return LOCTEXT("Countdown", "Starting");
	case EKCFMatchState::InProgress:		return LOCTEXT("InProgress", "In progress");
	case EKCFMatchState::SuddenDeath:		return LOCTEXT("SuddenDeath", "Sudden death");
	case EKCFMatchState::Finished:			return LOCTEXT("Finished", "Match over");
	case EKCFMatchState::ReturningToLobby:	return LOCTEXT("ReturningToLobby", "Returning to lobby");
	default:								return LOCTEXT("UnknownState", "Unknown");
	}
}

const TCHAR* UKCFMatchStateMachine::GetStateName(EKCFMatchState State)
{
	switch (State)
	{
	case EKCFMatchState::WaitingForPlayers:	return TEXT("WaitingForPlayers");
	case EKCFMatchState::Preparing:			return TEXT("Preparing");
	case EKCFMatchState::Countdown:			return TEXT("Countdown");
	case EKCFMatchState::InProgress:		return TEXT("InProgress");
	case EKCFMatchState::SuddenDeath:		return TEXT("SuddenDeath");
	case EKCFMatchState::Finished:			return TEXT("Finished");
	case EKCFMatchState::ReturningToLobby:	return TEXT("ReturningToLobby");
	default:								return TEXT("Unknown");
	}
}

#undef LOCTEXT_NAMESPACE
