// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "KCFMatchStateMachine.generated.h"

/**
 * The legal transitions of EKCFMatchState, in one place.
 *
 * Kept as pure static functions with no world and no state so that the rules can be unit-tested
 * exhaustively (see KCFMatchFlowTests.cpp) and so that both the game mode and any project code that
 * wants to drive the machine answer to the same table. A transition that is not in the table is
 * rejected with a log rather than silently corrupting the match.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFMatchStateMachine : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when From -> To is a legal transition. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	static bool IsTransitionAllowed(EKCFMatchState From, EKCFMatchState To);

	/** True while gameplay input, damage and scoring are live. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	static bool IsLiveState(EKCFMatchState State);

	/** True when players should be spawned and visible, even if frozen. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	static bool ShouldPlayersBeSpawned(EKCFMatchState State);

	/** True once the outcome is decided and no further scoring can occur. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	static bool IsTerminalState(EKCFMatchState State);

	/** Localisable display name for a state, for debug overlays and spectator UI. */
	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	static FText GetStateDisplayName(EKCFMatchState State);

	/** Stable, non-localised name for logs. */
	static const TCHAR* GetStateName(EKCFMatchState State);
};
