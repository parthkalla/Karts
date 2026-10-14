// Original work, MIT licensed.

#include "Components/KCFPowerupComponent.h"

#include "Components/KCFHealthComponent.h"
#include "Components/KCFInventoryComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFPowerupDataAsset.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Powerups/KCFPowerupEffect.h"
#include "TimerManager.h"
#include "Vehicles/KCFKartPawn.h"

UKCFPowerupComponent::UKCFPowerupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKCFPowerupComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAllEffects();
	Super::EndPlay(EndPlayReason);
}

void UKCFPowerupComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UKCFPowerupComponent, ActivePowerups);
	DOREPLIFETIME_CONDITION(UKCFPowerupComponent, CollectedCount, COND_OwnerOnly);
}

bool UKCFPowerupComponent::HasPowerupAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

AKCFKartPawn* UKCFPowerupComponent::GetKartPawn() const
{
	return Cast<AKCFKartPawn>(GetOwner());
}

float UKCFPowerupComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
}

bool UKCFPowerupComponent::CollectPowerup(UKCFPowerupDataAsset* Powerup)
{
	if (!HasPowerupAuthority() || !Powerup)
	{
		return false;
	}

	AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Kart->CanAct())
	{
		return false;
	}

	if (IsOnCooldown(Powerup->Id))
	{
		return false;
	}

	// A weapon powerup is not an effect at all: it is a delivery mechanism for an inventory entry.
	if (Powerup->Type == EKCFPowerupType::Weapon)
	{
		UKCFInventoryComponent* Inventory = Kart->GetInventoryComponent();
		if (!Inventory)
		{
			return false;
		}

		UKCFWeaponDataAsset* Weapon = Powerup->GrantedWeapon.LoadSynchronous();
		if (!Weapon)
		{
			KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Powerup '%s' is a weapon grant but has no weapon asset set."), *Powerup->Id.ToString());
			return false;
		}

		const int32 Slot = Inventory->AddWeapon(Weapon->Id, Weapon->Ammo, Weapon->MaxAmmo);
		if (Slot == KCF_NO_SLOT)
		{
			// Inventory full. Refusing the pickup leaves it in the world for someone who can use it,
			// which is better than silently deleting a contested resource.
			return false;
		}

		++CollectedCount;
		OnPowerupCollected.Broadcast(Powerup);
		return true;
	}

	if (Powerup->bAutoActivateOnPickup || Powerup->Type == EKCFPowerupType::Instant)
	{
		++CollectedCount;
		OnPowerupCollected.Broadcast(Powerup);
		return ActivatePowerup(Powerup);
	}

	// Everything else occupies a slot until the player chooses to use it.
	UKCFInventoryComponent* Inventory = Kart->GetInventoryComponent();
	if (!Inventory)
	{
		return false;
	}

	const int32 Slot = Inventory->AddPowerup(Powerup->Id, 1, Powerup->bStackable ? Powerup->MaxStacks : 1);
	if (Slot == KCF_NO_SLOT)
	{
		return false;
	}

	++CollectedCount;
	OnPowerupCollected.Broadcast(Powerup);
	return true;
}

bool UKCFPowerupComponent::ActivatePowerup(UKCFPowerupDataAsset* Powerup)
{
	if (!HasPowerupAuthority() || !Powerup)
	{
		return false;
	}

	AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Kart->CanAct())
	{
		return false;
	}

	if (IsOnCooldown(Powerup->Id))
	{
		return false;
	}

	StartEffect(Powerup);

	if (Powerup->Cooldown > 0.f)
	{
		CooldownEndTimes.Add(Powerup->Id, GetServerTime() + Powerup->Cooldown);
	}

	OnPowerupUsed.Broadcast(Powerup);
	BP_OnPowerupActivated(Powerup);
	return true;
}

void UKCFPowerupComponent::StartEffect(UKCFPowerupDataAsset* Powerup)
{
	AKCFKartPawn* Kart = GetKartPawn();
	if (!Kart || !Powerup)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Re-collecting a non-stackable powerup refreshes it rather than running a second copy. Running
	// two instances of an effect that sets a value (rather than adding to one) would make the first
	// expiry revert a state the second instance still believes it owns.
	if (FKCFActiveEffect* Existing = ActiveEffects.Find(Powerup->Id))
	{
		if (!Powerup->bStackable)
		{
			if (Powerup->Duration > 0.f)
			{
				World->GetTimerManager().SetTimer(Existing->ExpiryTimer,
					FTimerDelegate::CreateUObject(this, &UKCFPowerupComponent::EndEffect, Powerup->Id),
					Powerup->Duration, false);
			}
			RefreshReplicatedSummary();
			return;
		}

		if (Existing->Stacks < Powerup->MaxStacks)
		{
			++Existing->Stacks;
			if (Existing->Effect)
			{
				Existing->Effect->ApplyEffect(Kart, Powerup->Magnitude);
			}
		}

		if (Powerup->Duration > 0.f)
		{
			World->GetTimerManager().SetTimer(Existing->ExpiryTimer,
				FTimerDelegate::CreateUObject(this, &UKCFPowerupComponent::EndEffect, Powerup->Id),
				Powerup->Duration, false);
		}
		RefreshReplicatedSummary();
		return;
	}

	UClass* EffectClass = Powerup->EffectClass.LoadSynchronous();
	if (!EffectClass)
	{
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("Powerup '%s' has no effect class; nothing to apply."), *Powerup->Id.ToString());
		return;
	}

	UKCFPowerupEffect* Effect = NewObject<UKCFPowerupEffect>(this, EffectClass);
	if (!Effect)
	{
		return;
	}

	Effect->OwningComponent = this;
	Effect->SourcePowerupId = Powerup->Id;
	Effect->ApplyEffect(Kart, Powerup->Magnitude);

	// An Instant powerup has already done its work in ApplyEffect. Keeping it alive would mean a
	// pointless revert later and a HUD entry for something that is not actually ongoing.
	if (Powerup->Type == EKCFPowerupType::Instant)
	{
		return;
	}

	FKCFActiveEffect Entry;
	Entry.Effect = Effect;
	Entry.Definition = Powerup;
	Entry.Stacks = 1;

	EffectObjects.Add(Effect);

	if (Powerup->Duration > 0.f && Powerup->Type != EKCFPowerupType::Passive)
	{
		World->GetTimerManager().SetTimer(Entry.ExpiryTimer,
			FTimerDelegate::CreateUObject(this, &UKCFPowerupComponent::EndEffect, Powerup->Id),
			Powerup->Duration, false);
	}

	if (Effect->PeriodSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(Entry.PeriodicTimer,
			FTimerDelegate::CreateUObject(this, &UKCFPowerupComponent::TickEffect, Powerup->Id),
			Effect->PeriodSeconds, true);
	}

	for (const FGameplayTag& Tag : Powerup->GrantedTags)
	{
		if (UKCFHealthComponent* Health = Kart->GetHealthComponent())
		{
			Health->AddActiveTag(Tag);
		}
	}

	ActiveEffects.Add(Powerup->Id, MoveTemp(Entry));
	RefreshReplicatedSummary();
}

