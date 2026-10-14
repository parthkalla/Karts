// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/KCFDamageableInterface.h"
#include "Interfaces/KCFTeamAgentInterface.h"
#include "KCFKartPawn.generated.h"

class IKCFVehicleMovement;
class UBoxComponent;
class UCameraComponent;
class UKCFBoostComponent;
class UKCFCombatComponent;
class UKCFHealthComponent;
class UKCFInventoryComponent;
class UKCFKartCameraComponent;
class UKCFPowerupComponent;
class UKCFVehicleDataAsset;
class UKCFWeaponComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFKartSimpleSignature, AKCFKartPawn*, Kart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFKartImpactSignature, FVector, ImpactLocation, float, ImpactSpeed);

/**
 * The kart.
 *
 * A composition root, not a god class: every behaviour lives in a component and the pawn's own job
 * is to wire them together, route input, and expose a Blueprint-facing surface. The only logic it
 * implements itself is the part that genuinely belongs to the whole actor - collision damage,
 * spawn protection, the damageable interface, and applying the vehicle definition.
 *
 * Movement is reached through IKCFVehicleMovement, never through a concrete component type, so the
 * same pawn class works with the built-in arcade movement or with the optional Chaos Vehicles bridge.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFKartPawn : public APawn, public IKCFDamageable, public IKCFTeamAgent
{
	GENERATED_BODY()

public:
	AKCFKartPawn(const FObjectInitializer& ObjectInitializer);

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	//~ End AActor

	//~ Begin APawn
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	//~ End APawn

	//~ Begin IKCFDamageable
	virtual FKCFDamageResult ApplyKCFDamage_Implementation(const FKCFDamageInfo& DamageInfo) override;
	virtual bool IsDeadOrDestroyed_Implementation() const override;
	virtual bool IsImmuneToDamage_Implementation(const FKCFDamageInfo& DamageInfo) const override;
	virtual AKCFPlayerState* GetOwningKCFPlayerState_Implementation() const override;
	//~ End IKCFDamageable

	//~ Begin IKCFTeamAgent
	virtual int32 GetKCFTeamId_Implementation() const override;
	virtual void SetKCFTeamId_Implementation(int32 NewTeamId) override;
	//~ End IKCFTeamAgent

	// --- Components ----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFHealthComponent* GetHealthComponent() const { return HealthComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFPowerupComponent* GetPowerupComponent() const { return PowerupComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFBoostComponent* GetBoostComponent() const { return BoostComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFCombatComponent* GetCombatComponent() const { return CombatComponent; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UKCFKartCameraComponent* GetKartCamera() const { return CameraBoom; }
	UFUNCTION(BlueprintPure, Category = "KCF|Kart") UBoxComponent* GetCollisionBox() const { return CollisionBox; }

	/** The active movement backend. Null only if a project removed every movement component. */
	TScriptInterface<IKCFVehicleMovement> GetVehicleMovement() const;

	/** Subobject name of the movement backend, for subclasses that substitute a different one. */
	static const FName KartMovementComponentName;

	// --- Vehicle definition ------------------------------------------------------------------------

	/**
	 * Applies a vehicle definition. Called by the game mode on the server before FinishSpawning so
	 * that components see their configuration in BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Kart")
	void SetVehicleData(UKCFVehicleDataAsset* InVehicleData);

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	UKCFVehicleDataAsset* GetVehicleData() const { return VehicleData; }

	// --- Driving ------------------------------------------------------------------------------------

	/**
	 * Pushes an input state into the kart. Called every frame by UKCFKartInputComponent on the
	 * owning client, and by nothing else. The state is sanitised before use.
	 */
	void ApplyInputState(const FKCFKartInputState& NewInput);

	/** Enables or disables driving. Server-authoritative; replicated to all clients. */
	void SetDrivingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool IsDrivingEnabled() const { return bDrivingEnabled; }

	/** Signed speed along forward, cm/s. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	float GetCurrentSpeed() const;

	/** Absolute speed as a 0..1 fraction of the kart's maximum. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	float GetSpeedPercent() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool IsDrifting() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool IsBoosting() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool IsAirborne() const;

	/** True when the kart is alive and the match allows it to act. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool CanAct() const;

	/**
	 * Resets the kart in place: upright, stationary, at the given transform.
	 * Server-authoritative. Used by respawn and by a stuck-kart reset.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Kart")
	void ResetKart(const FTransform& NewTransform);

	/** Resets the kart at its current location, used when a player is stuck or upside down. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Kart")
	void ResetKartInPlace();

	// --- Spawn protection --------------------------------------------------------------------------------

	/** Grants temporary invulnerability. Server only. */
	void ApplySpawnProtection(float DurationSeconds, bool bBreaksOnFire);

	/** Ends spawn protection early, called when the player fires if the rule set says it should. */
	void BreakSpawnProtection();

	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	bool IsSpawnProtected() const;

	// --- Firing geometry -----------------------------------------------------------------------------------
	// Resolved on the server from the kart's own mesh. A client never supplies a muzzle transform,
	// which is what stops a modified client from firing from an arbitrary position.

	/** World transform projectiles are spawned from. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	FTransform GetMuzzleTransform() const;

	/** World transform deployables are dropped from. */
	UFUNCTION(BlueprintPure, Category = "KCF|Kart")
	FTransform GetDropTransform() const;

	// --- Delegates ------------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartSimpleSignature OnDriftStarted;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartSimpleSignature OnDriftEnded;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartSimpleSignature OnBecameAirborne;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartSimpleSignature OnLanded;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartImpactSignature OnSignificantImpact;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Kart") FKCFKartSimpleSignature OnVehicleDataApplied;

	// --- Cosmetic hooks -----------------------------------------------------------------------------------------
	// Implemented in Blueprint by the project. Never called on a dedicated server.

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Drift Changed"))
	void BP_OnDriftChanged(bool bNowDrifting);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Boost Changed"))
	void BP_OnBoostChanged(bool bNowBoosting);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Impact"))
	void BP_OnImpact(FVector ImpactLocation, float ImpactSpeed);

	UFUNCTION(BlueprintImplementableEvent, Category = "KCF|Cosmetics", meta = (DisplayName = "On Cosmetic Vehicle Visuals Ready"))
	void BP_OnVehicleVisualsReady();

