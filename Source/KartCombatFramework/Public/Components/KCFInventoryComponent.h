// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "KCFInventoryComponent.generated.h"

class UKCFPowerupDataAsset;
class UKCFWeaponDataAsset;

/**
 * One inventory slot.
 *
 * Holds identifiers rather than object pointers. An FName costs a couple of bytes on the wire and
 * does not force the referenced asset to be loaded and net-relevant on every client that can see
 * the kart; resolving an id to an asset is a local lookup on whichever machine actually needs it.
 */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFInventorySlot
{
	GENERATED_BODY()

	/** Weapon definition id, or NAME_None when this slot holds a powerup or is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Inventory")
	FName WeaponId;

	/** Powerup definition id, or NAME_None when this slot holds a weapon or is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Inventory")
	FName PowerupId;

	/** Remaining uses. Zero means the slot is spent and will be cleared. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Inventory")
	int32 Count = 0;

	bool IsEmpty() const { return Count <= 0 || (WeaponId.IsNone() && PowerupId.IsNone()); }

	void Clear()
	{
		WeaponId = NAME_None;
		PowerupId = NAME_None;
		Count = 0;
	}

	bool HoldsSameItemAs(const FKCFInventorySlot& Other) const
	{
		return WeaponId == Other.WeaponId && PowerupId == Other.PowerupId;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFInventoryChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFSlotSelectedSignature, int32, SlotIndex);

/**
 * Fixed-size slot inventory for a kart.
 *
 * Server-authoritative in both directions: the server is the only thing that adds or removes items,
 * and the contents replicate to the owning client only. Other players learn what you are carrying
 * by looking at your kart, not by reading your inventory off the wire, which also means an eight
 * player match replicates eight small arrays to one client each rather than to everyone.
 *
 * A plain replicated array is used rather than a fast array serialiser: the array is bounded at a
 * handful of slots, so delta bookkeeping would cost more than it saves.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFInventoryComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	UPROPERTY(BlueprintAssignable, Category = "KCF|Inventory")
	FKCFInventoryChangedSignature OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Inventory")
	FKCFSlotSelectedSignature OnSelectedSlotChanged;

	// --- Configuration --------------------------------------------------------------------------

	/** Resizes the inventory. Server only; existing contents beyond the new size are discarded. */
	void SetSlotCount(int32 NewSlotCount);

	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	int32 GetSlotCount() const { return Slots.Num(); }

	// --- Queries ----------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	const TArray<FKCFInventorySlot>& GetInventoryItems() const { return Slots; }

	/** Returns the slot at an index, or an empty slot when the index is out of range. */
	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	FKCFInventorySlot GetItem(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	FKCFInventorySlot GetCurrentItem() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	int32 GetSelectedSlot() const { return SelectedSlot; }

	/** True when any slot holds this weapon id. */
	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	bool HasWeapon(FName WeaponId) const;

	/** True when any slot holds this powerup id. */
	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	bool HasPowerup(FName PowerupId) const;

	/** True when every slot is occupied. */
	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	bool IsFull() const;

	/** Index of the first empty slot, or KCF_NO_SLOT when full. */
	UFUNCTION(BlueprintPure, Category = "KCF|Inventory")
	int32 FindFirstEmptySlot() const;

	// --- Server-only mutators -----------------------------------------------------------------------

	/**
	 * Adds a weapon. Stacks onto an existing slot holding the same weapon when there is room,
	 * otherwise fills the first empty slot. Returns the slot used, or KCF_NO_SLOT on failure.
	 */
	int32 AddWeapon(FName WeaponId, int32 Count, int32 MaxStack);

	/** Adds a powerup. Same stacking rules as AddWeapon. */
	int32 AddPowerup(FName PowerupId, int32 Count, int32 MaxStack);

	/** Empties one slot. Returns true when something was removed. */
	bool RemoveItem(int32 SlotIndex);

	/** Decrements the count in a slot, clearing it when it reaches zero. */
	bool ConsumeItem(int32 SlotIndex, int32 Count = 1);

	/** Replaces a slot's contents outright, used by weapon-swap powerups. */
	bool ReplaceItem(int32 SlotIndex, const FKCFInventorySlot& NewContents);

	/** Empties every slot. Called on death when the rule set says inventory is lost. */
	void ClearAll();

	/** Server-side slot selection, used by the game and by the validated client request. */
	void SetSelectedSlot(int32 NewSlot);

	// --- Client requests -----------------------------------------------------------------------------

	/** Asks the server to select a slot. The client does not change its own selection locally. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Inventory")
	void RequestSelectSlot(int32 NewSlot);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSelectSlot(int32 NewSlot);

protected:
	/**
	 * Owner-only replication. Nobody else needs to know what is in your inventory, and sending it to
	 * everyone would both waste bandwidth and hand opponents information they have not earned.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Slots, BlueprintReadOnly, Category = "KCF|Inventory")
	TArray<FKCFInventorySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedSlot, BlueprintReadOnly, Category = "KCF|Inventory")
	int32 SelectedSlot = KCF_NO_SLOT;

	UFUNCTION() void OnRep_Slots();
	UFUNCTION() void OnRep_SelectedSlot();

	/** True when this machine may mutate the inventory. */
	bool HasInventoryAuthority() const;

	/** Shared implementation behind AddWeapon and AddPowerup. */
	int32 AddItemInternal(FName WeaponId, FName PowerupId, int32 Count, int32 MaxStack);

	/** Moves the selection to the first occupied slot, or KCF_NO_SLOT when the inventory is empty. */
	void SelectFirstOccupiedSlot();
};
