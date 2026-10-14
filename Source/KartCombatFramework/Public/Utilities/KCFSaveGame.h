// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/SaveGame.h"
#include "KCFSaveGame.generated.h"

/**
 * Local player preferences.
 *
 * Deliberately contains nothing competitive. Every field here is a preference the player is free to
 * set however they like - sensitivity, which kart they last picked, whether haptics are on. There
 * is no currency, no unlock state, no statistics, because a value a player can edit in a file on
 * their own device must never be a value the match trusts.
 *
 * The cosmetic/vehicle selection stored here is a *request*: the server validates the id against
 * its catalogue before honouring it. Editing this file to name a kart you have not unlocked gets
 * you the fallback kart, not the kart.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bumped when the layout changes so that an old save can be migrated or discarded. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Save")
	int32 SaveVersion = 1;

	// --- Controls ------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	EKCFSteeringMode SteeringMode = EKCFSteeringMode::Direct;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SteeringDeadzone = 0.08f;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SteeringSensitivity = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float LookSensitivity = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	bool bInvertLookY = false;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	bool bAutoAccelerate = true;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	bool bDriftIsToggle = false;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	bool bBoostIsToggle = true;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls")
	bool bHapticsEnabled = true;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Controls", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HapticIntensity = 0.6f;

	// --- Audio -----------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume = 0.7f;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SfxVolume = 1.f;

	// --- Display ----------------------------------------------------------------------------------

	/** Project-defined quality bucket, 0 = lowest. The framework stores it and nothing more. */
	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Display", meta = (ClampMin = "0", ClampMax = "4"))
	int32 QualityLevel = 2;

	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Display", meta = (ClampMin = "30", ClampMax = "240"))
	int32 FrameRateLimit = 60;

	// --- Selection ----------------------------------------------------------------------------------

	/** Last chosen kart. A request, re-validated by the server on every spawn. */
	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Selection")
	FName SelectedVehicleId;

	/** Display name the player last used. */
	UPROPERTY(BlueprintReadWrite, Category = "KCF|Save|Selection")
	FString PlayerDisplayName;
};
