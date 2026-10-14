// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Optional bridge that lets a kart be driven by a Chaos wheeled vehicle instead of the framework's
 * arcade simulation.
 *
 * The module has no startup work of its own. Its entire purpose is to be the one place in the
 * plugin that depends on the ChaosVehicles module, so that a project using the default arcade
 * backend never links the vehicle solver.
 */
class FKartCombatFrameworkChaosVehiclesModule : public IModuleInterface
{
};
