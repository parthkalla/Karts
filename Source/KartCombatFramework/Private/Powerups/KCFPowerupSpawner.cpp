// Original work, MIT licensed.

#include "Powerups/KCFPowerupSpawner.h"

#include "Components/SceneComponent.h"
#include "Core/KCFLog.h"
#include "Data/KCFDataTableRows.h"
#include "Data/KCFPowerupDataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Match/KCFGameMode.h"
#include "Powerups/KCFPowerupPickup.h"
#include "TimerManager.h"

AKCFPowerupSpawner::AKCFPowerupSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SpawnRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnRoot"));
	SetRootComponent(SpawnRoot);

	// Deliberately not replicated. The crates are the observable thing; the spawner is bookkeeping.
	bReplicates = false;

	PickupClass = AKCFPowerupPickup::StaticClass();
}

void AKCFPowerupSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Under AKCFGameMode the match flow owns the start and stop, so crates appear when the match goes
	// live rather than while players are still in the lobby. Without it - a test map, or a project
	// using the spawner on its own - auto-start means what it says and spawning begins immediately.
	if (HasAuthority() && bAutoStart && !GetWorld()->GetAuthGameMode<AKCFGameMode>())
	{
		StartSpawning();
	}
}

void AKCFPowerupSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AKCFPowerupSpawner::StartSpawning()
{
	if (!HasAuthority() || bSpawningEnabled)
	{
		return;
	}

	bSpawningEnabled = true;

	// The first spawn is delayed and the interval is jittered slightly so that a map authored by
	// copy-pasting one spawner does not drop every crate on the same frame.
	const float FirstDelay = FMath::Max(KINDA_SMALL_NUMBER, InitialSpawnDelay + FMath::FRandRange(0.f, SpawnInterval * 0.25f));
	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &AKCFPowerupSpawner::SpawnTick, FirstDelay, false);
}

void AKCFPowerupSpawner::StopSpawning(bool bRemoveExisting)
{
	if (!HasAuthority())
	{
		return;
	}

	bSpawningEnabled = false;
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);

	if (!bRemoveExisting)
	{
		return;
	}

	for (AKCFPowerupPickup* Pickup : ActivePickups)
	{
		if (IsValid(Pickup))
		{
			Pickup->Destroy();
		}
	}
	ActivePickups.Reset();
}

void AKCFPowerupSpawner::PruneActivePickups()
{
	ActivePickups.RemoveAll([](const TObjectPtr<AKCFPowerupPickup>& Pickup)
	{
		return !IsValid(Pickup) || !Pickup->IsAvailable();
	});
}

void AKCFPowerupSpawner::NotifyPickupCollected(AKCFPowerupPickup* Pickup)
{
	if (!HasAuthority())
	{
		return;
	}

	ActivePickups.Remove(Pickup);

	if (IsValid(Pickup))
	{
		// Kept alive just long enough for the collected replication and its cosmetic hook to reach
		// clients. Destroying it in the same frame would race the property update.
		Pickup->SetLifeSpan(0.5f);
	}

	if (!bSpawningEnabled)
	{
		return;
	}

	const float Delay = FMath::Max(KINDA_SMALL_NUMBER, RespawnDelay);
	if (!GetWorldTimerManager().IsTimerActive(SpawnTimerHandle))
	{
		GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &AKCFPowerupSpawner::SpawnTick, Delay, false);
	}
}

void AKCFPowerupSpawner::SpawnTick()
{
	if (!HasAuthority() || !bSpawningEnabled)
	{
		return;
	}

	PruneActivePickups();

	if (ActivePickups.Num() < MaxActivePickups)
	{
		SpawnPickupNow();
	}

	// Rescheduled as a one-shot each time rather than as a repeating timer: the delay differs
	// depending on whether the spawner is topping up or waiting out a respawn.
	const float NextDelay = (ActivePickups.Num() < MaxActivePickups) ? SpawnInterval : FMath::Max(SpawnInterval, RespawnDelay);
	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this, &AKCFPowerupSpawner::SpawnTick, FMath::Max(KINDA_SMALL_NUMBER, NextDelay), false);
}

AKCFPowerupPickup* AKCFPowerupSpawner::SpawnPickupNow()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !PickupClass)
	{
		return nullptr;
	}

	PruneActivePickups();
	if (ActivePickups.Num() >= MaxActivePickups)
	{
		return nullptr;
	}

	UKCFPowerupDataAsset* Chosen = SelectPowerup();
	if (!Chosen)
	{
		KCF_REPORT_RECOVERABLE(LogKartCombatFramework, TEXT("Powerup spawner '%s' has no eligible powerups to spawn."), *GetName());
		return nullptr;
	}

	const FTransform SpawnTransform = GetSpawnTransformForIndex(ActivePickups.Num());

	AKCFPowerupPickup* Pickup = World->SpawnActorDeferred<AKCFPowerupPickup>(
		PickupClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Pickup)
	{
		return nullptr;
	}

	Pickup->InitialisePickup(Chosen, this);
	Pickup->FinishSpawning(SpawnTransform);

	ActivePickups.Add(Pickup);
	return Pickup;
}

FTransform AKCFPowerupSpawner::GetSpawnTransformForIndex(int32 Index) const
{
	FTransform Result = GetActorTransform();
	if (AdditionalSpawnOffsets.IsValidIndex(Index))
	{
		Result.SetLocation(Result.GetLocation() + Result.GetRotation().RotateVector(AdditionalSpawnOffsets[Index]));
	}
	return Result;
}

void AKCFPowerupSpawner::ResolvePool()
{
	if (bPoolResolved)
	{
		return;
	}
	bPoolResolved = true;

	if (PowerupPool.Num() > 0)
	{
		ResolvedPool = PowerupPool;
		return;
	}

	UDataTable* Table = PowerupCatalogue.LoadSynchronous();
	if (!Table)
	{
		return;
	}

	Table->ForeachRow<FKCFPowerupRow>(TEXT("AKCFPowerupSpawner::ResolvePool"),
		[this](const FName& /*RowName*/, const FKCFPowerupRow& Row)
		{
			if (!Row.bEnabled || Row.DropWeight <= 0.f || Row.Powerup.IsNull())
			{
				return;
			}

			if (!RequiredPowerupTags.IsEmpty() && !Row.Tags.HasAll(RequiredPowerupTags))
			{
				return;
			}

			FKCFWeightedPowerupEntry Entry;
			Entry.Powerup = Row.Powerup;
			Entry.Weight = Row.DropWeight;
			ResolvedPool.Add(Entry);
		});
}

UKCFPowerupDataAsset* AKCFPowerupSpawner::SelectPowerup()
{
	ResolvePool();

	float TotalWeight = 0.f;
	for (const FKCFWeightedPowerupEntry& Entry : ResolvedPool)
	{
		if (Entry.Weight > 0.f && !Entry.Powerup.IsNull())
		{
			TotalWeight += Entry.Weight;
		}
	}

	if (TotalWeight <= 0.f)
	{
		return nullptr;
	}

	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (const FKCFWeightedPowerupEntry& Entry : ResolvedPool)
	{
		if (Entry.Weight <= 0.f || Entry.Powerup.IsNull())
		{
			continue;
		}

		Roll -= Entry.Weight;
		if (Roll <= 0.f)
		{
			// Only the chosen definition is loaded. This is the whole reason the pool holds soft
			// pointers: a forty-entry catalogue costs one asset load per spawn, not forty.
			return Entry.Powerup.LoadSynchronous();
		}
	}

	return nullptr;
}
