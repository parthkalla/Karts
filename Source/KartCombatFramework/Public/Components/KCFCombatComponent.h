// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "KCFCombatComponent.generated.h"

class AKCFKartPawn;
class UKCFInventoryComponent;
class UKCFPowerupComponent;
class UKCFPowerupDataAsset;
class UKCFWeaponComponent;

/**
 * The single front door for "the player pressed an action button".
 *
 * Input, whether from Enhanced Input or from a mobile widget, calls exactly one of these methods.
 * The component then works out whether the selected inventory slot holds a weapon or a powerup and
 * routes the request to the right component.
 *
 * It exists so that the input layer never has to know the difference. A fire button that had to
 * check "is this a weapon or a powerup" before deciding which component to call would duplicate
 * that rule in every UI that has a fire button.
 *
 * Holds no authoritative state of its own; the weapon and powerup components own their own rules
 * and validate independently, so this is a router, not a trust boundary.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFCombatComponent();

	// --- Actions ---------------------------------------------------------------------------------

	/** Fires the selected weapon. Safe to call on a client; it becomes a validated request. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Combat")
	void FireCurrentWeapon();

	/** Uses the selected powerup. Safe to call on a client. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Combat")
	void UseCurrentPowerup();

	/**
	 * Fires or uses whatever is in the selected slot.
	 * Bound to the single action button that a one-button mobile layout exposes.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Combat")
	void UseSelectedSlot();

	/** Asks the server to select the next occupied slot. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Combat")
	void CycleToNextSlot();

	// --- Queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Combat")
	bool CanFireWeapon() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Combat")
	bool CanUsePowerup() const;

	/** The powerup in the selected slot, or null when the slot holds a weapon or is empty. */
	UFUNCTION(BlueprintPure, Category = "KCF|Combat")
	UKCFPowerupDataAsset* GetCurrentPowerup() const;

	/** True when the selected slot holds a weapon rather than a powerup. */
	UFUNCTION(BlueprintPure, Category = "KCF|Combat")
	bool IsSelectedSlotAWeapon() const;

protected:
	/**
	 * Powerup activation is a server RPC on this component rather than on the powerup component,
	 * because the decision of *which* powerup is being used comes from the inventory selection that
	 * this component already reads. Like the fire RPC it carries no arguments: the server re-reads
	 * the selected slot from its own authoritative inventory.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUsePowerup();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCycleSlot();

	/** Runs the powerup activation. Server only. */
	void ExecutePowerupUse();

	AKCFKartPawn* GetKartPawn() const;
	UKCFInventoryComponent* GetInventory() const;
	UKCFWeaponComponent* GetWeaponComponent() const;
	UKCFPowerupComponent* GetPowerupComponent() const;
};
