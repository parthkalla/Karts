// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "KCFKartCameraComponent.generated.h"

class UCameraComponent;
class UKCFVehicleDataAsset;

/**
 * Third-person chase camera for a kart.
 *
 * Extends USpringArmComponent rather than wrapping it, because collision handling, lag and socket
 * offsets are exactly what a spring arm already does well; reimplementing them would be duplicated
 * logic with worse edge cases. What this class adds is the kart-specific feel: speed-driven field of
 * view, a boost punch, a drift yaw offset, and an auto-centre behaviour for touch play.
 *
 * Ticks, but only on a machine with a local viewer. On a dedicated server the component disables its
 * own tick in BeginPlay and never touches a camera, which is checked by the dedicated-server audit
 * in Documentation/DedicatedServer.md.
 */
UCLASS(ClassGroup = "KartCombatFramework", meta = (BlueprintSpawnableComponent))
class KARTCOMBATFRAMEWORK_API UKCFKartCameraComponent : public USpringArmComponent
{
	GENERATED_BODY()

public:
	UKCFKartCameraComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** The camera whose FOV this component drives. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Camera")
	void SetManagedCamera(UCameraComponent* InCamera);

	/** Pulls distance, offset and FOV range from a vehicle definition. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Camera")
	void ApplyVehicleData(const UKCFVehicleDataAsset* InVehicleData);

	/** Adds a look delta in degrees. Called by the input layer; cosmetic only. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Camera")
	void AddLookInput(const FVector2D& LookDelta);

	/** Re-centres the camera behind the kart immediately, e.g. after a respawn. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Camera")
	void SnapBehindKart();

	/** Seconds without look input before the camera drifts back behind the kart. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Camera", meta = (ClampMin = "0.0"))
	float AutoCenterDelay = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Camera", meta = (ClampMin = "0.0"))
	float AutoCenterSpeed = 3.f;

	/** Pitch is clamped to this range so the player cannot look inside the kart or straight up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Camera")
	FVector2D PitchLimits = FVector2D(-45.f, 20.f);

	/** How quickly the FOV follows its target, units/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Camera", meta = (ClampMin = "0.1"))
	float FOVInterpSpeed = 4.f;

	/** Degrees of camera yaw added in the direction of a drift, for a sense of rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Camera", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float DriftYawInfluence = 12.f;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> ManagedCamera;

	float BaseFOV = 85.f;
	float MaxSpeedFOV = 100.f;
	float BoostFOVBonus = 8.f;

	/** Player-applied yaw/pitch relative to the kart. */
	FVector2D LookOffset = FVector2D::ZeroVector;

	/** Seconds since the player last supplied look input. */
	float TimeSinceLookInput = 0.f;

	/** Smoothed drift-driven yaw, kept separate from LookOffset so the two do not fight. */
	float CurrentDriftYaw = 0.f;

	/** True when this machine has a local viewer and should run camera logic at all. */
	bool bHasLocalViewer = false;
};
