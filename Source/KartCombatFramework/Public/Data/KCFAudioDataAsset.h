// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Data/KCFDataAssetBase.h"
#include "KCFAudioDataAsset.generated.h"

class USoundBase;

/**
 * Audio references for one kart.
 *
 * The framework never plays any of these. It raises cosmetic events (see UKCFKartPawn's
 * BlueprintImplementableEvent hooks) and a project Blueprint decides what to play, which keeps all
 * audio code out of dedicated-server code paths and lets a project use its own audio middleware.
 * This asset exists so that the references are still authored as data rather than hard-coded in a
 * Blueprint graph.
 */
UCLASS(BlueprintType)
class KARTCOMBATFRAMEWORK_API UKCFAudioDataAsset : public UKCFDataAssetBase
{
	GENERATED_BODY()

public:
	/** Looping engine tone. A project drives its pitch from GetSpeedPercent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Engine")
	TSoftObjectPtr<USoundBase> EngineLoop;

	/** Pitch multiplier at standstill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float EngineIdlePitch = 0.8f;

	/** Pitch multiplier at top speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Engine", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float EngineMaxPitch = 2.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<USoundBase> TireSkidLoop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<USoundBase> BoostStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<USoundBase> BoostLoop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Driving")
	TSoftObjectPtr<USoundBase> Landing;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact")
	TSoftObjectPtr<USoundBase> LightImpact;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact")
	TSoftObjectPtr<USoundBase> HeavyImpact;

	/** Impact speed above which HeavyImpact is used instead of LightImpact, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0", Units = "cm/s"))
	float HeavyImpactSpeedThreshold = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<USoundBase> DamageTaken;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<USoundBase> ShieldBreak;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<USoundBase> Destruction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<USoundBase> Respawn;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<USoundBase> PowerupCollected;

	//~ Begin UKCFDataAssetBase
	virtual bool ValidateConfiguration(TArray<FText>& OutErrors) const override;
protected:
	virtual FName GetPrimaryAssetType() const override { return TEXT("KCFAudio"); }
	//~ End UKCFDataAssetBase
};
