// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "KCFWeaponComponent.generated.h"

class AKCFKartPawn;
class AKCFPlayerState;
class UKCFInventoryComponent;
class UKCFWeaponDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFWeaponChangedSignature, UKCFWeaponDataAsset*, NewWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFWeaponFiredSignature, UKCFWeaponDataAsset*, Weapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFAmmoChangedSignature, int32, Ammo, int32, MaxAmmo);

/**
 * Turns "the player pressed fire" into an authoritative shot.
 *
 * The division of labour is the whole point of this class. A client may do exactly one thing: ask.
 * It cannot say what it is firing, where from, in which direction, or for how much damage. Every one
 * of those is derived on the server from state the server already owns - the selected inventory
 * slot, the kart's own muzzle transform, and the weapon definition that slot resolves to.
 *
 * The request RPC therefore carries no parameters at all. That is not an oversight: a fire RPC with
 * a direction argument is a fire RPC that can be aimed through a wall by a modified client.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFWeaponComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	UPROPERTY(BlueprintAssignable, Category = "KCF|Weapon")
	FKCFWeaponChangedSignature OnWeaponChanged;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Weapon")
	FKCFWeaponFiredSignature OnWeaponFired;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Weapon")
	FKCFAmmoChangedSignature OnAmmoChanged;

	// --- Queries ---------------------------------------------------------------------------------

	/** The weapon in the currently selected inventory slot, or null when none is held. */
	UFUNCTION(BlueprintPure, Category = "KCF|Weapon")
	UKCFWeaponDataAsset* GetCurrentWeapon() const { return CurrentWeapon; }

	/** Rounds left in the selected slot. */
	UFUNCTION(BlueprintPure, Category = "KCF|Weapon")
	int32 GetCurrentAmmo() const;

	/**
	 * Whether firing would succeed right now.
	 *
	 * Used by the client to grey out a fire button and by the server as the first half of its own
	 * validation. Shared code, not duplicated rules - a predicted "yes" that the server then refuses
	 * is a bug, and running the identical function on both sides is what keeps them honest.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Weapon")
	bool CanFireWeapon() const;

	/** Seconds until the weapon can fire again. 0 when it is ready. */
	UFUNCTION(BlueprintPure, Category = "KCF|Weapon")
	float GetRemainingCooldown() const;

	/** 0..1 cooldown progress, convenient for a radial UI sweep. */
	UFUNCTION(BlueprintPure, Category = "KCF|Weapon")
	float GetCooldownPercent() const;

	// --- Actions -----------------------------------------------------------------------------------

	/**
	 * Asks the server to fire the currently selected weapon.
	 *
	 * Callable from Blueprint so a mobile fire button can drive it directly. On the server this is
	 * the shot; on a client it is a request plus a local cooldown so the button feels responsive.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Weapon")
	void FireCurrentWeapon();

	/** Recomputes the equipped weapon from the inventory. Called when the selection changes. */
	void RefreshCurrentWeapon();

	/** Clears cooldowns and the equipped weapon. Called on death and on respawn. */
	void ResetWeaponState();

	// --- Cosmetic hooks -----------------------------------------------------------------------------

	/** Fired on every machine that can see the kart, so muzzle effects play for spectators too. */
	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Weapon Fired"))
	void BP_OnWeaponFired(UKCFWeaponDataAsset* Weapon, FVector MuzzleLocation);

protected:
	/**
	 * The resolved weapon definition for the selected slot.
	 *
	 * Not replicated. Every machine derives it the same way: the owning client from its own
	 * replicated inventory, the server from the authoritative one, and a simulated proxy not at all
	 * (it has no business knowing). Replicating it would duplicate information already on the wire.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UKCFWeaponDataAsset> CurrentWeapon;

	/**
	 * Server time at which firing becomes legal again.
	 *
	 * Replicated to the owner only so the client's cooldown UI reflects the authoritative value
	 * rather than its own prediction, which can drift apart after a rejected shot.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_NextFireTime)
	float NextFireTime = 0.f;

	UFUNCTION() void OnRep_NextFireTime();

	/** No parameters by design. See the class comment. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerFireWeapon();

	/** Cosmetic only. Unreliable: a dropped muzzle flash is not worth a retransmit. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWeaponFired(UKCFWeaponDataAsset* Weapon, FVector MuzzleLocation);

	/** Tells a client its request was refused, so it can drop the predicted cooldown. */
	UFUNCTION(Client, Reliable)
	void ClientFireRejected();

	// --- Server-side execution -------------------------------------------------------------------------

	/** Runs the shot. Server only; assumes CanFireWeapon has already passed. */
	void ExecuteFire(UKCFWeaponDataAsset* Weapon);

	void FireProjectiles(UKCFWeaponDataAsset* Weapon, const FTransform& Muzzle);
	void FireHitscan(UKCFWeaponDataAsset* Weapon, const FTransform& Muzzle);
	void PlaceDeployable(UKCFWeaponDataAsset* Weapon);
	void ApplySelfEffect(UKCFWeaponDataAsset* Weapon);
	void FireAreaEffect(UKCFWeaponDataAsset* Weapon);

	/** Spends a round and drops the weapon when the definition says an empty slot should clear. */
	void ConsumeAmmoAfterFiring(const UKCFWeaponDataAsset* Weapon);

	// --- Helpers --------------------------------------------------------------------------------------

	AKCFKartPawn* GetKartPawn() const;
	UKCFInventoryComponent* GetInventory() const;
	AKCFPlayerState* GetOwningPlayerState() const;
	float GetServerTime() const;

	/** Number of live projectiles this component has spawned, used to enforce the per-player cap. */
	int32 CountLiveProjectiles() const;

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleSelectedSlotChanged(int32 NewSlot);

	/** Projectiles spawned by this kart that have not yet been destroyed. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> LiveProjectiles;
};
