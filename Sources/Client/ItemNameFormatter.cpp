#include "ItemNameFormatter.h"
#include "Item/Item.h"
#include "lan_eng.h"
#include "GameConstants.h"
#include "OwnerType.h"

#include <format>
#include <string>

using hb::shared::item::EquipPos;

item_name_formatter& item_name_formatter::get()
{
	static item_name_formatter instance;
	return instance;
}

void item_name_formatter::set_item_configs(const std::array<std::unique_ptr<CItem>, 5000>& configs)
{
	m_item_configs = &configs;
}

void item_name_formatter::set_catalog(const modifier_catalog_entry* catalog)
{
	m_modifier_catalog = catalog;
}

namespace {

// Format a catalog format string with the scaled value. Format strings are
// replicated data — never trust them to be valid.
std::string format_catalog_value(const std::string& fmt, uint32_t value)
{
	try
	{
		return std::vformat(fmt, std::make_format_args(value));
	}
	catch (const std::format_error&)
	{
		return std::to_string(value);
	}
}

// Tooltip line placement is client rendering behavior, not catalog data:
// these modifiers merge into the base-stat lines instead of standing alone.
effect_category category_for_modifier(uint8_t modifier)
{
	using namespace hb::shared::item;
	switch (modifier)
	{
	case modifier_id::sharp:
	case modifier_id::ancient: return effect_category::inline_damage;
	case modifier_id::light:   return effect_category::inline_weight;
	case modifier_id::strong:  return effect_category::inline_durability;
	default:                   return effect_category::standalone;
	}
}

} // namespace

// Build the tooltip line for one rolled modifier from its catalog entry.
// A label containing "{}" (or an absent label) renders as one whole
// formatted line; otherwise the label pairs with the formatted value.
void item_name_formatter::append_modifier_effect(std::vector<tooltip_effect>& effects, uint8_t modifier, uint32_t rolled_value) const
{
	if (!m_modifier_catalog) return;
	const auto& entry = m_modifier_catalog[modifier];
	uint32_t value = rolled_value * entry.multiplier;
	effect_category category = category_for_modifier(modifier);

	bool whole_line = entry.effect_label.empty() || entry.effect_label.find("{}") != std::string::npos;
	if (whole_line)
	{
		const std::string& fmt = entry.effect_label.empty() ? entry.effect_format : entry.effect_label;
		if (!fmt.empty())
			effects.push_back({format_catalog_value(fmt, value), "", category});
	}
	else
	{
		effects.push_back({entry.effect_label,
			entry.effect_format.empty() ? "" : format_catalog_value(entry.effect_format, value), category});
	}
}

CItem* item_name_formatter::get_config(int item_id) const
{
	if (!m_item_configs || item_id <= 0 || item_id >= 5000) return nullptr;
	return (*m_item_configs)[item_id].get();
}

ItemNameInfo item_name_formatter::format(CItem* item)
{
	auto result = format(item->m_id_num, item->to_instance_data());

	// Mana save effect comes from item config, not instance data
	CItem* cfg = get_config(item->m_id_num);
	if (cfg)
	{
		auto effectType = cfg->get_item_effect_type();
		int mana_save_value = 0;
		if (effectType == hb::shared::item::ItemEffectType::AttackManaSave)
		{
			mana_save_value = cfg->m_item_effect_value4;
		}
		else if (effectType == hb::shared::item::ItemEffectType::add_effect &&
		         cfg->m_item_effect_value1 == hb::shared::item::to_int(hb::shared::item::AddEffectType::ManaSave))
		{
			mana_save_value = cfg->m_item_effect_value2;
		}

		if (mana_save_value > 0)
		{
			result.is_special = true;
			result.effects.push_back({"Mana save ", std::format("+{}%", mana_save_value)});
		}
	}

	return result;
}

ItemNameInfo item_name_formatter::format(short item_id)
{
	ItemNameInfo result;
	CItem* cfg = get_config(item_id);
	if (!cfg || cfg->m_name[0] == '\0')
	{
		result.name = "Unknown Item";
		return result;
	}
	result.name = cfg->m_name;
	if (hb::shared::item::is_special_item(item_id)) result.is_special = true;
	return result;
}

ItemNameInfo item_name_formatter::format(short item_id, const hb::shared::item::item_instance_data& data)
{
	ItemNameInfo result;
	uint32_t type1, type2, value1, value2, value3;

	CItem* cfg = get_config(item_id);
	if (!cfg || cfg->m_name[0] == '\0')
	{
		result.name = "Unknown Item";
		return result;
	}
	const char* name = cfg->m_name;

	if (hb::shared::item::is_special_item(item_id)) result.is_special = true;

	if (data.is_custom_made())
	{
		result.is_special = true;
		result.name = name;
		if (cfg->get_item_type() == hb::shared::item::item_type::material)
			result.effects.push_back({"Purity: ", std::format("{}%", data.special_effect_value2)});
		else
		{
			if (cfg->get_equip_pos() == EquipPos::LeftFinger)
				result.effects.push_back({"Completion: ", std::format("{}%", data.special_effect_value2)});
			else
				result.effects.push_back({"Completion: ", std::format("{}%", data.special_effect_value2 + 100)});
		}
	}
	else
	{
		if (data.count <= 1)
			result.name = name;
		else
			result.name = std::format(DRAW_DIALOGBOX_SELLOR_REPAIR_ITEM1, data.count, name);
	}

	type1 = data.get_prefix_type();
	value1 = data.get_prefix_value();
	type2 = data.get_secondary_type();
	value2 = data.get_secondary_value();

	if (type1 != 0 || type2 != 0)
	{
		result.is_special = true;
		if (type1 != 0)
		{
			// Name prefix word comes from the replicated catalog
			if (m_modifier_catalog && !m_modifier_catalog[type1].display_name.empty())
				result.name = m_modifier_catalog[type1].display_name + " " + result.name;

			append_modifier_effect(result.effects, static_cast<uint8_t>(type1), value1);
		}

		if (type2 != 0)
			append_modifier_effect(result.effects, static_cast<uint8_t>(type2), value2);
	}

	value3 = data.get_enchant_bonus();
	if (value3 > 0)
	{
		auto plusPos = result.name.rfind('+');
		if (plusPos != std::string::npos && plusPos + 1 < result.name.size())
		{
			try {
				int existingPlus = std::stoi(result.name.substr(plusPos + 1));
				value3 += existingPlus;
				result.name = std::format("{}+{}", result.name.substr(0, plusPos), value3);
			} catch (...) {
				result.name += std::format("+{}", value3);
			}
		}
		else
		{
			result.name += std::format("+{}", value3);
		}
	}

	return result;
}
