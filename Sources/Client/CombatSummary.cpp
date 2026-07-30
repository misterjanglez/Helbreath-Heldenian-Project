#include "CombatSummary.h"

#include "Game.h"
#include "Player.h"

#include "Game/SharedCalculations.h"

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

		out.defense_ratio = hb::shared::calc::defense_ratio_base(player.effective_dex());
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
			else if (effect == item::ItemEffectType::Defense
				|| effect == item::ItemEffectType::DefenseSpecAbility)
			{
				out.armour_defence += hb::shared::calc::defense_ratio_from_item(
					cfg->m_item_effect_value1,
					worn->is_custom_made() ? worn->m_instance.special_effect_value2 : 0);
			}
		}

		out.defense_ratio += out.armour_defence
			+ out.rolled(item::modifier_id::defense_ratio);
		if (out.defense_ratio <= 0) out.defense_ratio = 1;

		return out;
	}
}
