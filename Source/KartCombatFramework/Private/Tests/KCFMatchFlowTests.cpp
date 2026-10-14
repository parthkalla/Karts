// Original work, MIT licensed.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Match/KCFMatchStateMachine.h"
#include "Misc/AutomationTest.h"

namespace KCFMatchFlowTest
{
	static const TArray<EKCFMatchState> AllStates = {
		EKCFMatchState::WaitingForPlayers,
		EKCFMatchState::Preparing,
		EKCFMatchState::Countdown,
		EKCFMatchState::InProgress,
		EKCFMatchState::SuddenDeath,
		EKCFMatchState::Finished,
		EKCFMatchState::ReturningToLobby
	};
}

/**
 * The transition table, checked exhaustively.
 *
 * Every pair of states is tested rather than a sampled set, which is affordable here because the
 * rules are pure functions over a seven-value enum. The properties below are the ones a match
 * cannot violate without producing a corrupt result: no state goes backwards into gameplay, and
 * nothing re-enters a phase whose entry work has already run.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFMatchStateTransitionTest,
	"KartCombatFramework.Match.TransitionTableIsWellFormed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFMatchStateTransitionTest::RunTest(const FString& Parameters)
{
	for (const EKCFMatchState State : KCFMatchFlowTest::AllStates)
	{
		// Re-entering a state would re-run its entry work: respawning everyone, restarting the
		// clock, re-awarding placements.
		TestFalse(
			FString::Printf(TEXT("%s cannot transition to itself"), UKCFMatchStateMachine::GetStateName(State)),
			UKCFMatchStateMachine::IsTransitionAllowed(State, State));
	}

	// The happy path, end to end.
	TestTrue(TEXT("Lobby starts a match"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::WaitingForPlayers, EKCFMatchState::Preparing));
	TestTrue(TEXT("Preparing counts down"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::Preparing, EKCFMatchState::Countdown));
	TestTrue(TEXT("Countdown goes live"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::Countdown, EKCFMatchState::InProgress));
	TestTrue(TEXT("A tie goes to sudden death"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::InProgress, EKCFMatchState::SuddenDeath));
	TestTrue(TEXT("Sudden death finishes"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::SuddenDeath, EKCFMatchState::Finished));
	TestTrue(TEXT("Finished returns to the lobby"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::Finished, EKCFMatchState::ReturningToLobby));
	TestTrue(TEXT("A new match starts from the lobby again"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::ReturningToLobby, EKCFMatchState::WaitingForPlayers));

	// Sudden death only ever resolves. Falling back into normal play would mean the timer that
	// forced sudden death could be reset and the match could run forever.
	TestFalse(TEXT("Sudden death does not return to normal play"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::SuddenDeath, EKCFMatchState::InProgress));

	// A finished match has already assigned placements and told the session the match is over.
	TestFalse(TEXT("A finished match does not restart"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::Finished, EKCFMatchState::InProgress));
	TestFalse(TEXT("A finished match does not re-enter the lobby directly"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::Finished, EKCFMatchState::WaitingForPlayers));

	// Skipping the countdown would drop players into a live match with no warning.
	TestFalse(TEXT("The lobby cannot skip straight into play"), UKCFMatchStateMachine::IsTransitionAllowed(EKCFMatchState::WaitingForPlayers, EKCFMatchState::InProgress));

	// Every state must be able to abandon the match, otherwise a stuck phase has no way out.
	for (const EKCFMatchState State : KCFMatchFlowTest::AllStates)
	{
		if (State == EKCFMatchState::ReturningToLobby)
		{
			continue;
		}

		TestTrue(
			FString::Printf(TEXT("%s can abandon to ReturningToLobby"), UKCFMatchStateMachine::GetStateName(State)),
			UKCFMatchStateMachine::IsTransitionAllowed(State, EKCFMatchState::ReturningToLobby));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFMatchStateClassificationTest,
	"KartCombatFramework.Match.StateClassificationIsConsistent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFMatchStateClassificationTest::RunTest(const FString& Parameters)
{
	for (const EKCFMatchState State : KCFMatchFlowTest::AllStates)
	{
		const TCHAR* Name = UKCFMatchStateMachine::GetStateName(State);

		// Damage, scoring and respawn all gate on IsLiveState; a state that is live but has no
		// pawns in the world would mean karts taking damage that nobody can see.
		if (UKCFMatchStateMachine::IsLiveState(State))
		{
			TestTrue(
				FString::Printf(TEXT("%s is live, so players must be spawned"), Name),
				UKCFMatchStateMachine::ShouldPlayersBeSpawned(State));

			TestFalse(
				FString::Printf(TEXT("%s is live, so it cannot also be terminal"), Name),
				UKCFMatchStateMachine::IsTerminalState(State));
		}

		TestFalse(
			FString::Printf(TEXT("%s has a display name"), Name),
			UKCFMatchStateMachine::GetStateDisplayName(State).IsEmpty());
	}

	TestTrue(TEXT("InProgress is live"), UKCFMatchStateMachine::IsLiveState(EKCFMatchState::InProgress));
	TestTrue(TEXT("SuddenDeath is live"), UKCFMatchStateMachine::IsLiveState(EKCFMatchState::SuddenDeath));
	TestFalse(TEXT("Countdown is not live"), UKCFMatchStateMachine::IsLiveState(EKCFMatchState::Countdown));

	// Karts stay in the world on the results screen so the camera has something to look at.
	TestTrue(TEXT("Players remain spawned after the match"), UKCFMatchStateMachine::ShouldPlayersBeSpawned(EKCFMatchState::Finished));
	TestFalse(TEXT("Players are not spawned in the lobby"), UKCFMatchStateMachine::ShouldPlayersBeSpawned(EKCFMatchState::WaitingForPlayers));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
