// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/KCFTypes.h"
#include "KCFKartInputComponent.generated.h"

class AKCFKartPawn;
class AKCFPlayerController;
class UEnhancedInputComponent;
class UInputAction;
class UInputComponent;
class UKCFInputConfigDataAsset;
class UKCFMobileSettingsDataAsset;
struct FInputActionValue;

/**
 * Collects driving intent from every input source and reduces it to one FKCFKartInputState.
 *
 * Two sources feed the same struct. Enhanced Input drives it on desktop and console; a project's
 * touch UI drives it through the Set Mobile* functions below. Neither knows the other exists, and
 * nothing downstream can tell which was used - the kart reads one struct either way.
 *
 * That indirection is the whole reason the mobile API is a set of plain Blueprint-callable
 * functions rather than a widget class. The plugin must never contain the project's HUD, and the
 * project's HUD must never need to know how the movement component wants its data.
 *
 * Lives on the player controller rather than the pawn, so that a player who is dead and waiting to
 * respawn still has somewhere for their input and their control settings to live.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFKartInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKCFKartInputComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/**
	 * Ticks only on a machine with a local player, and only while a kart is possessed. The per-frame
	 * work is one struct assembly and one call into the pawn; everything that could be an event
	 * (fire, boost, slot changes) already is one.
	 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Loads configuration and installs the mapping context. Called on the owning client. */
	void InitialiseLocalInput();

	/** Binds Enhanced Input actions. Called from the controller's SetupInputComponent. */
	void BindInputActions(UInputComponent* PlayerInputComponent);

	/** Tells the component which kart to drive. Null when the player has no pawn. */
	void SetControlledKart(AKCFKartPawn* InKart);

	UFUNCTION(BlueprintPure, Category = "KCF|Input")
	AKCFKartPawn* GetControlledKart() const { return ControlledKart.Get(); }

	/** The state assembled for this frame. */
	UFUNCTION(BlueprintPure, Category = "KCF|Input")
	const FKCFKartInputState& GetInputState() const { return InputState; }

	/** Zeroes every channel. Used when the match freezes input or the player dies. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input")
	void ClearInput();

	// --- Mobile / touch API -----------------------------------------------------------------------
	// Every one of these is safe to call from a Blueprint widget on any platform, and they are the
	// supported way for a project's touch UI to drive the kart.

	/**
	 * Raw steering axis from a virtual stick or wheel, -1..+1.
	 * The dead zone, response curve and sensitivity from the mobile settings asset are applied here,
	 * so a widget sends an unprocessed value and all of the feel lives in data.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile")
	void SetMobileSteering(float AxisValue);

	/** Throttle, 0..1. Overridden while auto-acceleration is enabled. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile")
	void SetMobileThrottle(float AxisValue);

	/** Combined 2D stick: X steers, positive Y drives, negative Y brakes and then reverses. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile")
	void SetMobileMovement(FVector2D AxisValues);

	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StartMobileBrake();
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StopMobileBrake();
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StartDrift();
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StopDrift();
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StartBoost();
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void StopBoost();

	/** Fires the selected weapon, through the combat component's validation. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void FireWeapon();

	/** Uses the selected powerup. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void UsePowerup();

	/** Fires or uses whatever is selected, for a single-action-button layout. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void UseSelectedSlot();

	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void CycleSlot();

	/** Camera look delta in degrees. Cosmetic: it moves the camera and nothing else. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void AddLookInput(FVector2D Delta);

	/** Recentres the camera behind the kart. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void RecenterCamera();

	/** Asks the server for a stuck-kart reset. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input|Mobile") void RequestKartReset();

	// --- Configuration ----------------------------------------------------------------------------

	/** Replaces the mobile tuning at runtime, used when a player changes their control settings. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Input")
	void SetMobileSettings(UKCFMobileSettingsDataAsset* InSettings);

	UFUNCTION(BlueprintPure, Category = "KCF|Input")
	UKCFMobileSettingsDataAsset* GetMobileSettings() const { return MobileSettings; }

	/** True when throttle is applied automatically and a touch layout only needs a brake button. */
	UFUNCTION(BlueprintPure, Category = "KCF|Input")
	bool IsAutoAccelerationEnabled() const;

	/** Input configuration. A project sets this on its player controller Blueprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|Input")
	TSoftObjectPtr<UKCFInputConfigDataAsset> InputConfig;

protected:
	/** The state handed to the kart. Local to this machine; the pawn relays it to the server. */
	FKCFKartInputState InputState;

	UPROPERTY(Transient)
	TObjectPtr<UKCFInputConfigDataAsset> LoadedConfig;

	UPROPERTY(Transient)
	TObjectPtr<UKCFMobileSettingsDataAsset> MobileSettings;

	UPROPERTY(Transient)
	TWeakObjectPtr<AKCFKartPawn> ControlledKart;

	/** Raw steering before dead zone and curve, kept separately so processing happens exactly once. */
	float RawSteeringAxis = 0.f;

	/** Look delta accumulated since the last frame, consumed and cleared each tick. */
	FVector2D PendingLook = FVector2D::ZeroVector;

	/** True while a touch brake button is held, so a release reliably clears the brake. */
	bool bMobileBrakeHeld = false;

	/** Set while the fire button is held, used for repeat-fire on touch layouts. */
	bool bFireHeld = false;

	AKCFPlayerController* GetOwningController() const;

	/** Loads the configuration asset and mobile settings. Safe to call more than once. */
	void ResolveConfiguration();

	/** Applies the steering curve and auto-acceleration, then clamps everything. */
	void FinaliseInputForFrame();

	/** Enables the tick only when there is both a local player and a kart to drive. */
	void UpdateTickEnabled();

	// --- Enhanced Input handlers ---------------------------------------------------------------------

	void HandleMove(const FInputActionValue& Value);
	void HandleSteer(const FInputActionValue& Value);
	void HandleThrottle(const FInputActionValue& Value);
	void HandleBrake(const FInputActionValue& Value);
	void HandleDriftStarted(const FInputActionValue& Value);
	void HandleDriftCompleted(const FInputActionValue& Value);
	void HandleBoostStarted(const FInputActionValue& Value);
	void HandleBoostCompleted(const FInputActionValue& Value);
	void HandleFireStarted(const FInputActionValue& Value);
	void HandleFireCompleted(const FInputActionValue& Value);
	void HandleUsePowerup(const FInputActionValue& Value);
	void HandleCycleSlot(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleRecenterCamera(const FInputActionValue& Value);
	void HandleResetKart(const FInputActionValue& Value);
	void HandlePause(const FInputActionValue& Value);
};
