// Original work, MIT licensed.

#include "Components/KCFBoostComponent.h"

#include "Components/KCFHealthComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/World.h"
#include "Interfaces/KCFVehicleMovementInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Vehicles/KCFKartPawn.h"

const FName UKCFBoostComponent::BoostSpeedSourceId(TEXT("KCF.Boost"));

UKCFBoostComponent::UKCFBoostComponent()
{
	// No tick: a boost is two timers, not a per-frame computation.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKCFBoostComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BoostEndTimerHandle);
		World->GetTimerManager().ClearTimer(ChargeRefillTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UKCFBoostComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Boosting is visible to everyone (other players see the effect and the speed), but the charge
	// count is a private HUD detail.
	DOREPLIFETIME(UKCFBoostComponent, bBoosting);
	DOREPLIFETIME_CONDITION(UKCFBoostComponent, Charges, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UKCFBoostComponent, MaxCharges, COND_OwnerOnly);
}

void UKCFBoostComponent::ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData)
{
	if (!InVehicleData)
	{
		return;
	}

	BoostMultiplier = InVehicleData->BoostMultiplier;
	BoostDuration = InVehicleData->BoostDuration;
	BoostCooldown = InVehicleData->BoostCooldown;

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		MaxCharges = FMath::Max(0, InVehicleData->BoostCharges);
		Charges = MaxCharges;
		OnRep_Charges();
	}
}

IKCFVehicleMovement* UKCFBoostComponent::GetMovement() const
{
	const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(GetOwner());
	if (!Kart)
	{
		return nullptr;
	}

	const TScriptInterface<IKCFVehicleMovement> Movement = Kart->GetVehicleMovement();
	return Movement.GetInterface();
}

bool UKCFBoostComponent::IsOwnerAbleToBoost() const
{
	const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(GetOwner());
	return Kart && Kart->CanAct();
}

bool UKCFBoostComponent::CanBoost() const
{
	return !bBoosting && Charges > 0 && IsOwnerAbleToBoost();
}

float UKCFBoostComponent::GetChargeProgress() const
{
	if (Charges >= MaxCharges)
	{
		return 1.f;
	}

	const UWorld* World = GetWorld();
	if (!World || BoostCooldown <= 0.f)
	{
		return 1.f;
	}

	const float Remaining = World->GetTimerManager().GetTimerRemaining(ChargeRefillTimerHandle);
	if (Remaining <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(1.f - (Remaining / BoostCooldown), 0.f, 1.f);
}

float UKCFBoostComponent::GetBoostTimeRemaining() const
{
	const UWorld* World = GetWorld();
	if (!World || !bBoosting)
	{
		return 0.f;
	}

	return FMath::Max(0.f, World->GetTimerManager().GetTimerRemaining(BoostEndTimerHandle));
}

// -------------------------------------------------------------------------------------------------
// Requests
// -------------------------------------------------------------------------------------------------

void UKCFBoostComponent::RequestBoost()
{
	ServerRequestBoost();
}

bool UKCFBoostComponent::ServerRequestBoost_Validate()
{
	// No parameters to validate structurally; the gameplay checks live in the implementation.
	return true;
}

void UKCFBoostComponent::ServerRequestBoost_Implementation()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// Full re-validation on the server. The client's opinion about whether it may boost is only a
	// prediction for its own UI; this is the decision that counts.
	if (!CanBoost())
	{
		return;
	}

	--Charges;
	OnRep_Charges();
	UpdateRefillTimer();

	GrantBoost(BoostDuration, BoostMultiplier);
}

void UKCFBoostComponent::GrantBoost(float DurationSeconds, float Multiplier)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();

	if (!Owner || !Owner->HasAuthority() || !World || DurationSeconds <= 0.f)
	{
		return;
	}

	if (IKCFVehicleMovement* Movement = GetMovement())
	{
		Movement->SetSpeedMultiplier(BoostSpeedSourceId, FMath::Max(1.f, Multiplier));
	}

	bBoosting = true;
	OnRep_Boosting();

	// A second grant while already boosting extends the window rather than stacking a second timer,
	// so a boost pad hit mid-boost refreshes rather than double-counting.
	FTimerDelegate EndBoost;
	EndBoost.BindWeakLambda(this, [this]()
	{
		StopBoost();
	});

	World->GetTimerManager().SetTimer(BoostEndTimerHandle, EndBoost, DurationSeconds, false);
}

void UKCFBoostComponent::StopBoost()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !bBoosting)
	{
		return;
	}

	if (IKCFVehicleMovement* Movement = GetMovement())
	{
		Movement->ClearSpeedMultiplier(BoostSpeedSourceId);
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BoostEndTimerHandle);
	}

	bBoosting = false;
	OnRep_Boosting();
}

void UKCFBoostComponent::RefillCharges()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	Charges = MaxCharges;
	OnRep_Charges();
	UpdateRefillTimer();
}

// -------------------------------------------------------------------------------------------------
// Charge refill
// -------------------------------------------------------------------------------------------------

void UKCFBoostComponent::UpdateRefillTimer()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World || !Owner || !Owner->HasAuthority())
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	const bool bNeedsRefill = Charges < MaxCharges && BoostCooldown > 0.f;

	if (bNeedsRefill && !Timers.IsTimerActive(ChargeRefillTimerHandle))
	{
		Timers.SetTimer(ChargeRefillTimerHandle, this, &UKCFBoostComponent::RefillOneCharge, BoostCooldown, false);
	}
	else if (!bNeedsRefill && Timers.IsTimerActive(ChargeRefillTimerHandle))
	{
		Timers.ClearTimer(ChargeRefillTimerHandle);
	}
}

void UKCFBoostComponent::RefillOneCharge()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	Charges = FMath::Min(Charges + 1, MaxCharges);
	OnRep_Charges();

	// Re-arm for the next charge if there are still some missing. A single-shot timer per charge
	// keeps the cooldown honest when several charges are spent in quick succession.
	UpdateRefillTimer();
}

// -------------------------------------------------------------------------------------------------
// OnRep handlers
// -------------------------------------------------------------------------------------------------

void UKCFBoostComponent::OnRep_Boosting()
{
	if (bBoosting)
	{
		OnBoostStarted.Broadcast();
	}
	else
	{
		OnBoostEnded.Broadcast();
	}
}

void UKCFBoostComponent::OnRep_Charges()
{
	OnChargesChanged.Broadcast(Charges, MaxCharges);
}
