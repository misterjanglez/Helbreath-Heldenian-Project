#include "ItemNameFormatter.h"
#include "Item/Item.h"
#include "lan_eng.h"
#include "CommonTypes.h"
#include "GameConstants.h"
#include "OwnerType.h"

#include <array>
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

void item_name_formatter::set_tier_presentation(const tier_presentation_entry* tiers, const std::string* name_template)
{
	m_tier_presentation = tiers;
	m_tier_name_template = name_template;
}

namespace {

// Format a catalog format string with the scaled value(s). An attribute-pair
// row (spec §12) carries two placeholders and consumes both rolls; a one-
// placeholder row simply ignores the second argument. Format strings are
// replicated data — never trust them to be valid.
std::string format_catalog_value(const std::string& fmt, uint32_t value, uint32_t value2)
{
	try
	{
		return std::vformat(fmt, std::make_format_args(value, value2));
	}
	catch (const std::format_error&)
	{
		return std::to_string(value);
	}
}

// Lead the tier word onto the item name using the replicated template
// ("{tier} {name}"). Each placeholder's first occurrence is substituted, from
// positions found in the template so a substitution is never rescanned; a
// template missing either placeholder demotes to tier word plus base name.
std::string apply_name_template(const std::string& tmpl, const std::string& tier_word, const std::string& base_name)
{
	constexpr size_t token_len = 6;   // "{tier}" and "{name}" are the same length
	size_t tier_pos = tmpl.find("{tier}");
	size_t name_pos = tmpl.find("{name}");
	if (tier_pos == std::string::npos || name_pos == std::string::npos)
		return tier_word + " " + base_name;

	// Replace the later placeholder first so the earlier position stays valid.
	std::string out = tmpl;
	if (tier_pos > name_pos)
	{
		out.replace(tier_pos, token_len, tier_word);
		out.replace(name_pos, token_len, base_name);
	}
	else
	{
		out.replace(name_pos, token_len, base_name);
		out.replace(tier_pos, token_len, tier_word);
	}
	return out;
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
void item_name_formatter::append_modifier_effect(std::vector<tooltip_effect>& effects, uint8_t modifier,
	uint32_t rolled_value, uint32_t rolled_value2) const
{
	const modifier_catalog_entry* row = catalog_row(modifier);
	if (row == nullptr)
	{
		// Catalog gap: show the raw roll instead of dropping the line, so
		// missing replicated data is visible rather than silently invisible.
		effects.push_back({std::format("Modifier {} ", modifier), std::format("+{}", rolled_value),
			effect_category::standalone});
		return;
	}
	const auto& entry = *row;

	uint32_t value = rolled_value * entry.multiplier;
	uint32_t value2 = rolled_value2 * entry.multiplier;
	effect_category category = category_for_modifier(modifier);

	bool whole_line = entry.effect_label.empty() || entry.effect_label.find("{}") != std::string::npos;
	if (whole_line)
	{
		const std::string& fmt = entry.effect_label.empty() ? entry.effect_format : entry.effect_label;
		if (!fmt.empty())
			effects.push_back({format_catalog_value(fmt, value, value2), "", category});
	}
	else
	{
		effects.push_back({entry.effect_label,
			entry.effect_format.empty() ? "" : format_catalog_value(entry.effect_format, value, value2), category});
	}
}

// Tier word + Tier color (spec §11). Both come from the replicated tier
// presentation table; an unnamed or unreplicated row demotes to the plain
// name in the caller's own color.
void item_name_formatter::apply_tier_presentation(ItemNameInfo& info, uint8_t tier) const
{
	if (tier == 0 || tier > hb::shared::item::tier_count || m_tier_presentation == nullptr) return;

	const auto& row = m_tier_presentation[tier - 1];
	if (row.name.empty()) return;

	static const std::string no_template;
	info.tier = tier;
	info.tier_color = row.color;
	info.name = apply_name_template(m_tier_name_template ? *m_tier_name_template : no_template,
		row.name, info.name);
}

hb::shared::render::Color item_name_color(const ItemNameInfo& info,
	const hb::shared::render::Color& normal_color,
	const std::optional<hb::shared::render::Color>& dye_tint)
{
	if (info.tier != 0) return info.tier_color;
	if (dye_tint) return *dye_tint;
	if (info.is_special) return GameColors::UIItemName_Special;
	return normal_color;
}

// The one catalog lookup. A row the server never replicated is indistinguish-
// able from no catalog at all, so both answer nullptr and every caller — line
// text, prefix word, Bucket order — treats a gap the same way.
const modifier_catalog_entry* item_name_formatter::catalog_row(uint8_t modifier) const
{
	if (m_modifier_catalog == nullptr || modifier == 0) return nullptr;
	const auto& row = m_modifier_catalog[modifier];
	return row.present ? &row : nullptr;
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

	// Rolled modifier lines. Legacy items park their two lines in slots
	// [0]/[1]; tiered items fill one to four slots in roll order (spec §12),
	// which bounds the tooltip at four modifier lines by construction. Tiered
	// items order the lines by the catalog Bucket — spec §12 makes bucket_id
	// the stable tooltip sort key. Legacy items keep prefix-then-secondary so
	// their tooltips read exactly as they did before tiers existed.
	const auto& mods = data.attributes.modifiers;
	std::array<hb::shared::item::item_modifier, hb::shared::item::modifier_slot_count> lines{};
	std::array<uint8_t, hb::shared::item::modifier_slot_count> buckets{};
	size_t line_count = 0;
	for (const auto& mod : mods)
	{
		if (mod.type == 0) continue;
		const modifier_catalog_entry* row = catalog_row(mod.type);
		buckets[line_count] = row ? row->bucket_id : uint8_t{0};
		lines[line_count++] = mod;
	}

	if (line_count != 0)
	{
		result.is_special = true;

		if (data.get_tier() != 0)
		{
			// Insertion sort: four elements at most, stable, and allocation-free
			// (libstdc++'s stable_sort takes a temporary buffer at any size).
			for (size_t i = 1; i < line_count; i++)
			{
				auto line = lines[i];
				uint8_t bucket = buckets[i];
				size_t j = i;
				for (; j > 0 && buckets[j - 1] > bucket; j--)
				{
					lines[j] = lines[j - 1];
					buckets[j] = buckets[j - 1];
				}
				lines[j] = line;
				buckets[j] = bucket;
			}
		}
		else if (const modifier_catalog_entry* prefix = catalog_row(mods[0].type);
			prefix != nullptr && !prefix->display_name.empty())
		{
			// Legacy name prefix word — the tier word replaces it on a tiered
			// item, whose modifiers each get their own tooltip line instead.
			result.name = prefix->display_name + " " + result.name;
		}

		for (size_t i = 0; i < line_count; i++)
			append_modifier_effect(result.effects, lines[i].type, lines[i].value, lines[i].value2);
	}

	apply_tier_presentation(result, data.get_tier());

	uint32_t value3 = data.get_enchant_bonus();
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
