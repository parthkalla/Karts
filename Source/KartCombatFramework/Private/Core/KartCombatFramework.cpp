// Original work, MIT licensed.

#include "Core/KartCombatFramework.h"

#include "Components/ActorComponent.h"
#include "Core/KCFLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogKartCombatFramework);
DEFINE_LOG_CATEGORY(LogKCFNet);
DEFINE_LOG_CATEGORY(LogKCFCombat);
DEFINE_LOG_CATEGORY(LogKCFMatch);

#define LOCTEXT_NAMESPACE "FKartCombatFrameworkModule"

void FKartCombatFrameworkModule::StartupModule()
{
	// Native gameplay tags register themselves through their static initialisers; nothing else to do.
	// Deliberately no asset loading, no subsystem creation and no content scanning here: enabling the
	// plugin must not cost anything measurable at engine boot.
	UE_LOG(LogKartCombatFramework, Log, TEXT("KartCombatFramework runtime module started."));
}

void FKartCombatFrameworkModule::ShutdownModule()
{
	UE_LOG(LogKartCombatFramework, Log, TEXT("KartCombatFramework runtime module shut down."));
}

FString KCF_NetContext(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return TEXT("[?]");
	}

	const UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return TEXT("[NoWorld]");
	}

	const TCHAR* NetModeText = TEXT("?");
	switch (World->GetNetMode())
	{
	case NM_Standalone:		NetModeText = TEXT("Standalone"); break;
	case NM_DedicatedServer:	NetModeText = TEXT("DedServer"); break;
	case NM_ListenServer:	NetModeText = TEXT("Listen"); break;
	case NM_Client:			NetModeText = TEXT("Client"); break;
	default:				break;
	}

	const TCHAR* RoleText = TEXT("");
	if (const AActor* Actor = Cast<AActor>(WorldContext))
	{
		RoleText = Actor->HasAuthority() ? TEXT("|Auth") : TEXT("|Remote");
	}
	else if (const UActorComponent* Component = Cast<UActorComponent>(WorldContext))
	{
		if (const AActor* Owner = Component->GetOwner())
		{
			RoleText = Owner->HasAuthority() ? TEXT("|Auth") : TEXT("|Remote");
		}
	}

	return FString::Printf(TEXT("[%s%s]"), NetModeText, RoleText);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKartCombatFrameworkModule, KartCombatFramework)
