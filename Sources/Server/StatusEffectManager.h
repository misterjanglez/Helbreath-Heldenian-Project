#pragma once
#include <cstdint>

#include "MarqueeEffects.h"

class CGame;

class StatusEffectManager
{
public:
	StatusEffectManager() = default;
	~StatusEffectManager() = default;
	void set_game(CGame* game) { m_game = game; }

	// Status effect flags
	void set_hero_flag(short owner_h, char owner_type, bool status);
	void set_berserk_flag(short owner_h, char owner_type, bool status);
	void set_haste_flag(short owner_h, char owner_type, bool status);
	void set_poison_flag(short owner_h, char owner_type, bool status);
	void set_defense_shield_flag(short owner_h, char owner_type, bool status);
	void set_magic_protection_flag(short owner_h, char owner_type, bool status);
	void set_protection_from_arrow_flag(short owner_h, char owner_type, bool status);
	void set_illusion_movement_flag(short owner_h, char owner_type, bool status);
	void set_illusion_flag(short owner_h, char owner_type, bool status);
	void set_ice_flag(short owner_h, char owner_type, bool status);
	void set_invisibility_flag(short owner_h, char owner_type, bool status);
	void set_inhibition_casting_flag(short owner_h, char owner_type, bool status);
	void set_angel_flag(short owner_h, char owner_type, int status, int temp);

	// Farming exploit detection
	void check_farming_action(short attacker_h, short target_h, bool type);

	// --- Item Tiers Marquee debuffs (PLANS/ItemTiers_Plan.md §5) ------------
	// The timing and bookkeeping half; the bleed tick's damage belongs to
	// CombatManager::bleed_effect, mirroring how poison is split.

	// Applies (or refreshes) the flat defense-ratio debuff on the victim.
	void apply_sunder(short target_h, char target_type, uint32_t now);

	// Applies (or refreshes) the bleed. `attacker_h` is credited if a tick
	// lands the killing blow; only players can carry a Bleed weapon, so the
	// source is always a player handle.
	void apply_bleed(short target_h, char target_type, int attacker_h, uint32_t now);

	// The victim's active defense-ratio delta (a negative number), or 0. Read
	// at hit resolution rather than written into the victim's stat block, so
	// expiry needs no timer and nothing can leak into a saved character.
	int sunder_defense_delta(short target_h, char target_type, uint32_t now);

	// Advances one victim's bleed: damages on period, expires on duration.
	// Driven per-client by RegenManager and per-NPC by CEntityManager.
	void tick_bleed(short target_h, char target_type, uint32_t now);

	// Drops every marquee debuff (death, respawn, entity reuse).
	void clear_marquee(short owner_h, char owner_type);

private:
	CGame* m_game = nullptr;

	// The victim's debuff block, or nullptr when the handle names nothing.
	hb::server::marquee_debuffs* marquee_state(short owner_h, char owner_type);
};
