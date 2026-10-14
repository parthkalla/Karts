// Original work, MIT licensed.

#include "Components/KCFInventoryComponent.h"

#include "Core/KCFLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UKCFInventoryComponent::UKCFInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// One slot by default: that is the classic arcade kart loadout, and the vehicle Data Asset
	// raises it for karts that carry more.
	Slots.SetNum(1);
}

void UKCFInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UKCFInventoryComponent, Slots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UKCFInventoryComponent, SelectedSlot, COND_OwnerOnly);
}

bool UKCFInventoryComponent::HasInventoryAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

void UKCFInventoryComponent::SetSlotCount(int32 NewSlotCount)
{
	if (!HasInventoryAuthority())
	{
		return;
	}

	const int32 Clamped = FMath::Clamp(NewSlotCount, 1, 8);
	if (Slots.Num() == Clamped)
	{
		return;
	}

	Slots.SetNum(Clamped);

	if (SelectedSlot >= Slots.Num())
	{
		SelectFirstOccupiedSlot();
	}

	OnRep_Slots();
}

// -------------------------------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------------------------------

FKCFInventorySlot UKCFInventoryComponent::GetItem(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex] : FKCFInventorySlot();
}

FKCFInventorySlot UKCFInventoryComponent::GetCurrentItem() const
{
	return GetItem(SelectedSlot);
}

bool UKCFInventoryComponent::HasWeapon(FName WeaponId) const
{
	if (WeaponId.IsNone())
	{
		return false;
	}

	for (const FKCFInventorySlot& Slot : Slots)
	{
		if (!Slot.IsEmpty() && Slot.WeaponId == WeaponId)
		{
			return true;
		}
	}
	return false;
}

bool UKCFInventoryComponent::HasPowerup(FName PowerupId) const
{
	if (PowerupId.IsNone())
	{
		return false;
	}

	for (const FKCFInventorySlot& Slot : Slots)
	{
		if (!Slot.IsEmpty() && Slot.PowerupId == PowerupId)
		{
			return true;
		}
	}
	return false;
}

bool UKCFInventoryComponent::IsFull() const
{
	for (const FKCFInventorySlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

int32 UKCFInventoryComponent::FindFirstEmptySlot() const
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].IsEmpty())
		{
			return Index;
		}
	}
	return KCF_NO_SLOT;
}

// -------------------------------------------------------------------------------------------------
// Mutation
// -------------------------------------------------------------------------------------------------

int32 UKCFInventoryComponent::AddWeapon(FName WeaponId, int32 Count, int32 MaxStack)
{
	return AddItemInternal(WeaponId, NAME_None, Count, MaxStack);
}

int32 UKCFInventoryComponent::AddPowerup(FName PowerupId, int32 Count, int32 MaxStack)
{
	return AddItemInternal(NAME_None, PowerupId, Count, MaxStack);
}

int32 UKCFInventoryComponent::AddItemInternal(FName WeaponId, FName PowerupId, int32 Count, int32 MaxStack)
{
	if (!HasInventoryAuthority() || Count <= 0 || (WeaponId.IsNone() && PowerupId.IsNone()))
	{
		return KCF_NO_SLOT;
	}

	const int32 EffectiveMaxStack = FMath::Max(1, MaxStack);

	FKCFInventorySlot Incoming;
	Incoming.WeaponId = WeaponId;
	Incoming.PowerupId = PowerupId;

	// Stack onto a matching slot first. Picking up a second rocket should top up the one you have
	// rather than occupying your only other slot.
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		FKCFInventorySlot& Slot = Slots[Index];
		if (!Slot.IsEmpty() && Slot.HoldsSameItemAs(Incoming) && Slot.Count < EffectiveMaxStack)
		{
			Slot.Count = FMath::Min(Slot.Count + Count, EffectiveMaxStack);
			OnRep_Slots();
			return Index;
		}
	}

	const int32 EmptySlot = FindFirstEmptySlot();
	if (EmptySlot == KCF_NO_SLOT)
	{
		return KCF_NO_SLOT;
	}

	Incoming.Count = FMath::Min(Count, EffectiveMaxStack);
	Slots[EmptySlot] = Incoming;

	// Auto-select the new item when nothing is selected, so a player who picks up their first weapon
	// can fire it without a separate selection step.
	if (SelectedSlot == KCF_NO_SLOT)
	{
		SetSelectedSlot(EmptySlot);
	}

	OnRep_Slots();
	return EmptySlot;
}

