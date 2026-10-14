// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native gameplay tags owned by the framework.
 *
 * Only tags that C++ itself compares against are declared natively. Content-defined tags (specific
 * weapons, specific powerups, project cosmetics) belong in Config/Tags/*.ini so that adding one does
 * not require a recompile. The shipped ini lives at
 * Plugins/KartCombatFramework/Config/Tags/KartCombatFrameworkTags.ini.
 */

enum class EKCFDamageCategory : uint8;

namespace KCFGameplayTags
{
	// Kart state ------------------------------------------------------------------------------
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Alive);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Dead);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Boosting);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Drifting);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Airborne);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_SpawnProtected);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Shielded);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kart_State_Disabled);

	// Damage ----------------------------------------------------------------------------------
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Direct);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Projectile);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Explosion);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Collision);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Environmental);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Trap);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Melee);

	// Immunity --------------------------------------------------------------------------------
	/** Blanket immunity. Checked by the damage pipeline before anything else. */
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Immunity_All);
	/** Immune to control-loss effects (EMP, spin-out). */
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Immunity_Control);
	/** Immune to knockback impulses but not to damage. */
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Immunity_Knockback);

	// Event tags, used as keys for the cosmetic hook delegate -----------------------------------
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Boost_Started);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Boost_Ended);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Drift_Started);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Drift_Ended);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Weapon_Fired);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Weapon_Empty);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Powerup_Collected);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Powerup_Used);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kart_Collision);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kart_Damaged);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kart_Died);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Kart_Respawned);

	// Spawn point classification ----------------------------------------------------------------
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Spawn_Any);
	KARTCOMBATFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Spawn_Powerup);

	/** Maps an EKCFDamageCategory onto its canonical native tag. */
	KARTCOMBATFRAMEWORK_API FGameplayTag DamageCategoryToTag(EKCFDamageCategory Category);
}
