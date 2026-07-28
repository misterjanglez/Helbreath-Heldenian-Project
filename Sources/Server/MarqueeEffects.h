// MarqueeEffects.h: per-entity Marquee state (Item Tiers 3-D)
//
// The Legendary MARQUEE bucket splits cleanly in two: what a weapon GRANTS its
// wielder, and what a victim is SUFFERING. Both get a POD here, so adding a
// Marquee line never means growing a hand-maintained list of reset statements.
//
// The debuff half is carried by CClient and CNpc alike — players and NPCs are
// both legal victims — the way PlayerStatus/EntityStatus already are. All of
// it is server-side: v1 adds no broadcast bits for these, so victims learn
// about them from notify lines only.
//
// Design contract: PLANS/ItemTiers_Plan.md §5; plan cycle 3-D.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "NetConstants.h"

#include <cstdint>

namespace hb::server
{

// The Marquee lines the equipped weapon or wand grants, in display units and
// already clamped at their aggregate caps by calc_total_item_effect. The two
// speed lines are absent on purpose: they ride PlayerStatus so the client gets
// them for free (Cycle 4-C).
struct marquee_weapon_lines
{
	int sunder_pct = 0;      // proc chance per landed hit
	int bleed_pct = 0;       // proc chance per landed hit
	int mp_drain = 0;        // flat, always-on per landed hit
	int sp_drain = 0;

	void clear() { *this = marquee_weapon_lines{}; }
};

// What a victim is carrying: the two timed weapon exotics. Both refresh on
// re-proc; neither is resistable or curable in v1. Bleed deliberately shares
// no state with poison, so a victim can carry both at once.
struct marquee_debuffs
{
	// --- Sunder: a flat defense-ratio delta while it lasts. The delta is
	// stored rather than assumed so a live `reload tiers` cannot leave an
	// active debuff reading a constant it was not applied with.
	int      sunder_delta = 0;
	uint32_t sunder_expire_time = 0;

	// --- Bleed: fixed damage on a fixed period.
	int      bleed_damage = 0;
	uint32_t bleed_interval_ms = 0;
	uint32_t bleed_expire_time = 0;
	uint32_t bleed_next_tick_time = 0;

	// The source, for kill credit on a lethal tick. Only an equipped weapon can
	// carry a Bleed line, and only players equip, so the attacker is always a
	// player. Handles are recycled, so the name is stored beside the handle and
	// both must still agree when the tick lands — eight seconds is long enough
	// for a reconnect to inherit the handle and be blamed for the kill.
	int      bleed_attacker_h = 0;
	char     bleed_attacker_name[hb::shared::limits::CharNameLen] = {};

	// --- MP/SP drain notify throttle. Drains are per-landed-hit and always-on,
	// so an unthrottled line would flood the notice channel during melee.
	uint32_t drain_notify_time = 0;

	bool is_sundered(uint32_t now) const
	{
		return (sunder_expire_time != 0) && (static_cast<int32_t>(sunder_expire_time - now) > 0);
	}

	bool is_bleeding(uint32_t now) const
	{
		return (bleed_expire_time != 0) && (static_cast<int32_t>(bleed_expire_time - now) > 0);
	}

	void clear() { *this = marquee_debuffs{}; }
};

} // namespace hb::server
