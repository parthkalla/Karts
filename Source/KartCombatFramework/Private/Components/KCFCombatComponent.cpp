// Original work, MIT licensed.

#include "Components/KCFCombatComponent.h"

#include "Components/KCFInventoryComponent.h"
#include "Components/KCFPowerupComponent.h"
#include "Components/KCFWeaponComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFPowerupDataAsset.h"
#include "Subsystems/KCFContentRegistrySubsystem.h"
#include "Vehicles/KCFKartPawn.h"

UKCFCombatComponent::UKCFCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Replicated so its server RPCs are routable, even though it has no replicated properties.
	SetIsReplicatedByDefault(true);
}

AKCFKartPawn* UKCFCombatComponent::GetKartPawn() const
{
	return Cast<AKCFKartPawn>(GetOwner());
}

UKCFInventoryComponent* UKCFCombatComponent::GetInventory() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	return Kart ? Kart->GetInventoryComponent() : nullptr;
}

UKCFWeaponComponent* UKCFCombatComponent::GetWeaponComponent() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	return Kart ? Kart->GetWeaponComponent() : nullptr;
}

UKCFPowerupComponent* UKCFCombatComponent::GetPowerupComponent() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	return Kart ? Kart->GetPowerupComponent() : nullptr;
}

bool UKCFCombatComponent::IsSelectedSlotAWeapon() const
{
	const UKCFInventoryComponent* Inventory = GetInventory();
	if (!Inventory)
	{
		return false;
	}

	const FKCFInventorySlot Slot = Inventory->GetCurrentItem();
	return !Slot.IsEmpty() && !Slot.WeaponId.IsNone();
}

UKCFPowerupDataAsset* UKCFCombatComponent::GetCurrentPowerup() const
{
	const UKCFInventoryComponent* Inventory = GetInventory();
	if (!Inventory)
	{
		return nullptr;
	}

	const FKCFInventorySlot Slot = Inventory->GetCurrentItem();
	if (Slot.IsEmpty() || Slot.PowerupId.IsNone())
	{
		return nullptr;
	}

	UKCFContentRegistrySubsystem* Registry = UKCFContentRegistrySubsystem::Get(this);
	return Registry ? Registry->FindPowerup(Slot.PowerupId) : nullptr;
}

bool UKCFCombatComponent::CanFireWeapon() const
{
	const UKCFWeaponComponent* Weapons = GetWeaponComponent();
	return Weapons && Weapons->CanFireWeapon();
}

bool UKCFCombatComponent::CanUsePowerup() const
{
	const AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Kart->CanAct())
	{
		return false;
	}

	const UKCFPowerupDataAsset* Powerup = GetCurrentPowerup();
	if (!Powerup)
	{
		return false;
	}

	const UKCFPowerupComponent* Powerups = GetPowerupComponent();
	return Powerups && !Powerups->IsOnCooldown(Powerup->Id);
}

void UKCFCombatComponent::FireCurrentWeapon()
{
	if (UKCFWeaponComponent* Weapons = GetWeaponComponent())
	{
		Weapons->FireCurrentWeapon();
	}
}

void UKCFCombatComponent::UseCurrentPowerup()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		ExecutePowerupUse();
		return;
	}

	// The client checks first only to avoid sending obviously pointless RPCs. The answer that
	// matters is the server's, computed again in ExecutePowerupUse.
	if (CanUsePowerup())
	{
		ServerUsePowerup();
	}
}

void UKCFCombatComponent::UseSelectedSlot()
{
	if (IsSelectedSlotAWeapon())
	{
		FireCurrentWeapon();
	}
	else
	{
		UseCurrentPowerup();
	}
}

bool UKCFCombatComponent::ServerUsePowerup_Validate()
{
	return true;
}

void UKCFCombatComponent::ServerUsePowerup_Implementation()
{
	ExecutePowerupUse();
}

void UKCFCombatComponent::ExecutePowerupUse()
{
	UKCFInventoryComponent* Inventory = GetInventory();
	UKCFPowerupComponent* Powerups = GetPowerupComponent();
	if (!Inventory || !Powerups)
	{
		return;
	}

	// Re-read from the server's own inventory rather than trusting anything the client implied.
	UKCFPowerupDataAsset* Powerup = GetCurrentPowerup();
	if (!Powerup || !CanUsePowerup())
	{
		return;
	}

	const int32 Slot = Inventory->GetSelectedSlot();

	if (!Powerups->ActivatePowerup(Powerup))
	{
		return;
	}

	// Consumed only after the activation succeeds, so a refused powerup is not silently eaten.
	Inventory->ConsumeItem(Slot, 1);
}

void UKCFCombatComponent::CycleToNextSlot()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		ServerCycleSlot_Implementation();
	}
	else
	{
		ServerCycleSlot();
	}
}

bool UKCFCombatComponent::ServerCycleSlot_Validate()
{
	return true;
}

void UKCFCombatComponent::ServerCycleSlot_Implementation()
{
	UKCFInventoryComponent* Inventory = GetInventory();
	if (!Inventory)
	{
		return;
	}

	const int32 SlotCount = Inventory->GetSlotCount();
	if (SlotCount <= 0)
	{
		return;
	}

	const int32 Start = Inventory->GetSelectedSlot();
	for (int32 Step = 1; Step <= SlotCount; ++Step)
	{
		const int32 Candidate = (FMath::Max(Start, 0) + Step) % SlotCount;
		if (!Inventory->GetItem(Candidate).IsEmpty())
		{
			Inventory->SetSelectedSlot(Candidate);
			return;
		}
	}
}
