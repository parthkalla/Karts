// Original work, MIT licensed.

#include "KartCombatFrameworkEditor.h"

#include "Core/KCFLog.h"

void FKartCombatFrameworkEditorModule::StartupModule()
{
	// The validators are UObject classes discovered by the engine's Data Validation framework, so
	// there is nothing to register by hand. Loading the module is enough.
	UE_LOG(LogKartCombatFramework, Verbose, TEXT("Kart Combat Framework editor validation loaded."));
}

void FKartCombatFrameworkEditorModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FKartCombatFrameworkEditorModule, KartCombatFrameworkEditor)
