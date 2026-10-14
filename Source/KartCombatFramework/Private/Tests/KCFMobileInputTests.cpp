// Original work, MIT licensed.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/KCFTypes.h"
#include "Data/KCFMobileSettingsDataAsset.h"
#include "Misc/AutomationTest.h"

#include <limits>

/**
 * The touch steering curve.
 *
 * Worth testing on its own because it is the one piece of mobile input that is pure maths with no
 * device behind it, and because every property it has - a dead centre, a smooth start, a hard clamp
 * - is something a player feels immediately when it breaks.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFSteeringCurveTest,
	"KartCombatFramework.MobileInput.SteeringCurveIsWellBehaved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFSteeringCurveTest::RunTest(const FString& Parameters)
{
	UKCFMobileSettingsDataAsset* Settings = NewObject<UKCFMobileSettingsDataAsset>();
	Settings->SteeringDeadzone = 0.1f;
	Settings->SteeringSensitivity = 1.f;
	Settings->SteeringExponent = 1.f;

	TestEqual(TEXT("Centre is centre"), Settings->ProcessSteeringAxis(0.f), 0.f);
	TestEqual(TEXT("Inside the deadzone produces nothing"), Settings->ProcessSteeringAxis(0.05f), 0.f);
	TestEqual(TEXT("The deadzone is symmetric"), Settings->ProcessSteeringAxis(-0.05f), 0.f);

	// Full lock has to reach full lock. A curve that tops out at 0.98 means the kart can never
	// take its tightest corner, which reads as unresponsive steering rather than as a bug.
	TestEqual(TEXT("Full right is full lock"), Settings->ProcessSteeringAxis(1.f), 1.f);
	TestEqual(TEXT("Full left is full lock"), Settings->ProcessSteeringAxis(-1.f), -1.f);

	// Just past the deadzone must be a small value, not a step. This is the difference between a
	// thumbstick that eases in and one that snaps.
	const float JustPastDeadzone = Settings->ProcessSteeringAxis(0.11f);
	TestTrue(TEXT("Just past the deadzone steers a little"), JustPastDeadzone > 0.f);
	TestTrue(TEXT("Just past the deadzone does not snap to full"), JustPastDeadzone < 0.05f);

	// Monotonic: more thumb always means more steering.
	float Previous = 0.f;
	for (float Input = 0.11f; Input <= 1.f; Input += 0.05f)
	{
		const float Output = Settings->ProcessSteeringAxis(Input);
		TestTrue(
			FString::Printf(TEXT("Steering increases with input at %.2f"), Input),
			Output >= Previous);
		Previous = Output;
	}

	// Garbage in from a misbehaving device must not become garbage out into the simulation.
	TestEqual(TEXT("Beyond full lock clamps"), Settings->ProcessSteeringAxis(5.f), 1.f);
	TestEqual(TEXT("NaN is rejected"), Settings->ProcessSteeringAxis(std::numeric_limits<float>::quiet_NaN()), 0.f);

	// High sensitivity is still bounded: the simulation assumes -1..1 and a value outside it would
	// turn a sensitivity slider into a steering exploit.
	Settings->SteeringSensitivity = 3.f;
	TestEqual(TEXT("High sensitivity still clamps"), Settings->ProcessSteeringAxis(0.9f), 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFInputSanitiseTest,
	"KartCombatFramework.MobileInput.InputStateIsSanitisedBeforeUse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFInputSanitiseTest::RunTest(const FString& Parameters)
{
	// Sanitize is what stands between a modified client's replicated input struct and the
	// simulation, so the values it must reject are worth stating explicitly.
	FKCFKartInputState State;
	State.Steering = 50.f;
	State.Throttle = -3.f;
	State.Brake = 99.f;
	State.Look = FVector2D(std::numeric_limits<float>::quiet_NaN(), 1000.f);

	State.Sanitize();

	TestTrue(TEXT("Steering is within range"), State.Steering >= -1.f && State.Steering <= 1.f);
	TestTrue(TEXT("Throttle is within range"), State.Throttle >= 0.f && State.Throttle <= 1.f);
	TestTrue(TEXT("Brake is within range"), State.Brake >= 0.f && State.Brake <= 1.f);
	TestFalse(TEXT("Look contains no NaN"), State.Look.ContainsNaN());

	State.Reset();

	TestEqual(TEXT("Reset clears steering"), State.Steering, 0.f);
	TestEqual(TEXT("Reset clears throttle"), State.Throttle, 0.f);
	TestEqual(TEXT("Reset clears brake"), State.Brake, 0.f);
	TestFalse(TEXT("Reset clears drift"), State.bDrift);
	TestFalse(TEXT("Reset clears boost"), State.bBoost);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
