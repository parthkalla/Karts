// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Match/KCFGameModes.h"
#include "KCFBattleRoyaleGameMode.generated.h"

class AKCFPlayZone;

/**
 * Last kart standing, in a closing arena.
 *
 * Built on the elimination mode, because "last side standing" is already its win condition and
 * re-implementing it here would mean two copies of the same rule. What this class adds is the
 * closing play zone: the pressure that stops the final two players from sitting in opposite
 * corners waiting each other out.
 *
 * The zone is an AKCFPlayZone the mode either finds in the level or spawns. Placing one by hand is
 * the better workflow - a designer can see the opening circle against the arena - but spawning one
 * means the mode still works on a level that was built for deathmatch.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "KCF Battle Royale"))
class KARTCOMBATFRAMEWORK_API AKCFBattleRoyaleGameMode : public AKCFEliminationGameMode
{
	GENERATED_BODY()

public:
	AKCFBattleRoyaleGameMode();

	//~ Begin AActor
	virtual void BeginPlay() override;
	//~ End AActor

	//~ Begin AKCFGameMode
	virtual void OnMatchStateChanged(EKCFMatchState OldState, EKCFMatchState NewState) override;
	virtual bool GetRespawnTransformOverride(AController* Controller, FTransform& OutTransform) const override;
	//~ End AKCFGameMode

	/** The active zone. Null on a level with no zone and no ZoneClass set. */
	UFUNCTION(BlueprintPure, Category = "KCF|BattleRoyale")
	AKCFPlayZone* GetPlayZone() const { return PlayZone; }

	/** Spawned when the level contains no AKCFPlayZone. Leave unset to require a placed one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|BattleRoyale")
	TSubclassOf<AKCFPlayZone> ZoneClass;

	/** Opening radius of a spawned zone, cm. Ignored when the level supplies its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|BattleRoyale", meta = (ClampMin = "500.0", Units = "cm"))
	float InitialZoneRadius = 20000.f;

	/**
	 * A respawning player is placed inside the zone rather than at whichever spawn point scored
	 * best. Only relevant when the rule set grants more than one life; the mode's default is one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "KCF|BattleRoyale")
	bool bRespawnInsideZone = true;

protected:
	UPROPERTY(Transient)
	TObjectPtr<AKCFPlayZone> PlayZone;

	/** Finds a placed zone, or spawns one from ZoneClass. Returns null when neither is available. */
	AKCFPlayZone* ResolvePlayZone();

	/** Centre of the opening circle: the average of the level's spawn points. */
	FVector ComputeArenaCenter() const;
};
