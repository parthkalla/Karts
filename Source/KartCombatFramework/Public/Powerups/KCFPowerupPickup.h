// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KCFPowerupPickup.generated.h"

class AKCFPowerupSpawner;
class UKCFPowerupDataAsset;
class USphereComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFPickupCollectedSignature, AActor*, Collector, UKCFPowerupDataAsset*, Powerup);

/**
 * A powerup crate sitting in the arena.
 *
 * The actor exists on every machine so that it can be seen and so that its disappearance is
 * visible, but only the server decides that it was collected. A client's overlap event is ignored
 * entirely - it is not evidence, it is just the same physics running locally.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPowerupPickup : public AActor
{
	GENERATED_BODY()

public:
	AKCFPowerupPickup();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	UPROPERTY(BlueprintAssignable, Category = "KCF|Powerup")
	FKCFPickupCollectedSignature OnCollected;

	/** Configures the pickup. Server only, called before FinishSpawning. */
	void InitialisePickup(UKCFPowerupDataAsset* InPowerup, AKCFPowerupSpawner* InSpawner);

	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	UKCFPowerupDataAsset* GetPowerup() const { return Powerup; }

	UFUNCTION(BlueprintPure, Category = "KCF|Powerup")
	bool IsAvailable() const { return !bCollected; }

	/** Rotation speed of the floating crate, degrees per second. Cosmetic, client only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Presentation")
	float SpinRateDegreesPerSecond = 90.f;

	/** Vertical bob amplitude in cm. 0 disables bobbing and the cosmetic tick with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Presentation", meta = (ClampMin = "0.0", Units = "cm"))
	float BobAmplitude = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Presentation", meta = (ClampMin = "0.0"))
	float BobFrequency = 1.2f;

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Collected"))
	void BP_OnCollected();

protected:
	//~ Begin AActor
	/**
	 * Ticks only on machines that render the pickup, and only to spin and bob the mesh. The tick
	 * function is disabled outright on a dedicated server and whenever both effects are switched
	 * off, so the cost is zero rather than "small".
	 */
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USphereComponent> CollectionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_Powerup, BlueprintReadOnly, Category = "KCF|Powerup")
	TObjectPtr<UKCFPowerupDataAsset> Powerup;

	UPROPERTY(ReplicatedUsing = OnRep_Collected, BlueprintReadOnly, Category = "KCF|Powerup")
	bool bCollected = false;

	/** The spawner that owns this pickup, so it can be told to start its respawn timer. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AKCFPowerupSpawner> OwningSpawner;

	UFUNCTION() void OnRep_Powerup();
	UFUNCTION() void OnRep_Collected();

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Applies the pickup to a kart. Server only. */
	void TryCollect(AActor* Collector);

	/** Loads and applies the mesh from the definition. Client-side cosmetics only. */
	void ApplyPresentation();

	/** Enables the cosmetic tick only where it would actually be seen. */
	void UpdateCosmeticTick();

	FVector BaseMeshLocation = FVector::ZeroVector;
	float BobTime = 0.f;
};
