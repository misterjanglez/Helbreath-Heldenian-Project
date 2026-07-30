#pragma once

// What the equipped set adds up to, derived on the client.
//
// The server keeps the authoritative aggregates on CClient (m_add_defense_ratio,
// m_add_abs_physical_defense, m_damage_absorption_armor[] and the rest) and puts
// none of them on the wire — the only stat packet the client gets carries the six
// core attributes. So this re-derives what the client can see for itself: every
// equipped item's config row plus its rolled modifier lines, read through the
// replicated modifier catalog. It is the same arithmetic the item tooltips
// already do one item at a time, totalled across the worn set.
//
// That makes two kinds of row, and the difference matters when reading the
// character panel:
//
//   * Defence ratio and the weapon damage ranges are real totals. Their formulas
//     are fully reproducible from what the client holds, in the same way the
//     panel already re-derives max HP/MP/SP/load through hb::shared::calc.
//   * Everything keyed by modifier id is the GEAR CONTRIBUTION, not a final
//     total. Physical and magical absorption in particular have a per-slot
//     armour term the server computes and never sends, and magic resistance has
//     a base term from the same place. Showing the rolled contribution is honest;
//     claiming a total would not be. Closing that gap is a wire change and
//     therefore a Compatibility bump.

#include "Item/Item.h"          // dice_range, parse_dice
#include "Item/ModifierIds.h"

#include <array>

class CGame;

namespace hb::client
{
	struct combat_summary
	{
		// --- weapon ----------------------------------------------------------
		// The weapon's own dice, as configured. A bow's damage additionally
		// depends on the quiver, which the client does not track — the server
		// zeroes an unquivered bow at hit resolution and never says so.
		bool has_weapon = false;
		dice_range damage_small{ 0, 0 };   // vs small targets (players, most npcs)
		dice_range damage_large{ 0, 0 };   // vs large targets
		int attack_delay = 0;

		// --- armour ----------------------------------------------------------
		// hb::shared::calc::defense_ratio_base plus every worn piece's
		// defense_ratio_from_item, plus the rolled defence_ratio lines.
		int defense_ratio = 0;
		int armour_defence = 0;         // the gear term of the above, on its own

		// --- rolled modifier totals, in display units ------------------------
		// Indexed by unified modifier id. Display units means the raw rolled
		// value already multiplied by the catalog's scale, which is what every
		// tooltip shows.
		std::array<int, hb::shared::item::modifier_id::move_speed + 1> gear{};

		int rolled(uint8_t modifier_id) const
		{
			return modifier_id < gear.size() ? gear[modifier_id] : 0;
		}

		// Any rolled line at all — an unequipped character gets a short pane
		// rather than a wall of zeroes.
		bool has_any_rolled() const;
	};

	// Walk the player's equipped items once and total everything above.
	combat_summary build_combat_summary(CGame& game);
}
