// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Runtime module for the Kart Combat Framework.
 *
 * The module intentionally does almost nothing on startup: it registers native gameplay tags and
 * nothing else. No assets are loaded, no subsystems are forced into existence, and no content is
 * scanned, so enabling the plugin in a large project costs effectively nothing at boot.
 */
class FKartCombatFrameworkModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return true; }
	//~ End IModuleInterface

	/** Convenience accessor. Returns nullptr when the module is not loaded. */
	static FKartCombatFrameworkModule* GetPtr()
	{
		return FModuleManager::GetModulePtr<FKartCombatFrameworkModule>(TEXT("KartCombatFramework"));
	}
};
