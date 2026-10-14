// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KCFBoostComponent.generated.h"

class IKCFVehicleMovement;
class UKCFVehicleDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFBoostStartedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFBoostEndedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFBoostChargesChangedSignature, int32, Charges, int32, MaxCharges);

/**
 * Boost charges, duration and cooldown.
 *
 * Server-authoritative. The client asks; the server decides. A client that holds the boost button
 * down forever still only gets a boost when it has a charge and is off cooldown, because the only
 * thing its input does is send a request.
 *
 * Does not tick. The active boost ends on a one-shot timer and charges refill on a repeating timer
 * that stops itself once the kart is at full charges.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFBoostComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFBoostComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	UPROPERTY(BlueprintAssignable, Category = "KCF|Boost") FKCFBoostStartedSignature OnBoostStarted;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Boost") FKCFBoostEndedSignature OnBoostEnded;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Boost") FKCFBoostChargesChangedSignature OnChargesChanged;

	/** Applies boost tuning from a vehicle definition. Server only. */
	void ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData);

	UFUNCTION(BlueprintPure, Category = "KCF|Boost") bool IsBoosting() const { return bBoosting; }
	UFUNCTION(BlueprintPure, Category = "KCF|Boost") int32 GetCharges() const { return Charges; }
	UFUNCTION(BlueprintPure, Category = "KCF|Boost") int32 GetMaxCharges() const { return MaxCharges; }

	/** True when a boost could be started right now. */
	UFUNCTION(BlueprintPure, Category = "KCF|Boost")
	bool CanBoost() const;

	/** 0..1 progress towards the next charge. 1 when already at maximum. */
	UFUNCTION(BlueprintPure, Category = "KCF|Boost")
	float GetChargeProgress() const;

	/** Seconds remaining on the active boost. 0 when not boosting. */
	UFUNCTION(BlueprintPure, Category = "KCF|Boost")
	float GetBoostTimeRemaining() const;

	/** Client-facing entry point. Sends a request; changes nothing locally. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Boost")
	void RequestBoost();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestBoost();

	/**
	 * Starts a boost on the server without consuming a charge.
	 * This is the entry point for a speed powerup or a boost pad, which grant a boost rather than
	 * spending the player's own.
	 */
	void GrantBoost(float DurationSeconds, float Multiplier);

	/** Ends any active boost immediately. Server only. */
	void StopBoost();

	/** Refills every charge, used on respawn. Server only. */
	void RefillCharges();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Boosting, BlueprintReadOnly, Category = "KCF|Boost")
	bool bBoosting = false;

	/** Only the owner's HUD needs the charge count; other players just see the boost effect. */
	UPROPERTY(ReplicatedUsing = OnRep_Charges, BlueprintReadOnly, Category = "KCF|Boost")
	int32 Charges = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Boost")
	int32 MaxCharges = 1;

	float BoostMultiplier = 1.6f;
	float BoostDuration = 1.8f;
	float BoostCooldown = 6.f;

	FTimerHandle BoostEndTimerHandle;
	FTimerHandle ChargeRefillTimerHandle;

	/** Identifier used when registering the speed multiplier, so it can be removed precisely. */
	static const FName BoostSpeedSourceId;

	UFUNCTION() void OnRep_Boosting();
	UFUNCTION() void OnRep_Charges();

	/** Starts or stops the refill timer so it only runs while charges are missing. */
	void UpdateRefillTimer();

	/** Adds one charge. Runs on the refill timer. */
	void RefillOneCharge();

	/** Resolves the owner's movement backend through the interface. May be null. */
	IKCFVehicleMovement* GetMovement() const;

	/** True when the owning kart is in a state that permits boosting at all. */
	bool IsOwnerAbleToBoost() const;
};
