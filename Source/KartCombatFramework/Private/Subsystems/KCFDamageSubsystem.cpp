// Original work, MIT licensed.

#include "Subsystems/KCFDamageSubsystem.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFGameplayTags.h"
#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Interfaces/KCFDamageableInterface.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "Interfaces/KCFVehicleMovementInterface.h"
#include "Match/KCFGameState.h"
#include "Match/KCFMatchStateMachine.h"
#include "Player/KCFPlayerState.h"
#include "Vehicles/KCFKartPawn.h"

void UKCFDamageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AssistLedger.Reset();
}

void UKCFDamageSubsystem::Deinitialize()
{
	AssistLedger.Reset();
	Super::Deinitialize();
}

bool UKCFDamageSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Editor preview and inactive worlds have no damage to resolve. Game and PIE both need it,
	// because PIE hosts the listen server.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UKCFDamageSubsystem* UKCFDamageSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UKCFDamageSubsystem>() : nullptr;
}

float UKCFDamageSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}

	const AKCFGameState* GameState = World->GetGameState<AKCFGameState>();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

// -------------------------------------------------------------------------------------------------
// The pipeline
// -------------------------------------------------------------------------------------------------

FKCFDamageResult UKCFDamageSubsystem::ApplyDamage(AActor* Target, const FKCFDamageInfo& DamageInfo)
{
	UWorld* World = GetWorld();

	// Step 1: authority. There is no client path into this function that does anything.
	if (!World || World->GetNetMode() == NM_Client)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::NoAuthority);
	}

	// Step 2: structural validity. Catches NaN and negative damage before any rule runs.
	if (!DamageInfo.IsStructurallyValid() || DamageInfo.DamageAmount <= 0.f)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::ZeroDamage);
	}

	// Step 3: the target must be damageable.
	if (!Target || !Target->GetClass()->ImplementsInterface(UKCFDamageable::StaticClass()))
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetNotDamageable);
	}

	if (IKCFDamageable::Execute_IsDeadOrDestroyed(Target))
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetAlreadyDead);
	}

	// Step 4: the match must be live. Without this, a projectile already in flight when the match
	// ends would still score a kill after the results screen appeared.
	const AKCFGameState* GameState = World->GetGameState<AKCFGameState>();
	if (GameState && !UKCFMatchStateMachine::IsLiveState(GameState->GetMatchState()) && !DamageInfo.bIgnoreInvulnerability)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::MatchNotLive);
	}

	// Step 5: friendly fire policy.
	EKCFDamageRejectReason FriendlyFireReason = EKCFDamageRejectReason::None;
	const float FriendlyFireScale = ResolveFriendlyFireScale(Target, DamageInfo, FriendlyFireReason);
	if (FriendlyFireScale <= 0.f)
	{
		return FKCFDamageResult::Rejected(FriendlyFireReason);
	}

	// Step 6: immunity, including spawn protection and blanket-immunity tags.
	if (!DamageInfo.bIgnoreInvulnerability && IKCFDamageable::Execute_IsImmuneToDamage(Target, DamageInfo))
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetInvulnerable);
	}

	// Step 7: category multiplier from the target's own vehicle definition.
	FKCFDamageInfo Resolved = DamageInfo;
	Resolved.DamageAmount *= FriendlyFireScale * ResolveCategoryMultiplier(Target, DamageInfo.Category);

	if (!Resolved.DamageTags.HasTag(KCFGameplayTags::DamageCategoryToTag(Resolved.Category)))
	{
		Resolved.DamageTags.AddTag(KCFGameplayTags::DamageCategoryToTag(Resolved.Category));
	}

	if (Resolved.DamageAmount <= 0.f)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::ZeroDamage);
	}

	// Step 8: shield and health, delegated to the target.
	const FKCFDamageResult Result = IKCFDamageable::Execute_ApplyKCFDamage(Target, Resolved);

	if (!Result.bWasApplied)
	{
		return Result;
	}

	// Step 9: knockback.
	ApplyKnockbackToTarget(Target, Resolved);

	// Step 10: ledger and statistics.
	const float TotalDamage = Result.GetTotalDamage();

	if (AKCFPlayerState* InstigatorState = Resolved.InstigatorPlayerState)
	{
		RecordLedgerEntry(Target, InstigatorState, TotalDamage);
		InstigatorState->AddDamageDealt(TotalDamage);
	}

	if (AKCFPlayerState* VictimState = IKCFDamageable::Execute_GetOwningKCFPlayerState(Target))
	{
		VictimState->AddDamageReceived(TotalDamage);
	}

	UE_LOG(LogKCFCombat, Verbose, TEXT("%.1f damage to '%s' from '%s' (%s)%s."),
		TotalDamage,
		*Target->GetName(),
		*GetNameSafe(Resolved.InstigatorPlayerState),
		*Resolved.WeaponId.ToString(),
		Result.bWasLethal ? TEXT(" [lethal]") : TEXT(""));

	return Result;
}

