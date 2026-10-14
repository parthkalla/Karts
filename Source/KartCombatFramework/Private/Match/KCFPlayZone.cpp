// Original work, MIT licensed.

#include "Match/KCFPlayZone.h"

#include "Core/KCFGameplayTags.h"
#include "Core/KCFLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Subsystems/KCFDamageSubsystem.h"
#include "TimerManager.h"
#include "Vehicles/KCFKartPawn.h"

AKCFPlayZone::AKCFPlayZone()
{
	// No Tick. The radius is a function of two replicated timestamps, so every machine can compute
	// it on demand, and the damage pass is a one-per-second timer rather than per-frame work.
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	// The schedule replicates as five scalars when a phase changes and is silent in between, so a
	// slow update rate costs nothing and keeps the zone out of the per-frame replication budget.
	SetNetUpdateFrequency(2.f);
}

void AKCFPlayZone::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && Phases.Num() == 0)
	{
		UE_LOG(LogKCFMatch, Warning,
			TEXT("Play zone '%s' has no phases. It will never close, so a battle royale on this level cannot end by attrition."),
			*GetName());
	}
}

void AKCFPlayZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	GetWorldTimerManager().ClearTimer(DamageTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AKCFPlayZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKCFPlayZone, ZoneCenter);
	DOREPLIFETIME(AKCFPlayZone, StartRadius);
	DOREPLIFETIME(AKCFPlayZone, TargetRadius);
	DOREPLIFETIME(AKCFPlayZone, ShrinkStartServerTime);
	DOREPLIFETIME(AKCFPlayZone, ShrinkEndServerTime);
	DOREPLIFETIME(AKCFPlayZone, CurrentPhase);
	DOREPLIFETIME(AKCFPlayZone, bZoneActive);
}

float AKCFPlayZone::GetServerTime() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : 0.f;
}

// -------------------------------------------------------------------------------------------------
// Geometry
// -------------------------------------------------------------------------------------------------

float AKCFPlayZone::GetCurrentRadius() const
{
	if (!bZoneActive)
	{
		return StartRadius;
	}

	const float Now = GetServerTime();

	if (Now <= ShrinkStartServerTime || ShrinkEndServerTime <= ShrinkStartServerTime)
	{
		return StartRadius;
	}

	if (Now >= ShrinkEndServerTime)
	{
		return TargetRadius;
	}

	// Derived rather than replicated. Every machine has the same two timestamps and the same
	// synchronised clock, so they all arrive at the same radius with no per-frame traffic at all.
	const float Alpha = (Now - ShrinkStartServerTime) / (ShrinkEndServerTime - ShrinkStartServerTime);
	return FMath::Lerp(StartRadius, TargetRadius, Alpha);
}

float AKCFPlayZone::GetSecondsUntilShrink() const
{
	return bZoneActive ? FMath::Max(0.f, ShrinkStartServerTime - GetServerTime()) : 0.f;
}

bool AKCFPlayZone::IsShrinking() const
{
	const float Now = GetServerTime();
	return bZoneActive && Now > ShrinkStartServerTime && Now < ShrinkEndServerTime;
}

bool AKCFPlayZone::IsInsideZone(const FVector& Location) const
{
	// Horizontal distance only: a kart on a ramp is inside or outside the ring regardless of how
	// high it is, which is what a player reads off a minimap.
	const FVector Delta = Location - ZoneCenter;
	return FVector2D(Delta.X, Delta.Y).SizeSquared() <= FMath::Square(GetCurrentRadius());
}

float AKCFPlayZone::GetCurrentDamagePerSecond() const
{
	return Phases.IsValidIndex(CurrentPhase) ? Phases[CurrentPhase].DamagePerSecond : 0.f;
}

// -------------------------------------------------------------------------------------------------
// Schedule (server)
// -------------------------------------------------------------------------------------------------

void AKCFPlayZone::StartZone(const FVector& InCenter, float InStartRadius)
{
	if (!HasAuthority())
	{
		return;
	}

	ZoneCenter = InCenter;
	StartRadius = FMath::Max(100.f, InStartRadius);
	TargetRadius = StartRadius;
	bZoneActive = true;

	OnRep_ZoneState();

	GetWorldTimerManager().SetTimer(DamageTimerHandle, this, &AKCFPlayZone::ApplyZoneDamage, DamageInterval, true);

	EnterPhase(0);
}

