// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"

namespace KCFTest
{
	/**
	 * A throwaway game world for one test.
	 *
	 * The framework's server-authoritative guards all go through AActor::HasAuthority, so a test
	 * that wants to exercise damage, inventory or match flow needs a real world with real actors in
	 * it rather than bare UObjects. An actor spawned into a standalone world is ROLE_Authority,
	 * which is exactly the server case these systems are written for.
	 *
	 * Tears the world down in the destructor so a failing test cannot leak a world into the next
	 * one and make the failure look like it came from somewhere else.
	 */
	struct FScopedTestWorld
	{
		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false, TEXT("KCFTestWorld"));
			check(World);

			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);

			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		FScopedTestWorld(const FScopedTestWorld&) = delete;
		FScopedTestWorld& operator=(const FScopedTestWorld&) = delete;

		/** Advances world time so timer-driven behaviour (regeneration, expiry) can be observed. */
		void Advance(float Seconds, float StepSeconds = 1.f / 60.f)
		{
			float Remaining = Seconds;
			while (Remaining > 0.f)
			{
				const float Step = FMath::Min(StepSeconds, Remaining);
				World->Tick(LEVELTICK_All, Step);
				Remaining -= Step;
			}
		}

		AActor* SpawnTestActor()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		}

		/** Creates and registers a component on an actor, the way a constructor subobject would be. */
		template <typename ComponentType>
		ComponentType* AddComponent(AActor* Owner)
		{
			ComponentType* Component = NewObject<ComponentType>(Owner);
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		UWorld* World = nullptr;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