int32 UKCFDamageSubsystem::ApplyRadialDamage(
	const FVector& Origin,
	float Radius,
	float MinimumDamageFraction,
	const FKCFDamageInfo& DamageInfo,
	const TArray<AActor*>& IgnoredActors)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.f)
	{
		return 0;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KCFRadialDamage), /*bTraceComplex=*/false);
	Params.AddIgnoredActors(IgnoredActors);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(
		Overlaps,
		Origin,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Radius),
		Params);

	// De-duplicate: a single kart can produce several overlap results through different components,
	// and without this it would take the explosion once per component.
	TSet<AActor*> Damaged;
	Damaged.Reserve(Overlaps.Num());

	int32 Count = 0;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || Damaged.Contains(HitActor))
		{
			continue;
		}
		Damaged.Add(HitActor);

		const FVector TargetLocation = HitActor->GetActorLocation();
		const float Distance = FVector::Distance(Origin, TargetLocation);
		const float Alpha = FMath::Clamp(Distance / Radius, 0.f, 1.f);

		// Linear falloff from full damage at the centre to MinimumDamageFraction at the rim.
		const float Falloff = FMath::Lerp(1.f, FMath::Clamp(MinimumDamageFraction, 0.f, 1.f), Alpha);

		FKCFDamageInfo RadialInfo = DamageInfo;
		RadialInfo.DamageAmount = DamageInfo.DamageAmount * Falloff;
		RadialInfo.KnockbackForce = DamageInfo.KnockbackForce * Falloff;
		RadialInfo.HitLocation = TargetLocation;

		// Knockback radiates outward from the blast, which is both correct and much more readable
		// to a player than knockback along the projectile's travel direction.
		const FVector Outward = (TargetLocation - Origin).GetSafeNormal();
		RadialInfo.KnockbackDirection = Outward.IsNearlyZero() ? FVector::UpVector : Outward;

		if (ApplyDamage(HitActor, RadialInfo).bWasApplied)
		{
			++Count;
		}
	}

	return Count;
}

// -------------------------------------------------------------------------------------------------
// Rules
// -------------------------------------------------------------------------------------------------

float UKCFDamageSubsystem::ResolveFriendlyFireScale(const AActor* Target, const FKCFDamageInfo& DamageInfo, EKCFDamageRejectReason& OutReason) const
{
	OutReason = EKCFDamageRejectReason::None;

	if (DamageInfo.bIgnoreFriendlyFireRules)
	{
		return 1.f;
	}

	const AActor* InstigatorActor = DamageInfo.InstigatorPlayerState
		? Cast<AActor>(DamageInfo.InstigatorPlayerState.Get())
		: DamageInfo.DamageCauser.Get();

	if (!InstigatorActor)
	{
		// Environmental damage has no instigator and is never friendly.
		return 1.f;
	}

	const int32 InstigatorTeam = DamageInfo.InstigatorPlayerState
		? IKCFTeamAgent::Execute_GetKCFTeamId(DamageInfo.InstigatorPlayerState)
		: IKCFTeamAgent::ResolveTeamId(InstigatorActor);

	const int32 TargetTeam = IKCFTeamAgent::ResolveTeamId(Target);

	// Free-for-all: nobody is friendly, so there is nothing to filter.
	if (InstigatorTeam == KCF_NO_TEAM || TargetTeam == KCF_NO_TEAM || InstigatorTeam != TargetTeam)
	{
		return 1.f;
	}

	const UWorld* World = GetWorld();
	const AKCFGameState* GameState = World ? World->GetGameState<AKCFGameState>() : nullptr;
	const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;

	if (!Config || !Config->bFriendlyFire)
	{
		OutReason = EKCFDamageRejectReason::FriendlyFireDisabled;
		return 0.f;
	}

	return FMath::Clamp(Config->FriendlyFireScale, 0.f, 1.f);
}

float UKCFDamageSubsystem::ResolveCategoryMultiplier(const AActor* Target, EKCFDamageCategory Category) const
{
	if (const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(Target))
	{
		if (const UKCFVehicleDataAsset* Vehicle = Kart->GetVehicleData())
		{
			return Vehicle->GetDamageMultiplier(Category);
		}
	}

	// Non-kart damageables (destructible props, objectives) have no per-category table.
	return 1.f;
}