void AKCFPlayZone::StopZone()
{
	if (!HasAuthority())
	{
		return;
	}

	bZoneActive = false;
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	GetWorldTimerManager().ClearTimer(DamageTimerHandle);

	OnRep_ZoneState();
}

void AKCFPlayZone::EnterPhase(int32 PhaseIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!Phases.IsValidIndex(PhaseIndex))
	{
		// Out of phases. The zone stays where it is and keeps damaging anyone outside, which is the
		// right end state: it has already closed as far as the designer asked it to.
		CurrentPhase = Phases.Num() - 1;
		GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
		OnRep_ZoneState();
		return;
	}

	const FKCFPlayZonePhase& Phase = Phases[PhaseIndex];
	const float Now = GetServerTime();

	CurrentPhase = PhaseIndex;
	StartRadius = GetCurrentRadius();
	TargetRadius = FMath::Min(StartRadius, FMath::Max(100.f, Phase.TargetRadius));
	ShrinkStartServerTime = Now + Phase.HoldSeconds;
	ShrinkEndServerTime = ShrinkStartServerTime + Phase.ShrinkSeconds;
	ZoneCenter = ChooseNextCenter(TargetRadius);

	OnRep_ZoneState();

	// One timer for the whole phase. The radius in between is interpolated, not stepped, so there
	// is nothing to update until the next phase begins.
	const float PhaseDuration = Phase.HoldSeconds + Phase.ShrinkSeconds;
	GetWorldTimerManager().SetTimer(
		PhaseTimerHandle,
		FTimerDelegate::CreateUObject(this, &AKCFPlayZone::EnterPhase, PhaseIndex + 1),
		FMath::Max(0.1f, PhaseDuration),
		false);

	UE_LOG(LogKCFMatch, Log, TEXT("Play zone phase %d: %.0f -> %.0f cm over %.0fs after a %.0fs hold."),
		PhaseIndex, StartRadius, TargetRadius, Phase.ShrinkSeconds, Phase.HoldSeconds);
}

FVector AKCFPlayZone::ChooseNextCenter(float NewRadius) const
{
	const float CurrentRadius = GetCurrentRadius();
	const float MaxDrift = FMath::Max(0.f, CurrentRadius - NewRadius) * FMath::Clamp(CenterDriftFraction, 0.f, 1.f);

	if (MaxDrift <= KINDA_SMALL_NUMBER)
	{
		return ZoneCenter;
	}

	// Bounded by the difference in radii, so the new circle is always fully inside the old one.
	// A zone that drifted further would put players who were already safe outside through no fault
	// of their own.
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Distance = FMath::FRandRange(0.f, MaxDrift);

	return ZoneCenter + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
}

void AKCFPlayZone::ApplyZoneDamage()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !bZoneActive || !World)
	{
		return;
	}

	const float DamagePerSecond = GetCurrentDamagePerSecond();
	if (DamagePerSecond <= 0.f)
	{
		return;
	}

	UKCFDamageSubsystem* Damage = UKCFDamageSubsystem::Get(this);
	if (!Damage)
	{
		return;
	}

	for (TActorIterator<AKCFKartPawn> It(World); It; ++It)
	{
		AKCFKartPawn* Kart = *It;
		if (!Kart || IsInsideZone(Kart->GetActorLocation()))
		{
			continue;
		}

		FKCFDamageInfo Info(DamagePerSecond * DamageInterval, EKCFDamageCategory::Environmental);
		Info.DamageCauser = this;
		Info.HitLocation = Kart->GetActorLocation();
		Info.DamageTags.AddTag(KCFGameplayTags::Damage_Environmental);

		// The zone ignores shields and spawn protection deliberately: it is the one pressure in the
		// mode that cannot be waited out, and anything that could block it would stall the match.
		Info.bIgnoreShield = true;
		Info.bIgnoreInvulnerability = true;
		Info.bIgnoreFriendlyFireRules = true;

		Damage->ApplyDamage(Kart, Info);
	}
}

void AKCFPlayZone::OnRep_ZoneState()
{
	OnZoneChanged.Broadcast();

	if (bZoneActive && Phases.IsValidIndex(CurrentPhase))
	{
		OnPhaseStarted.Broadcast(CurrentPhase, GetSecondsUntilShrink());
	}
}
