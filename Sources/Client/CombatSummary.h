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
		// What the worn pieces contribute to defence ratio, summed through
		// hb::shared::calc::defense_ratio_from_item.
		//
		// The defence ratio TOTAL is not here: the server sends its own on
		// Notify::DerivedStats, and re-deriving it alongside would be two
		// answers to one question, free to disagree the moment the equip pass
		// changes. This is the armour term on its own, which the panel shows
		// beside the total rather than instead of it.
		int armour_defence = 0;

		// --- recovery rates, as real totals ----------------------------------
		// These three are genuine totals rather than gear contribution, and are
		// the only defensive-ish figures that are. The server's m_add_hp /
		// m_add_mp / m_add_sp are accumulated from exactly one source each — the
		// hp/mp/sp_recovery rolled lines (ItemManager's apply_modifier_totals) —
		// so summing the same rolls here reproduces them.
		//
		// The one other writer, an add_effect sub-type 13 "Magin Ruby" term, is
		// unreachable: no row in `items` carries item_effect_value1 13, 14 or 15,
		// so those three gems were never ported and the branch cannot fire. Do
		// not port it here on the strength of the server having it — and note
		// that Shared's AddEffectType enum disagrees with that switch outright
		// (it calls 13 "Summon"), so the raw numbers are the only truth.
		int hp_recovery_pct = 0;
		int mp_recovery_pct = 0;
		int sp_recovery_pct = 0;

		// --- mana save, as a real total ---------------------------------------
		// Also genuine rather than a contribution, and it needed no wire field to
		// become so: every term is in the item configs the client already caches,
		// and get_mana_cost has been totalling exactly this at every cast since
		// long before the panel existed. Capped at 80 the way the server's equip
		// pass caps m_mana_save_ratio.
		int mana_save_pct = 0;

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

	// The worn set's mana saving, capped at 80. Separate from the summary
	// because spell casting needs the one figure on every cast and has no use
	// for the rest of it.
	int equipped_mana_save(const CGame& game);
}
