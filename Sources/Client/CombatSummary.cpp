#include "CombatSummary.h"

#include "Game.h"
#include "Player.h"

#include "Game/BalanceConstants.h"
#include "Game/SharedCalculations.h"

#include <algorithm>

namespace hb::client
{
	namespace
	{
		namespace item = hb::shared::item;

		// The rolled lines an item carries, added into the running totals in
		// display units. This is the same value * catalog-multiplier scaling the
		// tooltips print, so a pane row and an item's own tooltip line agree.
		//
		// A pair row rolls two independent values into one slot; the second half
		// belongs to the paired attribute, which the catalog does not name, so
		// only the first half is attributed here. Pairs are attribute rows, and
		// the attribute totals already arrive from the server on their own
		// packet — the pane reads those, not these.
		void add_rolled_lines(combat_summary& out, const CGame& game,
			const item::item_instance_data& instance)
		{
			for (const auto& slot : instance.attributes.modifiers)
			{
				if (slot.type == item::modifier_id::empty) continue;
				if (slot.type >= out.gear.size()) continue;
				out.gear[slot.type] += slot.value * game.modifier_multiplier(slot.type);
			}
		}
	}

	bool combat_summary::has_any_rolled() const
	{
		for (int total : gear)
			if (total != 0) return true;
		return false;
	}

	combat_summary build_combat_summary(CGame& game)
	{
		combat_summary out;
		const CPlayer& player = *game.m_player;

		out.attack_delay = player.m_playerStatus.attack_delay;

		for (int i = 0; i < hb::shared::limits::MaxItems; i++)
		{
			const CItem* worn = player.m_item_list[i].get();
			if (worn == nullptr || !game.m_is_item_equipped[i]) continue;

			CItem* cfg = game.get_item_config(worn->m_id_num);
			if (cfg == nullptr) continue;

			add_rolled_lines(out, game, worn->m_instance);

			const auto effect = cfg->get_item_effect_type();

			if (item::is_attack_effect_type(effect))
			{
				// Only one weapon can be worn, so the last one wins the same way
				// the server's single set of dice fields does.
				out.has_weapon = true;
				out.damage_small = cfg->get_damage_range();
				out.damage_large = cfg->get_damage_range_large();
			}
			else if (item::is_defense_effect_type(effect))
			{
				out.armour_defence += hb::shared::calc::defense_ratio_from_item(
					cfg->m_item_effect_value1,
					worn->is_custom_made() ? worn->m_instance.special_effect_value2 : 0);
			}

			// Totalled in the walk that is already here rather than by calling
			// equipped_mana_save, which would walk all fifty slots a second time
			// for every frame the panel is open. What the two share is the part
			// that could drift — which fields count, and the ceiling — and both
			// of those are named once elsewhere.
			out.mana_save_pct += cfg->mana_save_percent();
		}

		out.hp_recovery_pct = out.rolled(item::modifier_id::hp_recovery);
		out.mp_recovery_pct = out.rolled(item::modifier_id::mp_recovery);
		out.sp_recovery_pct = out.rolled(item::modifier_id::sp_recovery);
		out.mana_save_pct = std::min(out.mana_save_pct, hb::shared::balance::mana_save_max);

		return out;
	}

	int equipped_mana_save(const CGame& game)
	{
		const CPlayer& player = *game.m_player;
		int total = 0;

		for (int i = 0; i < hb::shared::limits::MaxItems; i++)
		{
			const CItem* worn = player.m_item_list[i].get();
			if (worn == nullptr || !game.m_is_item_equipped[i]) continue;

			// The worn entry carries the instance; only the cached config row
			// carries the effect fields.
			const CItem* cfg = game.get_item_config(worn->m_id_num);
			if (cfg != nullptr) total += cfg->mana_save_percent();
		}

		return std::min(total, hb::shared::balance::mana_save_max);
	}
}
