// Original work, MIT licensed.

#include "Tests/KCFTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/KCFHealthComponent.h"
#include "Core/KCFTypes.h"
#include "Misc/AutomationTest.h"

/**
 * Durability rules that the whole combat system rests on.
 *
 * These are the behaviours a balance change or a refactor is most likely to break silently: shield
 * ordering, the exact point at which a kart dies, and whether invulnerability can be cancelled by
 * the wrong source. None of them need rendering, networking or input, so they run headless.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthShieldOrderingTest,
	"KartCombatFramework.Damage.ShieldAbsorbsBeforeHealth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthShieldOrderingTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 50.f);

	TestEqual(TEXT("Starts at full health"), Health->GetHealth(), 100.f);
	TestEqual(TEXT("Starts at full shield"), Health->GetShield(), 50.f);

	// Less than the shield: health must be untouched. A kart with a shield up that still loses
	// health is the bug this test exists to catch.
	const FKCFDamageResult Partial = Health->ApplyDamage(FKCFDamageInfo(20.f, EKCFDamageCategory::Direct));

	TestTrue(TEXT("Partial damage was applied"), Partial.bWasApplied);
	TestEqual(TEXT("Shield took all of it"), Partial.ShieldDamage, 20.f);
	TestEqual(TEXT("Health took none of it"), Partial.HealthDamage, 0.f);
	TestEqual(TEXT("Health unchanged"), Health->GetHealth(), 100.f);
	TestEqual(TEXT("Shield reduced"), Health->GetShield(), 30.f);

	// More than the remaining shield: the overflow, and only the overflow, reaches health.
	const FKCFDamageResult Overflow = Health->ApplyDamage(FKCFDamageInfo(50.f, EKCFDamageCategory::Direct));

	TestEqual(TEXT("Shield absorbed what was left"), Overflow.ShieldDamage, 30.f);
	TestEqual(TEXT("Overflow reached health"), Overflow.HealthDamage, 20.f);
	TestEqual(TEXT("Shield emptied"), Health->GetShield(), 0.f);
	TestEqual(TEXT("Health took the remainder"), Health->GetHealth(), 80.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthShieldBypassTest,
	"KartCombatFramework.Damage.IgnoreShieldBypassesIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthShieldBypassTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 50.f);

	FKCFDamageInfo Info(30.f, EKCFDamageCategory::Direct);
	Info.bIgnoreShield = true;

	const FKCFDamageResult Result = Health->ApplyDamage(Info);

	TestEqual(TEXT("Shield untouched"), Health->GetShield(), 50.f);
	TestEqual(TEXT("Health took the full amount"), Result.HealthDamage, 30.f);
	TestEqual(TEXT("Health reduced"), Health->GetHealth(), 70.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthLethalDamageTest,
	"KartCombatFramework.Damage.LethalDamageKillsExactlyOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthLethalDamageTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 0.f);

	const FKCFDamageResult Fatal = Health->ApplyDamage(FKCFDamageInfo(250.f, EKCFDamageCategory::Explosion));

	TestTrue(TEXT("Reported as lethal"), Fatal.bWasLethal);
	TestTrue(TEXT("Target is dead"), Health->IsDead());
	TestEqual(TEXT("Health floors at zero rather than going negative"), Health->GetHealth(), 0.f);
	TestEqual(TEXT("Health damage is capped at the health that was there"), Fatal.HealthDamage, 100.f);

	// Shooting a wreck must not produce a second kill. Scoring reads bWasLethal, so a second
	// lethal result here would award the killer twice.
	const FKCFDamageResult AfterDeath = Health->ApplyDamage(FKCFDamageInfo(50.f, EKCFDamageCategory::Direct));

	TestFalse(TEXT("No damage applied to a dead target"), AfterDeath.bWasApplied);
	TestFalse(TEXT("Not lethal a second time"), AfterDeath.bWasLethal);
	TestEqual(TEXT("Rejected as already dead"), AfterDeath.RejectReason, EKCFDamageRejectReason::TargetAlreadyDead);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthInvulnerabilityStackTest,
	"KartCombatFramework.Damage.InvulnerabilityStacksIndependently",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthInvulnerabilityStackTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 0.f);

	// Two independent sources, e.g. spawn protection plus a shield powerup.
	Health->PushInvulnerability();
	Health->PushInvulnerability();

	TestTrue(TEXT("Invulnerable with two stacks"), Health->IsInvulnerable());

	const FKCFDamageResult Blocked = Health->ApplyDamage(FKCFDamageInfo(40.f, EKCFDamageCategory::Direct));
	TestFalse(TEXT("Damage blocked"), Blocked.bWasApplied);
	TestEqual(TEXT("Rejected as invulnerable"), Blocked.RejectReason, EKCFDamageRejectReason::TargetInvulnerable);
	TestEqual(TEXT("Health untouched"), Health->GetHealth(), 100.f);

	// One source expiring must not cancel the other. This is the reason the count is a stack and
	// not a boolean.
	Health->PopInvulnerability();
	TestTrue(TEXT("Still invulnerable after one source expires"), Health->IsInvulnerable());

	Health->PopInvulnerability();
	TestFalse(TEXT("Vulnerable once both sources are gone"), Health->IsInvulnerable());

	const FKCFDamageResult Landed = Health->ApplyDamage(FKCFDamageInfo(40.f, EKCFDamageCategory::Direct));
	TestTrue(TEXT("Damage lands once protection is gone"), Landed.bWasApplied);
	TestEqual(TEXT("Health reduced"), Health->GetHealth(), 60.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthKillVolumeTest,
	"KartCombatFramework.Damage.IgnoreInvulnerabilityReachesProtectedTarget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthKillVolumeTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 50.f);
	Health->PushInvulnerability();

	// Falling out of the arena has to kill a spawn-protected kart, otherwise a player can park
	// below the level and sit there untouchable for the whole match.
	FKCFDamageInfo OutOfBounds(1000.f, EKCFDamageCategory::Environmental);
	OutOfBounds.bIgnoreInvulnerability = true;
	OutOfBounds.bIgnoreShield = true;

	const FKCFDamageResult Result = Health->ApplyDamage(OutOfBounds);

	TestTrue(TEXT("Applied despite invulnerability"), Result.bWasApplied);
	TestTrue(TEXT("Lethal"), Result.bWasLethal);
	TestTrue(TEXT("Target is dead"), Health->IsDead());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthReviveTest,
	"KartCombatFramework.Damage.ReviveRestoresFullDurability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthReviveTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 40.f);

	Health->ApplyDamage(FKCFDamageInfo(500.f, EKCFDamageCategory::Direct));
	TestTrue(TEXT("Dead before revive"), Health->IsDead());

	Health->Revive();

	TestFalse(TEXT("Alive after revive"), Health->IsDead());
	TestEqual(TEXT("Health restored"), Health->GetHealth(), 100.f);
	TestEqual(TEXT("Shield restored"), Health->GetShield(), 40.f);

	// A respawned kart that is still flagged invulnerable from its previous life would be
	// untouchable; a respawned kart that can be damaged again is the expected state.
	const FKCFDamageResult Result = Health->ApplyDamage(FKCFDamageInfo(10.f, EKCFDamageCategory::Direct));
	TestTrue(TEXT("Damageable after revive"), Result.bWasApplied);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFHealthHealClampTest,
	"KartCombatFramework.Damage.HealingClampsToMaximum",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFHealthHealClampTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Target = TestWorld.SpawnTestActor();
	UKCFHealthComponent* Health = TestWorld.AddComponent<UKCFHealthComponent>(Target);
	Health->InitialiseHealth(100.f, 0.f);

	Health->ApplyDamage(FKCFDamageInfo(30.f, EKCFDamageCategory::Direct));

	TestEqual(TEXT("Heals only the missing amount"), Health->Heal(80.f), 30.f);
	TestEqual(TEXT("Health capped at maximum"), Health->GetHealth(), 100.f);
	TestEqual(TEXT("Healing at full health does nothing"), Health->Heal(25.f), 0.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