void UKCFPowerupComponent::TickEffect(FName PowerupId)
{
	FKCFActiveEffect* Entry = ActiveEffects.Find(PowerupId);
	AKCFKartPawn* Kart = GetKartPawn();
	if (!Entry || !Entry->Effect || !Kart)
	{
		return;
	}

	Entry->Effect->TickEffect(Kart, Entry->Effect->PeriodSeconds);
}

void UKCFPowerupComponent::EndEffect(FName PowerupId)
{
	FKCFActiveEffect Entry;
	if (!ActiveEffects.RemoveAndCopyValue(PowerupId, Entry))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Entry.ExpiryTimer);
		World->GetTimerManager().ClearTimer(Entry.PeriodicTimer);
	}

	AKCFKartPawn* Kart = GetKartPawn();

	if (Entry.Effect)
	{
		// Reverted once per stack, matching the number of times ApplyEffect ran, so an additive
		// effect ends up exactly where it started.
		for (int32 Index = 0; Index < Entry.Stacks; ++Index)
		{
			Entry.Effect->RevertEffect(Kart);
		}
		EffectObjects.Remove(Entry.Effect);
	}

	if (Kart && Entry.Definition)
	{
		if (UKCFHealthComponent* Health = Kart->GetHealthComponent())
		{
			for (const FGameplayTag& Tag : Entry.Definition->GrantedTags)
			{
				Health->RemoveActiveTag(Tag);
			}
		}
	}

	RefreshReplicatedSummary();
	OnPowerupExpired.Broadcast(PowerupId);
}

void UKCFPowerupComponent::ClearAllEffects()
{
	if (!HasPowerupAuthority())
	{
		return;
	}

	TArray<FName> Ids;
	ActiveEffects.GetKeys(Ids);
	for (const FName& Id : Ids)
	{
		EndEffect(Id);
	}

	EffectObjects.Reset();
	ActiveEffects.Reset();
	RefreshReplicatedSummary();
}

void UKCFPowerupComponent::ResetForRespawn()
{
	if (!HasPowerupAuthority())
	{
		return;
	}

	ClearAllEffects();
	CooldownEndTimes.Reset();
	CollectedCount = 0;
}

void UKCFPowerupComponent::RefreshReplicatedSummary()
{
	if (!HasPowerupAuthority())
	{
		return;
	}

	const float Now = GetServerTime();
	const UWorld* World = GetWorld();

	ActivePowerups.Reset(ActiveEffects.Num());
	for (const TPair<FName, FKCFActiveEffect>& Pair : ActiveEffects)
	{
		FKCFActivePowerupInfo Info;
		Info.PowerupId = Pair.Key;
		Info.Stacks = Pair.Value.Stacks;

		const float Remaining = (World && Pair.Value.ExpiryTimer.IsValid())
			? World->GetTimerManager().GetTimerRemaining(Pair.Value.ExpiryTimer)
			: 0.f;
		Info.EndServerTime = Remaining > 0.f ? Now + Remaining : 0.f;

		ActivePowerups.Add(Info);
	}

	OnRep_ActivePowerups();
}

void UKCFPowerupComponent::OnRep_ActivePowerups()
{
	// Deliberately broadcasts nothing beyond the generic hook: the HUD reads GetActivePowerups.
	// Firing OnPowerupUsed here as well would double-fire it on a listen server.
}

bool UKCFPowerupComponent::HasActivePowerup(FName PowerupId) const
{
	return ActivePowerups.ContainsByPredicate(
		[PowerupId](const FKCFActivePowerupInfo& Info) { return Info.PowerupId == PowerupId; });
}

float UKCFPowerupComponent::GetRemainingTime(FName PowerupId) const
{
	const FKCFActivePowerupInfo* Info = ActivePowerups.FindByPredicate(
		[PowerupId](const FKCFActivePowerupInfo& Candidate) { return Candidate.PowerupId == PowerupId; });

	if (!Info || Info->EndServerTime <= 0.f)
	{
		return 0.f;
	}

	return FMath::Max(0.f, Info->EndServerTime - GetServerTime());
}

bool UKCFPowerupComponent::IsOnCooldown(FName PowerupId) const
{
	const float* EndTime = CooldownEndTimes.Find(PowerupId);
	return EndTime && *EndTime > GetServerTime();
}