bool UKCFInventoryComponent::RemoveItem(int32 SlotIndex)
{
	if (!HasInventoryAuthority() || !Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	Slots[SlotIndex].Clear();

	if (SelectedSlot == SlotIndex)
	{
		SelectFirstOccupiedSlot();
	}

	OnRep_Slots();
	return true;
}

bool UKCFInventoryComponent::ConsumeItem(int32 SlotIndex, int32 Count)
{
	if (!HasInventoryAuthority() || !Slots.IsValidIndex(SlotIndex) || Count <= 0)
	{
		return false;
	}

	FKCFInventorySlot& Slot = Slots[SlotIndex];
	if (Slot.IsEmpty())
	{
		return false;
	}

	Slot.Count -= Count;

	if (Slot.Count <= 0)
	{
		Slot.Clear();
		if (SelectedSlot == SlotIndex)
		{
			SelectFirstOccupiedSlot();
		}
	}

	OnRep_Slots();
	return true;
}

bool UKCFInventoryComponent::ReplaceItem(int32 SlotIndex, const FKCFInventorySlot& NewContents)
{
	if (!HasInventoryAuthority() || !Slots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	Slots[SlotIndex] = NewContents;

	if (SelectedSlot == KCF_NO_SLOT && !NewContents.IsEmpty())
	{
		SetSelectedSlot(SlotIndex);
	}

	OnRep_Slots();
	return true;
}

void UKCFInventoryComponent::ClearAll()
{
	if (!HasInventoryAuthority())
	{
		return;
	}

	for (FKCFInventorySlot& Slot : Slots)
	{
		Slot.Clear();
	}

	SetSelectedSlot(KCF_NO_SLOT);
	OnRep_Slots();
}

void UKCFInventoryComponent::SetSelectedSlot(int32 NewSlot)
{
	if (!HasInventoryAuthority())
	{
		return;
	}

	const int32 Clamped = Slots.IsValidIndex(NewSlot) ? NewSlot : KCF_NO_SLOT;
	if (SelectedSlot == Clamped)
	{
		return;
	}

	SelectedSlot = Clamped;
	OnRep_SelectedSlot();
}

void UKCFInventoryComponent::SelectFirstOccupiedSlot()
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!Slots[Index].IsEmpty())
		{
			SelectedSlot = Index;
			OnRep_SelectedSlot();
			return;
		}
	}

	SelectedSlot = KCF_NO_SLOT;
	OnRep_SelectedSlot();
}

// -------------------------------------------------------------------------------------------------
// Client requests
// -------------------------------------------------------------------------------------------------

void UKCFInventoryComponent::RequestSelectSlot(int32 NewSlot)
{
	// No optimistic local change. Selection governs what the server lets you fire, so letting the
	// client lead would mean the two could disagree about what is equipped.
	ServerRequestSelectSlot(NewSlot);
}

bool UKCFInventoryComponent::ServerRequestSelectSlot_Validate(int32 NewSlot)
{
	// Structural bound only; whether the slot exists on this particular inventory is checked below.
	return NewSlot >= KCF_NO_SLOT && NewSlot < 16;
}

void UKCFInventoryComponent::ServerRequestSelectSlot_Implementation(int32 NewSlot)
{
	if (NewSlot != KCF_NO_SLOT && !Slots.IsValidIndex(NewSlot))
	{
		return;
	}

	// Selecting an empty slot is refused rather than clamped: it would otherwise be a way to
	// silently disarm, and a HUD showing an empty selected slot is confusing.
	if (NewSlot != KCF_NO_SLOT && Slots[NewSlot].IsEmpty())
	{
		return;
	}

	SetSelectedSlot(NewSlot);
}

// -------------------------------------------------------------------------------------------------
// OnRep handlers
// -------------------------------------------------------------------------------------------------

void UKCFInventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}

void UKCFInventoryComponent::OnRep_SelectedSlot()
{
	OnSelectedSlotChanged.Broadcast(SelectedSlot);
}
