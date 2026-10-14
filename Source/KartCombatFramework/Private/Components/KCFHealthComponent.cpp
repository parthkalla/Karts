// Original work, MIT licensed.

#include "Components/KCFHealthComponent.h"

#include "Core/KCFGameplayTags.h"
#include "Core/KCFLog.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	/** How often the shield regeneration timer fires while active. */
	constexpr float KCFShieldRegenInterval = 0.2f;
}

UKCFHealthComponent::UKCFHealthComponent()
{
	// No tick. Regeneration and invulnerability both use timers that only exist while needed.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKCFHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	LastKnownHealth = Health;
	LastKnownShield = Shield;
}

void UKCFHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldRegenTimerHandle);
		World->GetTimerManager().ClearTimer(InvulnerabilityTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UKCFHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UKCFHealthComponent, Health);
	DOREPLIFETIME(UKCFHealthComponent, MaxHealth);
	DOREPLIFETIME(UKCFHealthComponent, Shield);
	DOREPLIFETIME(UKCFHealthComponent, MaxShield);
	DOREPLIFETIME(UKCFHealthComponent, bDead);
	DOREPLIFETIME(UKCFHealthComponent, InvulnerabilityStacks);
	DOREPLIFETIME(UKCFHealthComponent, ActiveTags);
}

// -------------------------------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------------------------------

float UKCFHealthComponent::GetHealthPercent() const
{
	return (MaxHealth > 0.f) ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
}

float UKCFHealthComponent::GetShieldPercent() const
{
	return (MaxShield > 0.f) ? FMath::Clamp(Shield / MaxShield, 0.f, 1.f) : 0.f;
}

float UKCFHealthComponent::GetCombinedDurabilityPercent() const
{
	const float Total = MaxHealth + MaxShield;
	return (Total > 0.f) ? FMath::Clamp((Health + Shield) / Total, 0.f, 1.f) : 0.f;
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

void UKCFHealthComponent::ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData)
{
	if (!InVehicleData)
	{
		return;
	}

	ShieldRegenPerSecond = InVehicleData->ShieldRegenPerSecond;
	ShieldRegenDelay = InVehicleData->ShieldRegenDelay;

	InitialiseHealth(InVehicleData->MaxHealth, InVehicleData->MaxShield, /*bFillToMax=*/true);
}

void UKCFHealthComponent::InitialiseHealth(float InMaxHealth, float InMaxShield, bool bFillToMax)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	MaxHealth = FMath::Max(1.f, InMaxHealth);
	BaseMaxShield = FMath::Max(0.f, InMaxShield);
	MaxShield = BaseMaxShield;

	if (bFillToMax)
	{
		Health = MaxHealth;
		Shield = MaxShield;
	}
	else
	{
		Health = FMath::Min(Health, MaxHealth);
		Shield = FMath::Min(Shield, MaxShield);
	}

	bDead = false;

	OnRep_Health();
	OnRep_Shield();
}

// -------------------------------------------------------------------------------------------------
// Damage
// -------------------------------------------------------------------------------------------------

