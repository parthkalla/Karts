// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KCFPlayZone.generated.h"

class AKCFKartPawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFZonePhaseSignature, int32, PhaseIndex, float, SecondsUntilShrink);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKCFZoneChangedSignature);

/** One step of the zone's schedule: how long it holds, how long it takes to close, and to what. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFPlayZonePhase
{
	GENERATED_BODY()

	/** Seconds the zone holds at its current size before this phase starts closing it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "0.0", Units = "s"))
	float HoldSeconds = 30.f;

	/** Seconds the zone takes to reach TargetRadius once it starts closing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "1.0", Units = "s"))
	float ShrinkSeconds = 25.f;

	/** Radius at the end of this phase, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "100.0", Units = "cm"))
	float TargetRadius = 4000.f;

	/** Damage per second dealt to a kart outside the zone during and after this phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "0.0"))
	float DamagePerSecond = 5.f;
};

/**
 * A closing play area.
 *
 * Shrinks on a schedule and damages anything outside it, which is what forces a battle royale to
 * end rather than turning into two players hiding in opposite corners.
 *
 * Authority model: the server owns the schedule, the radius and the damage. Clients receive the
 * centre, the current radius and the next target so the HUD can draw a minimap ring and a warning,
 * and that is all they receive - the damage itself is applied through UKCFDamageSubsystem on the
 * server like every other damage source in the framework.
 *
 * The zone does not tick. Shrinking is interpolated from replicated start and end timestamps, so a
 * client that joins mid-phase computes the same radius the server has without a single update in
 * between, and the damage pass runs on a slow repeating timer rather than per frame.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPlayZone : public AActor
{
	GENERATED_BODY()

public:
	AKCFPlayZone();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	/** Fires on every machine when a new phase begins. HUDs bind to this for the warning banner. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Zone")
	FKCFZonePhaseSignature OnPhaseStarted;

	UPROPERTY(BlueprintAssignable, Category = "KCF|Zone")
	FKCFZoneChangedSignature OnZoneChanged;

	// --- Queries (identical on server and client) --------------------------------------------------

	/** Radius right now, interpolated from the replicated phase timestamps. */
	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	float GetCurrentRadius() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	FVector GetZoneCenter() const { return ZoneCenter; }

	/** Radius this phase is closing to. Equal to the current radius while the zone is holding. */
	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	float GetTargetRadius() const { return TargetRadius; }

	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	int32 GetCurrentPhase() const { return CurrentPhase; }

	/** Seconds until the zone starts closing. Zero while it is already closing. */
	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	float GetSecondsUntilShrink() const;

	/** True while the radius is actively changing. */
	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	bool IsShrinking() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	bool IsInsideZone(const FVector& Location) const;

	/** Damage per second currently applied outside the zone. */
	UFUNCTION(BlueprintPure, Category = "KCF|Zone")
	float GetCurrentDamagePerSecond() const;

	// --- Server-only control -------------------------------------------------------------------------

	/** Starts the schedule from the first phase. Called by the game mode when the match goes live. */
	void StartZone(const FVector& InCenter, float InStartRadius);

	/** Stops shrinking and stops damaging. Called when the match ends. */
	void StopZone();

	/** The schedule. Each phase holds, then closes to its target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone")
	TArray<FKCFPlayZonePhase> Phases;

	/** Seconds between damage passes on karts outside the zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "0.1", ClampMax = "5.0", Units = "s"))
	float DamageInterval = 1.f;

	/**
	 * How far the next centre may drift from the current one, as a fraction of the shrink in
	 * radius. Zero keeps the zone concentric; 1 lets it move as far as it can without exposing
	 * ground that was already outside.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "KCF|Zone", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CenterDriftFraction = 0.6f;

protected:
	/**
	 * Replicated to everyone: the ring is drawn on every client's HUD, so this is public
	 * information by design rather than something to restrict.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	FVector ZoneCenter = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	float StartRadius = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	float TargetRadius = 0.f;

	/** Server time the current phase's hold ends and the shrink begins. */
	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	float ShrinkStartServerTime = -1.f;

	/** Server time the shrink completes. */
	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	float ShrinkEndServerTime = -1.f;

	UPROPERTY(ReplicatedUsing = OnRep_ZoneState, BlueprintReadOnly, Category = "KCF|Zone")
	int32 CurrentPhase = INDEX_NONE;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Zone")
	bool bZoneActive = false;

	FTimerHandle PhaseTimerHandle;
	FTimerHandle DamageTimerHandle;

	/** Server: begins the phase at PhaseIndex, or stops the schedule when there are none left. */
	void EnterPhase(int32 PhaseIndex);

	/** Server: runs on DamageInterval and damages every kart outside the zone. */
	void ApplyZoneDamage();

	/** Picks the next centre, keeping the new circle inside the old one. */
	FVector ChooseNextCenter(float NewRadius) const;

	/** Synchronised time, so client and server agree on the radius without extra replication. */
	float GetServerTime() const;

	UFUNCTION() void OnRep_ZoneState();
};
