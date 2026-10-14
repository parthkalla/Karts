// Original work, MIT licensed.

#include "Subsystems/KCFActorPoolSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UKCFActorPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UKCFActorPoolSubsystem::Deinitialize()
{
	DrainPools();
	Super::Deinitialize();
}

bool UKCFActorPoolSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UKCFActorPoolSubsystem* UKCFActorPoolSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine && WorldContextObject
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UKCFActorPoolSubsystem>() : nullptr;
}

bool UKCFActorPoolSubsystem::IsPoolingEnabled() const
{
	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	return Settings && Settings->bEnableActorPooling;
}

AActor* UKCFActorPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform, AActor* Owner, APawn* InstigatorPawn)
{
	UWorld* World = GetWorld();
	if (!World || !ActorClass)
	{
		return nullptr;
	}

	if (IsPoolingEnabled())
	{
		if (FKCFActorPoolEntry* Entry = Pools.Find(ActorClass))
		{
			while (Entry->Instances.Num() > 0)
			{
				AActor* Candidate = Entry->Instances.Pop(EAllowShrinking::No);

				// A pooled actor can still have been destroyed out from under the pool, for example
				// by a streaming level unload. Skipping invalid entries is cheaper than trying to
				// keep the pool perfectly in sync with actor lifetimes.
				if (IsValid(Candidate))
				{
					ActivateActor(Candidate, SpawnTransform, Owner, InstigatorPawn);
					return Candidate;
				}
			}
		}
	}

	// Empty pool, or pooling disabled: a normal spawn. This is the fallback that makes pooling a
	// pure optimisation rather than a dependency.
	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = InstigatorPawn;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	return World->SpawnActor<AActor>(ActorClass, SpawnTransform, Params);
}

void UKCFActorPoolSubsystem::ReleaseActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	if (!IsPoolingEnabled())
	{
		Actor->Destroy();
		return;
	}

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const int32 MaxPerClass = Settings ? Settings->MaxPooledActorsPerClass : 32;

	const TSubclassOf<AActor> Key = Actor->GetClass();
	FKCFActorPoolEntry& Entry = Pools.FindOrAdd(Key);

	// A full pool destroys rather than growing without bound. The ceiling is what stops a long
	// match from holding hundreds of idle projectiles in memory on a phone.
	if (Entry.Instances.Num() >= MaxPerClass)
	{
		Actor->Destroy();
		return;
	}

	DeactivateActor(Actor);
	Entry.Instances.Add(Actor);
}

void UKCFActorPoolSubsystem::PrewarmPool(TSubclassOf<AActor> ActorClass, int32 Count)
{
	UWorld* World = GetWorld();
	if (!World || !ActorClass || Count <= 0 || !IsPoolingEnabled())
	{
		return;
	}

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const int32 MaxPerClass = Settings ? Settings->MaxPooledActorsPerClass : 32;

	FKCFActorPoolEntry& Entry = Pools.FindOrAdd(ActorClass);
	const int32 ToCreate = FMath::Min(Count, MaxPerClass - Entry.Instances.Num());

	for (int32 Index = 0; Index < ToCreate; ++Index)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AActor* Actor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, Params))
		{
			DeactivateActor(Actor);
			Entry.Instances.Add(Actor);
		}
	}

	UE_LOG(LogKartCombatFramework, Verbose, TEXT("Pre-warmed %d instances of %s."), ToCreate, *GetNameSafe(ActorClass));
}

void UKCFActorPoolSubsystem::DrainPools()
{
	for (TPair<TSubclassOf<AActor>, FKCFActorPoolEntry>& Pair : Pools)
	{
		for (AActor* Actor : Pair.Value.Instances)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	Pools.Reset();
}

int32 UKCFActorPoolSubsystem::GetPooledCount(TSubclassOf<AActor> ActorClass) const
{
	const FKCFActorPoolEntry* Entry = Pools.Find(ActorClass);
	return Entry ? Entry->Instances.Num() : 0;
}

void UKCFActorPoolSubsystem::DeactivateActor(AActor* Actor) const
{
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);

	// Lifespan has to be cleared explicitly: an actor parked with a pending lifespan would destroy
	// itself while sitting in the pool and hand out an invalid instance later.
	Actor->SetLifeSpan(0.f);

	// Dormancy keeps a parked actor from consuming replication bandwidth. It is flushed again when
	// the actor is handed back out.
	Actor->SetNetDormancy(DORM_DormantAll);

	if (Actor->GetClass()->ImplementsInterface(UKCFPoolableActor::StaticClass()))
	{
		IKCFPoolableActor::Execute_OnReturnedToPool(Actor);
	}
}

void UKCFActorPoolSubsystem::ActivateActor(AActor* Actor, const FTransform& SpawnTransform, AActor* Owner, APawn* InstigatorPawn) const
{
	Actor->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
	Actor->SetOwner(Owner);
	Actor->SetInstigator(InstigatorPawn);

	if (Actor->GetClass()->ImplementsInterface(UKCFPoolableActor::StaticClass()))
	{
		// Reset runs before the actor becomes visible and collidable, so a half-reset actor is
		// never observable.
		IKCFPoolableActor::Execute_OnAcquiredFromPool(Actor);
	}

	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(Actor->PrimaryActorTick.bCanEverTick);
	Actor->FlushNetDormancy();
	Actor->SetNetDormancy(DORM_Awake);
}