FKCFDamageResult UKCFHealthComponent::ApplyDamage(const FKCFDamageInfo& DamageInfo)
{
	AActor* Owner = GetOwner();

	if (!Owner || !Owner->HasAuthority())
	{
		// Reaching here on a client means something bypassed the damage subsystem. Report it rather
		// than silently absorbing it: a silent no-op here would look like a damage bug.
		KCF_REPORT_RECOVERABLE(LogKCFCombat, TEXT("ApplyDamage called without authority; ignoring."));
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::NoAuthority);
	}

	if (bDead)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetAlreadyDead);
	}

	if (!DamageInfo.IsStructurallyValid())
	{
		UE_LOG(LogKCFCombat, Warning, TEXT("Rejected structurally invalid damage against '%s'."), *Owner->GetName());
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::ZeroDamage);
	}

	if (DamageInfo.DamageAmount <= 0.f)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::ZeroDamage);
	}

	if (!DamageInfo.bIgnoreInvulnerability && IsInvulnerable())
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetInvulnerable);
	}

	FKCFDamageResult Result;
	Result.bWasApplied = true;

	float Remaining = DamageInfo.DamageAmount;

	// Shield absorbs first unless the weapon explicitly bypasses it. The order matters: absorbing
	// after health would make a shield useless against a lethal hit.
	if (!DamageInfo.bIgnoreShield && Shield > 0.f)
	{
		const float Absorbed = FMath::Min(Shield, Remaining);
		Shield -= Absorbed;
		Remaining -= Absorbed;
		Result.ShieldDamage = Absorbed;
	}

	if (Remaining > 0.f)
	{
		const float Applied = FMath::Min(Health, Remaining);
		Health -= Applied;
		Result.HealthDamage = Applied;
	}

	Result.bWasLethal = Health <= 0.f;

	OnRep_Health();
	OnRep_Shield();

	OnDamageTaken.Broadcast(DamageInfo, Result);

	// Any damage interrupts regeneration and restarts the delay, so sustained pressure keeps a
	// shield down rather than letting it tick back up between hits.
	if (Result.ShieldDamage > 0.f || Result.HealthDamage > 0.f)
	{
		ScheduleShieldRegen();
	}

	if (Result.bWasLethal)
	{
		HandleDeath(DamageInfo);
	}

	return Result;
}

float UKCFHealthComponent::Heal(float Amount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bDead || Amount <= 0.f || !FMath::IsFinite(Amount))
	{
		return 0.f;
	}

	const float Restored = FMath::Min(Amount, MaxHealth - Health);
	if (Restored <= 0.f)
	{
		return 0.f;
	}

	Health += Restored;
	OnRep_Health();

	return Restored;
}

float UKCFHealthComponent::RestoreShield(float Amount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bDead || Amount <= 0.f || !FMath::IsFinite(Amount))
	{
		return 0.f;
	}

	const float Restored = FMath::Min(Amount, MaxShield - Shield);
	if (Restored <= 0.f)
	{
		return 0.f;
	}

	Shield += Restored;
	OnRep_Shield();

	return Restored;
}

float UKCFHealthComponent::AddTemporaryShield(float Amount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.f || !FMath::IsFinite(Amount))
	{
		return MaxShield;
	}

	// Raising the ceiling and filling to it, so a shield powerup is immediately useful rather than
	// giving the player an empty, larger bar.
	MaxShield += Amount;
	Shield += Amount;

	OnRep_Shield();
	return MaxShield;
}

// -------------------------------------------------------------------------------------------------
// Invulnerability
// -------------------------------------------------------------------------------------------------

void UKCFHealthComponent::PushInvulnerability()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	++InvulnerabilityStacks;
	OnRep_Invulnerability();
}

void UKCFHealthComponent::PopInvulnerability()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || InvulnerabilityStacks <= 0)
	{
		return;
	}

	--InvulnerabilityStacks;
	OnRep_Invulnerability();
}

void UKCFHealthComponent::GrantTimedInvulnerability(float DurationSeconds)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !Owner->HasAuthority() || !World || DurationSeconds <= 0.f)
	{
		return;
	}

	PushInvulnerability();

	// A weak lambda so a kart destroyed mid-protection does not leave a timer pointing at it.
	FTimerDelegate Expire;
	Expire.BindWeakLambda(this, [this]()
	{
		PopInvulnerability();
	});

	// One handle for all timed grants: a second grant extends the window rather than scheduling a
	// second pop, which would unbalance the stack count.
	World->GetTimerManager().SetTimer(InvulnerabilityTimerHandle, Expire, DurationSeconds, false);
}

void UKCFHealthComponent::ClearAllInvulnerability()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || InvulnerabilityStacks == 0)
	{
		return;
	}

	InvulnerabilityStacks = 0;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InvulnerabilityTimerHandle);
	}

	OnRep_Invulnerability();
}

void UKCFHealthComponent::AddActiveTag(const FGameplayTag& Tag)
{
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && Tag.IsValid())
	{
		ActiveTags.AddTag(Tag);
	}
}

void UKCFHealthComponent::RemoveActiveTag(const FGameplayTag& Tag)
{
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && Tag.IsValid())
	{
		ActiveTags.RemoveTag(Tag);
	}
}

