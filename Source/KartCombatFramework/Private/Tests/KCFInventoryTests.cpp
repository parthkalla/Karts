// Original work, MIT licensed.

#include "Tests/KCFTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/KCFInventoryComponent.h"
#include "Core/KCFTypes.h"
#include "Misc/AutomationTest.h"

namespace KCFInventoryTest
{
	static const FName Rocket(TEXT("Weapon.Rocket"));
	static const FName Mine(TEXT("Weapon.Mine"));
	static const FName Shield(TEXT("Powerup.Shield"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFInventoryStackingTest,
	"KartCombatFramework.Inventory.PickupsStackBeforeTakingANewSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFInventoryStackingTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Owner = TestWorld.SpawnTestActor();
	UKCFInventoryComponent* Inventory = TestWorld.AddComponent<UKCFInventoryComponent>(Owner);
	Inventory->SetSlotCount(3);

	const int32 FirstSlot = Inventory->AddWeapon(KCFInventoryTest::Rocket, 1, 3);
	TestEqual(TEXT("First rocket takes slot 0"), FirstSlot, 0);
	TestEqual(TEXT("Picking up the first item selects it"), Inventory->GetSelectedSlot(), 0);

	// Topping up the slot you already have beats consuming one of your two remaining slots.
	const int32 SecondSlot = Inventory->AddWeapon(KCFInventoryTest::Rocket, 1, 3);
	TestEqual(TEXT("Second rocket stacks into the same slot"), SecondSlot, 0);
	TestEqual(TEXT("Stack count is two"), Inventory->GetItem(0).Count, 2);
	TestEqual(TEXT("Only one slot is occupied"), Inventory->FindFirstEmptySlot(), 1);

	// A full stack has to spill into a new slot rather than silently vanishing.
	Inventory->AddWeapon(KCFInventoryTest::Rocket, 5, 3);
	TestEqual(TEXT("Stack clamps at the maximum"), Inventory->GetItem(0).Count, 3);

	const int32 OverflowSlot = Inventory->AddWeapon(KCFInventoryTest::Rocket, 1, 3);
	TestEqual(TEXT("Overflow takes the next free slot"), OverflowSlot, 1);

	// A different item never stacks onto an existing one.
	const int32 MineSlot = Inventory->AddWeapon(KCFInventoryTest::Mine, 1, 3);
	TestEqual(TEXT("A different weapon takes its own slot"), MineSlot, 2);
	TestTrue(TEXT("Inventory is now full"), Inventory->IsFull());

	TestEqual(TEXT("Adding to a full inventory fails"), Inventory->AddPowerup(KCFInventoryTest::Shield, 1, 1), KCF_NO_SLOT);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFInventoryConsumptionTest,
	"KartCombatFramework.Inventory.ConsumingASlotClearsAndReselects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFInventoryConsumptionTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Owner = TestWorld.SpawnTestActor();
	UKCFInventoryComponent* Inventory = TestWorld.AddComponent<UKCFInventoryComponent>(Owner);
	Inventory->SetSlotCount(2);

	Inventory->AddWeapon(KCFInventoryTest::Rocket, 2, 5);
	Inventory->AddPowerup(KCFInventoryTest::Shield, 1, 5);

	TestTrue(TEXT("Consuming one succeeds"), Inventory->ConsumeItem(0, 1));
	TestEqual(TEXT("One rocket left"), Inventory->GetItem(0).Count, 1);
	TestEqual(TEXT("Still selected"), Inventory->GetSelectedSlot(), 0);

	// Spending the last one has to empty the slot and move the selection to something usable,
	// otherwise the player is left holding a slot they cannot fire.
	Inventory->ConsumeItem(0, 1);
	TestTrue(TEXT("Slot is empty"), Inventory->GetItem(0).IsEmpty());
	TestEqual(TEXT("Selection moved to the remaining item"), Inventory->GetSelectedSlot(), 1);

	Inventory->ConsumeItem(1, 1);
	TestEqual(TEXT("Nothing left to select"), Inventory->GetSelectedSlot(), KCF_NO_SLOT);

	TestFalse(TEXT("Consuming an empty slot fails"), Inventory->ConsumeItem(0, 1));
	TestFalse(TEXT("Consuming an out-of-range slot fails"), Inventory->ConsumeItem(99, 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFInventorySelectionValidationTest,
	"KartCombatFramework.Inventory.ClientSelectionRequestsAreValidated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFInventorySelectionValidationTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Owner = TestWorld.SpawnTestActor();
	UKCFInventoryComponent* Inventory = TestWorld.AddComponent<UKCFInventoryComponent>(Owner);
	Inventory->SetSlotCount(3);
	Inventory->AddWeapon(KCFInventoryTest::Rocket, 1, 1);

	// This is the server-side handler a client RPC lands in. Everything it refuses is something a
	// modified client would otherwise be able to do.
	Inventory->ServerRequestSelectSlot_Implementation(7);
	TestEqual(TEXT("Out-of-range selection is refused"), Inventory->GetSelectedSlot(), 0);

	Inventory->ServerRequestSelectSlot_Implementation(2);
	TestEqual(TEXT("Selecting an empty slot is refused"), Inventory->GetSelectedSlot(), 0);

	Inventory->AddWeapon(KCFInventoryTest::Mine, 1, 1);
	Inventory->ServerRequestSelectSlot_Implementation(1);
	TestEqual(TEXT("Selecting an occupied slot is honoured"), Inventory->GetSelectedSlot(), 1);

	// Validation is a structural bound only; the occupancy check above is what does the real work.
	TestFalse(TEXT("Absurd indices fail validation outright"), Inventory->ServerRequestSelectSlot_Validate(9999));
	TestTrue(TEXT("Deselection passes validation"), Inventory->ServerRequestSelectSlot_Validate(KCF_NO_SLOT));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKCFInventoryClearOnDeathTest,
	"KartCombatFramework.Inventory.ClearAllEmptiesEverything",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FKCFInventoryClearOnDeathTest::RunTest(const FString& Parameters)
{
	KCFTest::FScopedTestWorld TestWorld;

	AActor* Owner = TestWorld.SpawnTestActor();
	UKCFInventoryComponent* Inventory = TestWorld.AddComponent<UKCFInventoryComponent>(Owner);
	Inventory->SetSlotCount(3);
	Inventory->AddWeapon(KCFInventoryTest::Rocket, 2, 5);
	Inventory->AddPowerup(KCFInventoryTest::Shield, 1, 5);

	Inventory->ClearAll();

	TestEqual(TEXT("Slot count is preserved"), Inventory->GetSlotCount(), 3);
	TestEqual(TEXT("First slot is free"), Inventory->FindFirstEmptySlot(), 0);
	TestFalse(TEXT("Rocket is gone"), Inventory->HasWeapon(KCFInventoryTest::Rocket));
	TestFalse(TEXT("Shield is gone"), Inventory->HasPowerup(KCFInventoryTest::Shield));
	TestEqual(TEXT("Nothing is selected"), Inventory->GetSelectedSlot(), KCF_NO_SLOT);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
