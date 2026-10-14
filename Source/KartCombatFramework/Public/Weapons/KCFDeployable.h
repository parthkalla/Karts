// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/Actor.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "KCFDeployable.generated.h"

class AKCFPlayerState;
class UKCFWeaponDataAsset;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A placed hazard: mine, oil slick, trap.
 *
 * Distinct from AKCFProjectile because the behaviour genuinely differs rather than being a
 * projectile with zero speed. A deployable stays put, arms after a delay so the placer can escape
 * it, triggers on proximity rather than on impact, and is net-dormant while it waits - which is
 * what makes it cheap to leave a dozen of them around an arena.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFDeployable : public AActor, public IKCFTeamAgent
{
	GENERATED_BODY()

public:
	AKCFDeployable();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	//~ Begin IKCFTeamAgent
	virtual int32 GetKCFTeamId_Implementation() const override { return TeamId; }
	virtual void SetKCFTeamId_Implementation(int32 NewTeamId) override { TeamId = NewTeamId; }
	//~ End IKCFTeamAgent

	/** Configures and arms the deployable. Server only, called before FinishSpawning. */
	void InitialiseDeployable(UKCFWeaponDataAsset* InWeaponData, AKCFPlayerState* InInstigatorState, AActor* InInstigatorActor, int32 InTeamId);

	UFUNCTION(BlueprintPure, Category = "KCF|Deployable")
	bool IsArmed() const { return bArmed; }

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Armed"))
	void BP_OnArmed();

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Triggered"))
	void BP_OnTriggered(FVector Location);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USphereComponent> TriggerSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Deployable")
	TObjectPtr<UKCFWeaponDataAsset> WeaponData;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Deployable")
	int32 TeamId = KCF_NO_TEAM;

	UPROPERTY(ReplicatedUsing = OnRep_Armed, BlueprintReadOnly, Category = "KCF|Deployable")
	bool bArmed = false;

	UPROPERTY(Transient)
	TObjectPtr<AKCFPlayerState> InstigatorPlayerState;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PlacingActor;

	bool bHasTriggered = false;

	FTimerHandle ArmTimerHandle;

	UFUNCTION() void OnRep_Armed();

	/** Becomes live after the arm delay. Server only. */
	void Arm();

	UFUNCTION()
	void HandleTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Applies damage and removes the deployable. Server only. */
	void Trigger(AActor* TriggeringActor);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastTriggered(FVector Location);
};