// -------------------------------------------------------------------------------------------------
// Life cycle
// -------------------------------------------------------------------------------------------------

void UKCFHealthComponent::Revive()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// Temporary shield bonuses do not survive death; the base maximum is restored.
	MaxShield = BaseMaxShield;
	Health = MaxHealth;
	Shield = MaxShield;
	bDead = false;
	ActiveTags.Reset();

	OnRep_Health();
	OnRep_Shield();
	OnRep_Dead();

	OnRevive.Broadcast();
}

void UKCFHealthComponent::KillImmediately(AActor* Killer, EKCFDamageCategory Category)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bDead)
	{
		return;
	}

	FKCFDamageInfo Fatal(Health + Shield, Category);
	Fatal.DamageCauser = Killer;
	Fatal.bIgnoreShield = true;
	Fatal.bIgnoreInvulnerability = true;
	Fatal.HitLocation = Owner->GetActorLocation();

	Health = 0.f;
	Shield = 0.f;

	OnRep_Health();
	OnRep_Shield();

	HandleDeath(Fatal);
}

void UKCFHealthComponent::HandleDeath(const FKCFDamageInfo& FatalDamage)
{
	if (bDead)
	{
		return;
	}

	bDead = true;
	Health = 0.f;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldRegenTimerHandle);
	}

	ClearAllInvulnerability();

	// Broadcast directly rather than through OnRep_Dead: the server has the full damage information
	// and OnRep_Dead would fire a second, emptier notification on this machine.
	OnDeath.Broadcast(GetOwner(), FatalDamage.DamageCauser, FatalDamage);
}

// -------------------------------------------------------------------------------------------------
// Shield regeneration
// -------------------------------------------------------------------------------------------------

void UKCFHealthComponent::ScheduleShieldRegen()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();

	if (!World || !Owner || !Owner->HasAuthority() || ShieldRegenPerSecond <= 0.f || MaxShield <= 0.f)
	{
		return;
	}

	// Re-arming the same handle restarts the delay, which is exactly the behaviour wanted when a
	// second hit lands during the wait.
	World->GetTimerManager().SetTimer(
		ShieldRegenTimerHandle,
		this,
		&UKCFHealthComponent::TickShieldRegen,
		KCFShieldRegenInterval,
		/*bLoop=*/true,
		/*FirstDelay=*/ShieldRegenDelay);
}

void UKCFHealthComponent::TickShieldRegen()
{
	if (bDead || Shield >= MaxShield)
	{
		// Stop the timer the moment it has nothing left to do, rather than letting it run for the
		// rest of the match doing nothing.
		if (const UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ShieldRegenTimerHandle);
		}
		return;
	}

	RestoreShield(ShieldRegenPerSecond * KCFShieldRegenInterval);
}

// -------------------------------------------------------------------------------------------------
// OnRep handlers
// -------------------------------------------------------------------------------------------------

void UKCFHealthComponent::OnRep_Health()
{
	const float Delta = Health - LastKnownHealth;
	LastKnownHealth = Health;
	OnHealthChanged.Broadcast(Health, MaxHealth, Delta);
}

void UKCFHealthComponent::OnRep_Shield()
{
	const float Delta = Shield - LastKnownShield;
	LastKnownShield = Shield;
	OnShieldChanged.Broadcast(Shield, MaxShield, Delta);
}

void UKCFHealthComponent::OnRep_Dead()
{
	// Clients learn about death through this replicated flag rather than a multicast, so a player
	// who becomes relevant after the fact still sees the correct state. The damage information is
	// not replicated with it: cosmetic death effects need the fact, not the arithmetic, and sending
	// a full FKCFDamageInfo to every client for every death would be pure waste.
	if (bDead)
	{
		const FKCFDamageInfo UnknownCause;
		OnDeath.Broadcast(GetOwner(), nullptr, UnknownCause);
	}
}

void UKCFHealthComponent::OnRep_Invulnerability()
{
	OnInvulnerabilityChanged.Broadcast(IsInvulnerable());
}
