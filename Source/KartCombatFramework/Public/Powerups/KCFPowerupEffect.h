// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "UObject/Object.h"
#include "KCFPowerupEffect.generated.h"

class AKCFKartPawn;
class UKCFPowerupComponent;

/**
 * One applied powerup behaviour.
 *
 * Effects are server-side objects. They are instantiated when a powerup activates, live on the
 * owning UKCFPowerupComponent for their duration, and are told to undo themselves when they expire.
 * Clients never create one; they learn that an effect is active from the replicated tag container
 * on the component, which is all a HUD or a material needs.
 *
 * Subclass this in C++ or Blueprint. The three overridable entry points are deliberately narrow:
 * apply, revert, and an optional periodic tick that the component schedules on a timer rather than
 * per frame.
 */
UCLASS(Blueprintable, BlueprintType, Abstract, EditInlineNew, DefaultToInstanced)
class KARTCOMBATFRAMEWORK_API UKCFPowerupEffect : public UObject
{
	GENERATED_BODY()

public:
	//~ Begin UObject
	virtual UWorld* GetWorld() const override;
	//~ End UObject

	/** Called once when the effect becomes active. Server only. */
	UFUNCTION(BlueprintNativeEvent, Category = "KCF|Powerup")
	void ApplyEffect(AKCFKartPawn* Target, float Magnitude);
	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude);

	/**
	 * Called once when the effect ends - on expiry, on death, or when the match ends.
	 * Must undo everything ApplyEffect did; the component guarantees it is called exactly once.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "KCF|Powerup")
	void RevertEffect(AKCFKartPawn* Target);
	virtual void RevertEffect_Implementation(AKCFKartPawn* Target);

	/**
	 * Optional periodic update, scheduled on a timer at PeriodSeconds. Returning a period of 0
	 * (the default) means the effect never ticks at all, which is the right answer for almost every
	 * effect: a speed boost sets a multiplier once and does nothing until it expires.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "KCF|Powerup")
	void TickEffect(AKCFKartPawn* Target, float DeltaSeconds);
	virtual void TickEffect_Implementation(AKCFKartPawn* Target, float DeltaSeconds) {}

	/** Seconds between TickEffect calls. 0 disables periodic updates entirely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Powerup", meta = (ClampMin = "0.0", Units = "s"))
	float PeriodSeconds = 0.f;

	/** Set by the component before ApplyEffect so an effect can reach its owner. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Powerup")
	TWeakObjectPtr<UKCFPowerupComponent> OwningComponent;

	/** Id of the powerup definition that produced this effect, for UI and logging. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Powerup")
	FName SourcePowerupId;
};

/** Multiplies the kart's top speed and acceleration for the duration. Magnitude is the multiplier. */
UCLASS(BlueprintType, meta = (DisplayName = "Speed Boost Effect"))
class KARTCOMBATFRAMEWORK_API UKCFSpeedBoostEffect : public UKCFPowerupEffect
{
	GENERATED_BODY()

public:
	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude) override;
	virtual void RevertEffect_Implementation(AKCFKartPawn* Target) override;

protected:
	/** Remembered so revert restores the previous value rather than assuming it was 1. */
	float PreviousMultiplier = 1.f;
};

/** Grants shield points. Magnitude is the shield amount added, clamped to the kart's maximum. */
UCLASS(BlueprintType, meta = (DisplayName = "Shield Effect"))
class KARTCOMBATFRAMEWORK_API UKCFShieldEffect : public UKCFPowerupEffect
{
	GENERATED_BODY()

public:
	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude) override;
	virtual void RevertEffect_Implementation(AKCFKartPawn* Target) override;

protected:
	/**
	 * Whether the shield should be stripped when the effect expires.
	 * A timed bubble shield wants this; a shield pickup that simply tops you up does not, because
	 * taking away points the player has already "banked" feels like a bug.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Powerup")
	bool bRemoveShieldOnExpiry = true;

	float GrantedShield = 0.f;
};

/** Restores health immediately. Magnitude is the health restored. Instant: nothing to revert. */
UCLASS(BlueprintType, meta = (DisplayName = "Repair Effect"))
class KARTCOMBATFRAMEWORK_API UKCFRepairEffect : public UKCFPowerupEffect
{
	GENERATED_BODY()

public:
	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude) override;
};

/** Makes the kart immune to damage for the duration. Magnitude is unused. */
UCLASS(BlueprintType, meta = (DisplayName = "Invulnerability Effect"))
class KARTCOMBATFRAMEWORK_API UKCFInvulnerabilityEffect : public UKCFPowerupEffect
{
	GENERATED_BODY()

public:
	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude) override;
	virtual void RevertEffect_Implementation(AKCFKartPawn* Target) override;

protected:
	bool bAppliedImmunity = false;
};

/** Refills boost charges and keeps them refilling for the duration. Magnitude is charges per tick. */
UCLASS(BlueprintType, meta = (DisplayName = "Nitro Effect"))
class KARTCOMBATFRAMEWORK_API UKCFNitroEffect : public UKCFPowerupEffect
{
	GENERATED_BODY()

public:
	UKCFNitroEffect();

	virtual void ApplyEffect_Implementation(AKCFKartPawn* Target, float Magnitude) override;
	virtual void TickEffect_Implementation(AKCFKartPawn* Target, float DeltaSeconds) override;
};
