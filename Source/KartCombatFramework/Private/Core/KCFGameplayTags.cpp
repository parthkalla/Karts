// Original work, MIT licensed.

#include "Core/KCFGameplayTags.h"

#include "Core/KCFTypes.h"

namespace KCFGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Alive, "KCF.Kart.State.Alive", "The kart is alive and can act.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Dead, "KCF.Kart.State.Dead", "The kart is destroyed and awaiting respawn.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Boosting, "KCF.Kart.State.Boosting", "A boost is currently active.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Drifting, "KCF.Kart.State.Drifting", "The kart is drifting.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Airborne, "KCF.Kart.State.Airborne", "The kart has no ground contact.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_SpawnProtected, "KCF.Kart.State.SpawnProtected", "Post-respawn invulnerability is active.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Shielded, "KCF.Kart.State.Shielded", "A shield powerup is absorbing damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Kart_State_Disabled, "KCF.Kart.State.Disabled", "Control is temporarily suppressed, e.g. by an EMP.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Direct, "KCF.Damage.Direct", "Damage applied directly with no delivery medium.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Projectile, "KCF.Damage.Projectile", "Damage from a projectile impact.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Explosion, "KCF.Damage.Explosion", "Radial damage from an explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Collision, "KCF.Damage.Collision", "Damage from a kart-to-kart or kart-to-world impact.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Environmental, "KCF.Damage.Environmental", "Damage from the arena itself, e.g. a kill volume.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Trap, "KCF.Damage.Trap", "Damage from a placed trap or mine.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Melee, "KCF.Damage.Melee", "Damage from a close-range melee effect.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Immunity_All, "KCF.Immunity.All", "Immune to all incoming damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Immunity_Control, "KCF.Immunity.Control", "Immune to control-loss effects.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Immunity_Knockback, "KCF.Immunity.Knockback", "Immune to knockback impulses.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Boost_Started, "KCF.Event.Boost.Started", "Cosmetic hook: boost began.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Boost_Ended, "KCF.Event.Boost.Ended", "Cosmetic hook: boost ended.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Drift_Started, "KCF.Event.Drift.Started", "Cosmetic hook: drift began.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Drift_Ended, "KCF.Event.Drift.Ended", "Cosmetic hook: drift ended.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Weapon_Fired, "KCF.Event.Weapon.Fired", "Cosmetic hook: a weapon was fired.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Weapon_Empty, "KCF.Event.Weapon.Empty", "Cosmetic hook: fire attempted with no ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Powerup_Collected, "KCF.Event.Powerup.Collected", "Cosmetic hook: a powerup was picked up.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Powerup_Used, "KCF.Event.Powerup.Used", "Cosmetic hook: a powerup was consumed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kart_Collision, "KCF.Event.Kart.Collision", "Cosmetic hook: a significant impact occurred.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kart_Damaged, "KCF.Event.Kart.Damaged", "Cosmetic hook: the kart took damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kart_Died, "KCF.Event.Kart.Died", "Cosmetic hook: the kart was destroyed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Kart_Respawned, "KCF.Event.Kart.Respawned", "Cosmetic hook: the kart respawned.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Spawn_Any, "KCF.Spawn.Any", "A spawn point usable by any team.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Spawn_Powerup, "KCF.Spawn.Powerup", "A spawn point reserved for powerup spawners.");

	FGameplayTag DamageCategoryToTag(EKCFDamageCategory Category)
	{
		switch (Category)
		{
		case EKCFDamageCategory::Projectile:		return Damage_Projectile;
		case EKCFDamageCategory::Explosion:			return Damage_Explosion;
		case EKCFDamageCategory::Collision:			return Damage_Collision;
		case EKCFDamageCategory::Environmental:		return Damage_Environmental;
		case EKCFDamageCategory::Trap:				return Damage_Trap;
		case EKCFDamageCategory::Melee:				return Damage_Melee;
		case EKCFDamageCategory::Direct:
		default:									return Damage_Direct;
		}
	}
}
