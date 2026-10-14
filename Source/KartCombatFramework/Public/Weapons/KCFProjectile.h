// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/Actor.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "KCFProjectile.generated.h"

class AKCFPlayerState;
class UKCFWeaponDataAsset;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A server-authoritative projectile.
 *
 * The server owns the trajectory and the hit. Clients receive the actor through replication and see
 * it move via replicated movement; their copy has collision disabled entirely, so there is no way
 * for a client-side hit to influence anything. The explosion a client sees is a cosmetic multicast
 * sent *after* the server has already resolved and applied the damage.
 *
 * Reused through UKCFActorPoolSubsystem where pooling is enabled: a match can spawn hundreds of
 * these, and on mobile the allocation and garbage collection cost of that is worth avoiding.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFProjectile : public AActor, public IKCFTeamAgent
{
	GENERATED_BODY()

public:
	AKCFProjectile();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void LifeSpanExpired() override;
	//~ End AActor

	//~ Begin IKCFTeamAgent
	virtual int32 GetKCFTeamId_Implementation() const override { return TeamId; }
	virtual void SetKCFTeamId_Implementation(int32 NewTeamId) override { TeamId = NewTeamId; }
	//~ End IKCFTeamAgent

	/**
	 * Configures the projectile from its weapon definition and fires it.
	 * Server only, and called before FinishSpawning so that movement is configured before the first
	 * tick rather than after a frame of travel at the wrong speed.
	 */
	void InitialiseProjectile(UKCFWeaponDataAsset* InWeaponData, AKCFPlayerState* InInstigatorState, AActor* InInstigatorActor, int32 InTeamId);

	/** Resets state so a pooled instance can be fired again. */
	void ResetForReuse();

	UFUNCTION(BlueprintPure, Category = "KCF|Projectile")
	const UKCFWeaponDataAsset* GetWeaponData() const { return WeaponData; }

	/** Cosmetic hook for the impact, implemented by the project in Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Explode"))
	void BP_OnExplode(FVector Location, FVector Normal);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/**
	 * The weapon that fired this. Replicated so clients can play the right impact effect without a
	 * second RPC carrying the same information.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Projectile")
	TObjectPtr<UKCFWeaponDataAsset> WeaponData;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Projectile")
	int32 TeamId = KCF_NO_TEAM;

	/** Who gets the kill. Server-only: a client has no use for it and no right to it. */
	UPROPERTY(Transient)
	TObjectPtr<AKCFPlayerState> InstigatorPlayerState;

	/** The kart that fired. Used to exclude self-damage and to seed homing exclusions. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> FiringActor;

	/** Prevents a projectile from resolving twice when two components overlap in one frame. */
	bool bHasDetonated = false;

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Resolves damage on the server and tells clients to play the explosion. */
	void Detonate(const FVector& Location, const FVector& Normal, AActor* DirectHitActor);

	/** Cosmetic only. The damage it represents has already been applied on the server. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplode(FVector Location, FVector Normal);

	/** Finds the best homing target in front of the projectile. Server only. */
	AActor* AcquireHomingTarget() const;

	/** True when this projectile is allowed to damage the given actor. */
	bool CanHit(const AActor* Other) const;
};
