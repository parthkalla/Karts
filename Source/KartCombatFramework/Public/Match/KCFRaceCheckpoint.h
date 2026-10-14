// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KCFRaceCheckpoint.generated.h"

class AKCFKartPawn;
class UBoxComponent;

/**
 * One gate on a race circuit.
 *
 * Checkpoints exist for two reasons, and only the second one is about the lap counter. The first is
 * anti-shortcut: a lap only counts when every checkpoint was passed in order, so cutting the track
 * gains nothing. The second is respawn - a kart that falls off the circuit comes back at the last
 * gate it passed rather than at the start line.
 *
 * Server-only by construction. The actor does not replicate: clients never need to know a gate
 * exists, and the only observable result of touching one is the lap counter on the player state.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFRaceCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AKCFRaceCheckpoint();

	//~ Begin AActor
	virtual void BeginPlay() override;
	//~ End AActor

	/**
	 * Position in the lap, starting at 0. Gates are sorted by this, so the numbers only have to be
	 * increasing, not contiguous - which means inserting a gate into an existing circuit does not
	 * mean renumbering every gate after it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Race")
	int32 CheckpointIndex = 0;

	/** Crossing this gate with every other gate already passed completes a lap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Race")
	bool bIsFinishLine = false;

	/**
	 * Transform a kart respawning at this gate is placed on. Separate from the actor transform so a
	 * gate can span the full width of the track while the respawn point sits on the racing line.
	 */
	UFUNCTION(BlueprintPure, Category = "KCF|Race")
	FTransform GetRespawnTransform() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Race")
	UBoxComponent* GetTriggerVolume() const { return TriggerVolume; }

protected:
	/** The gate itself. Sized in the level to span the drivable width of the track. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UBoxComponent> TriggerVolume;

	/** Where a kart is placed when it respawns at this gate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USceneComponent> RespawnPoint;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
