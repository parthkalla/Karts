// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Vehicles/KCFKartPawn.h"
#include "KCFChaosKartPawn.generated.h"

class UKCFChaosKartMovementComponent;

/**
 * A kart driven by the Chaos wheeled vehicle solver.
 *
 * AKCFKartPawn is built around a box collider root with the meshes hanging off it, which is right
 * for the arcade simulation and wrong for Chaos: the solver needs a skeletal mesh with a physics
 * asset and wheel bones as the simulating body. This subclass does that reparenting - skeletal mesh
 * becomes the root, the box becomes a query-only overlap volume attached to it - and swaps the
 * movement subobject for the Chaos bridge. Everything else about the kart is unchanged, which is
 * the point: weapons, powerups, health, damage, inventory, respawn and the whole match flow are
 * identical because none of them ever referred to the movement backend by name.
 *
 * Two things a project must still supply, because they are content and cannot be code:
 *   - A skeletal mesh with a physics asset and wheel bones, set on the vehicle Data Asset.
 *   - Wheel setups on the movement component (the WheelSetups array), configured as for any Chaos
 *     vehicle. Without them the solver has nothing to drive and the kart will not move.
 *
 * See Documentation/VehicleSystem.md for the full setup walkthrough and for why the arcade backend
 * remains the default on mobile.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORKCHAOSVEHICLES_API AKCFChaosKartPawn : public AKCFKartPawn
{
	GENERATED_BODY()

public:
	AKCFChaosKartPawn(const FObjectInitializer& ObjectInitializer);

	/** The Chaos bridge, already cast. Null only if a subclass substituted the backend again. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	UKCFChaosKartMovementComponent* GetChaosKartMovement() const;

protected:
	//~ Begin AKCFKartPawn
	virtual void ApplyVehicleVisuals() override;
	//~ End AKCFKartPawn
};