protected:
	// --- Components ------------------------------------------------------------------------------------------------

	/** Root collision body. Sized from the vehicle Data Asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UBoxComponent> CollisionBox;

	/** Used when the vehicle Data Asset supplies a skeletal mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<USkeletalMeshComponent> SkeletalBody;

	/** Used when the vehicle Data Asset supplies a static mesh. Cheaper, preferred on mobile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UStaticMeshComponent> StaticBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFKartCameraComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * The movement backend. Declared as the base type on purpose: a C++ subclass swaps the backend
	 * with ObjectInitializer.SetDefaultSubobjectClass<T>(AKCFKartPawn::KartMovementComponentName),
	 * which is how the optional Chaos Vehicles bridge replaces the arcade simulation without this
	 * class knowing it exists.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UPawnMovementComponent> KartMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFInventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFPowerupComponent> PowerupComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFBoostComponent> BoostComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Components")
	TObjectPtr<UKCFCombatComponent> CombatComponent;

	// --- Replicated state ---------------------------------------------------------------------------------------------

	/**
	 * The vehicle definition. Replicated as a hard object pointer with COND_InitialOnly: it is set
	 * once before the actor is ever relevant and never changes, so there is nothing to resend.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_VehicleData, BlueprintReadOnly, Category = "KCF|Kart")
	TObjectPtr<UKCFVehicleDataAsset> VehicleData;

	UPROPERTY(ReplicatedUsing = OnRep_DrivingEnabled, BlueprintReadOnly, Category = "KCF|Kart")
	bool bDrivingEnabled = true;

	/** Current input, replicated to simulated proxies purely so their wheels and lean look right. */
	UPROPERTY(Replicated)
	FKCFKartInputState ReplicatedInput;

	UFUNCTION() void OnRep_VehicleData();
	UFUNCTION() void OnRep_DrivingEnabled();

	// --- Input relay ---------------------------------------------------------------------------------------------------

	/**
	 * Sends the local player's input to the server.
	 * Unreliable by design: input is sent continuously, and a dropped packet is corrected by the
	 * next one in under 50 ms. Making it reliable would queue stale input behind a retransmit.
	 */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSendInput(FKCFKartInputState NewInput);

	/** Input actually driving the simulation on this machine. */
	FKCFKartInputState CurrentInput;

	/** Throttles how often ServerSendInput is called. */
	float TimeSinceInputSend = 0.f;

	/** Input send rate, Hz. Matched to a typical mobile client's send budget. */
	static constexpr float InputSendRate = 30.f;

	// --- Internals --------------------------------------------------------------------------------------------------------

	/** Applies CurrentInput to the movement backend and raises drift/boost cosmetic transitions. */
	void PushInputToMovement(float DeltaTime);

	/** Loads and applies meshes, materials and collision extents from the vehicle Data Asset. */
	/**
	 * Loads meshes and materials from the vehicle definition. Skipped entirely on a dedicated
	 * server. Virtual because a physics-driven backend needs its mesh on the server too, where the
	 * physics asset is part of the simulation rather than something to look at.
	 */
	virtual void ApplyVehicleVisuals();

	/** Server-side: converts a physical impact into a damage request. */
	void HandleCollisionDamage(AActor* Other, const FVector& HitLocation, const FVector& HitNormal, float ImpactSpeed);

	/** Bound to the health component's death delegate. */
	UFUNCTION()
	void HandleDeath(AActor* Victim, AActor* Killer, const FKCFDamageInfo& FatalDamage);

	/** Tracks drift/airborne edges so the cosmetic delegates fire once per transition, not per frame. */
	bool bWasDrifting = false;
	bool bWasAirborne = false;
	bool bWasBoosting = false;

	/** Spawn protection ends the first time this kart fires, when the rule set says it should. */
	bool bSpawnProtectionBreaksOnFire = false;

	/** Server time of the last collision damage, used to rate-limit repeated scrapes. */
	float LastCollisionDamageTime = -100.f;

	/** Minimum seconds between two collision damage events against the same kart. */
	static constexpr float CollisionDamageCooldown = 0.5f;

public:
	//~ Begin AActor
	/**
	 * The pawn ticks. It is one of the three ticking classes in the framework and the comment on
	 * Tick explains the per-frame work it does; everything that can be an event or a timer is.
	 */
	virtual void Tick(float DeltaTime) override;
	//~ End AActor
};