void UKCFDamageSubsystem::ApplyKnockbackToTarget(AActor* Target, const FKCFDamageInfo& DamageInfo) const
{
	if (DamageInfo.KnockbackForce <= 0.f)
	{
		return;
	}

	const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(Target);
	if (!Kart)
	{
		return;
	}

	const TScriptInterface<IKCFVehicleMovement> Movement = Kart->GetVehicleMovement();
	IKCFVehicleMovement* MovementPtr = Movement.GetInterface();
	if (!MovementPtr)
	{
		return;
	}

	FVector Direction = DamageInfo.KnockbackDirection;
	if (Direction.IsNearlyZero())
	{
		// Derive a direction from the impact when the caller did not supply one.
		Direction = (Target->GetActorLocation() - DamageInfo.HitLocation).GetSafeNormal();
	}

	if (Direction.IsNearlyZero())
	{
		Direction = FVector::UpVector;
	}

	// A small upward bias makes a hit read as a hit: a purely horizontal shove on a grounded kart is
	// largely absorbed by the ground contact and barely registers.
	Direction.Z = FMath::Max(Direction.Z, 0.25f);
	Direction.Normalize();

	MovementPtr->ApplyKnockback(Direction * DamageInfo.KnockbackForce);
}

// -------------------------------------------------------------------------------------------------
// Assist ledger
// -------------------------------------------------------------------------------------------------

void UKCFDamageSubsystem::RecordLedgerEntry(const AActor* Victim, AKCFPlayerState* Instigator, float Amount)
{
	if (!Victim || !Instigator || Amount <= 0.f)
	{
		return;
	}

	TArray<FKCFDamageLedgerEntry>& Entries = AssistLedger.FindOrAdd(Victim);
	const float Now = GetServerTime();

	PruneLedger(Entries, Now);

	// Accumulate onto an existing entry for the same instigator rather than appending, so a machine
	// gun does not produce a hundred ledger entries for one engagement.
	for (FKCFDamageLedgerEntry& Entry : Entries)
	{
		if (Entry.Instigator.Get() == Instigator)
		{
			Entry.Amount += Amount;
			Entry.ServerTime = Now;
			return;
		}
	}

	FKCFDamageLedgerEntry NewEntry;
	NewEntry.Instigator = Instigator;
	NewEntry.Amount = Amount;
	NewEntry.ServerTime = Now;
	Entries.Add(NewEntry);
}

void UKCFDamageSubsystem::PruneLedger(TArray<FKCFDamageLedgerEntry>& Entries, float Now) const
{
	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const float Window = Settings ? Settings->AssistWindowSeconds : 10.f;

	Entries.RemoveAll([Now, Window](const FKCFDamageLedgerEntry& Entry)
	{
		return !Entry.Instigator.IsValid() || (Now - Entry.ServerTime) > Window;
	});
}

TArray<AKCFPlayerState*> UKCFDamageSubsystem::CollectAssisters(const AActor* Victim, const AKCFPlayerState* Killer) const
{
	TArray<AKCFPlayerState*> Assisters;

	const TArray<FKCFDamageLedgerEntry>* Entries = AssistLedger.Find(Victim);
	if (!Entries)
	{
		return Assisters;
	}

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const float Window = Settings ? Settings->AssistWindowSeconds : 10.f;
	const float RequiredFraction = Settings ? Settings->AssistDamageFraction : 0.2f;

	// The threshold is a fraction of the victim's maximum health, so an assist means "did a
	// meaningful share of the work" rather than "touched them once".
	float VictimMaxHealth = 100.f;
	if (const AKCFKartPawn* Kart = Cast<AKCFKartPawn>(Victim))
	{
		if (const UKCFVehicleDataAsset* Vehicle = Kart->GetVehicleData())
		{
			VictimMaxHealth = FMath::Max(1.f, Vehicle->MaxHealth);
		}
	}

	const float RequiredDamage = VictimMaxHealth * RequiredFraction;
	const float Now = GetServerTime();

	for (const FKCFDamageLedgerEntry& Entry : *Entries)
	{
		AKCFPlayerState* Instigator = Entry.Instigator.Get();
		if (!Instigator || Instigator == Killer)
		{
			continue;
		}

		if ((Now - Entry.ServerTime) <= Window && Entry.Amount >= RequiredDamage)
		{
			Assisters.Add(Instigator);
		}
	}

	return Assisters;
}

void UKCFDamageSubsystem::ClearLedgerForVictim(const AActor* Victim)
{
	AssistLedger.Remove(Victim);
}

void UKCFDamageSubsystem::ClearAllLedgers()
{
	AssistLedger.Reset();
}
