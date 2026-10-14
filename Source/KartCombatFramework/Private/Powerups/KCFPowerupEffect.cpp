// Original work, MIT licensed.

#include "Powerups/KCFPowerupEffect.h"

#include "Components/KCFBoostComponent.h"
#include "Components/KCFHealthComponent.h"
#include "Components/KCFPowerupComponent.h"
#include "Interfaces/KCFVehicleMovementInterface.h"
#include "Vehicles/KCFKartPawn.h"

UWorld* UKCFPowerupEffect::GetWorld() const
{
	// Effects are plain UObjects owned by a component. Routing GetWorld through the outer is what
	// lets a Blueprint effect subclass use latent nodes and timers at all.
	if (const UObject* Outer = GetOuter())
	{
		return Outer->GetWorld();
	}
	return nullptr;
}

void UKCFPowerupEffect::ApplyEffect_Implementation(AKCFKartPawn* /*Target*/, float /*Magnitude*/)
{
}

void UKCFPowerupEffect::RevertEffect_Implementation(AKCFKartPawn* /*Target*/)
{
}

// --- Speed boost -------------------------------------------------------------------------------

void UKCFSpeedBoostEffect::ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude)
{
	if (!Target)
	{
		return;
	}

	const TScriptInterface<IKCFVehicleMovement> Movement = Target->GetVehicleMovement();
	if (IKCFVehicleMovement* Impl = Movement ? static_cast<IKCFVehicleMovement*>(Movement.GetInterface()) : nullptr)
	{
		PreviousMultiplier = FMath::Max(Magnitude, 0.1f);

		// Keyed on the source powerup id rather than a shared constant, so two different speed
		// powerups stack instead of silently overwriting one another.
		Impl->SetSpeedMultiplier(SourcePowerupId, PreviousMultiplier);
	}
}

void UKCFSpeedBoostEffect::RevertEffect_Implementation(AKCFKartPawn* Target)
{
	if (!Target)
	{
		return;
	}

	const TScriptInterface<IKCFVehicleMovement> Movement = Target->GetVehicleMovement();
	if (IKCFVehicleMovement* Impl = Movement ? static_cast<IKCFVehicleMovement*>(Movement.GetInterface()) : nullptr)
	{
		Impl->ClearSpeedMultiplier(SourcePowerupId);
	}
}

// --- Shield -------------------------------------------------------------------------------------

void UKCFShieldEffect::ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude)
{
	UKCFHealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr;
	if (!Health)
	{
		return;
	}

	GrantedShield = FMath::Max(Magnitude, 0.f);
	Health->AddTemporaryShield(GrantedShield);
}

void UKCFShieldEffect::RevertEffect_Implementation(AKCFKartPawn* Target)
{
	UKCFHealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr;
	if (!Health || !bRemoveShieldOnExpiry)
	{
		return;
	}

	// Negative amount lowers the ceiling again; the component clamps current shield to the new one.
	Health->AddTemporaryShield(-GrantedShield);
	GrantedShield = 0.f;
}

// --- Repair --------------------------------------------------------------------------------------

void UKCFRepairEffect::ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude)
{
	if (UKCFHealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr)
	{
		Health->Heal(FMath::Max(Magnitude, 0.f));
	}
}

// --- Invulnerability -------------------------------------------------------------------------------

void UKCFInvulnerabilityEffect::ApplyEffect_Implementation(AKCFKartPawn* Target, float /*Magnitude*/)
{
	if (UKCFHealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr)
	{
		Health->PushInvulnerability();
		bAppliedImmunity = true;
	}
}

void UKCFInvulnerabilityEffect::RevertEffect_Implementation(AKCFKartPawn* Target)
{
	// Guarded so a double revert cannot pop a stack this effect never pushed, which would cancel
	// somebody else's spawn protection.
	if (!bAppliedImmunity)
	{
		return;
	}

	if (UKCFHealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr)
	{
		Health->PopInvulnerability();
	}
	bAppliedImmunity = false;
}

// --- Nitro ------------------------------------------------------------------------------------------

UKCFNitroEffect::UKCFNitroEffect()
{
	// One refill per second for the duration, rather than a frame tick: boost charges are a coarse
	// resource and nothing about them needs sub-second resolution.
	PeriodSeconds = 1.f;
}

void UKCFNitroEffect::ApplyEffect_Implementation(AKCFKartPawn* Target, float /*Magnitude*/)
{
	if (UKCFBoostComponent* Boost = Target ? Target->GetBoostComponent() : nullptr)
	{
		Boost->RefillCharges();
	}
}

void UKCFNitroEffect::TickEffect_Implementation(AKCFKartPawn* Target, float /*DeltaSeconds*/)
{
	if (UKCFBoostComponent* Boost = Target ? Target->GetBoostComponent() : nullptr)
	{
		Boost->RefillCharges();
	}
}
