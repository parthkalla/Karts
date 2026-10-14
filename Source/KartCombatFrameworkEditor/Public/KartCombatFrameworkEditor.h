// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Editor-only companion to the runtime module.
 *
 * Its entire job is content validation: catching a broken Data Asset or a mismatched catalogue row
 * when it is saved, rather than as a confusing gameplay symptom in a playtest. The validators
 * register themselves with the engine's Data Validation framework, so they also run as part of a
 * commandlet-driven content check in CI.
 *
 * No runtime class depends on anything in here, which is what lets a packaged game ship without it.
 */
class FKartCombatFrameworkEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
