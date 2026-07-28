#pragma once

#include <cstdint>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "Item/ItemInstanceData.h"
#include "ModifierCatalog.h"

class CItem;

enum class effect_category
{
	standalone,         // shown as its own tooltip line
	inline_damage,      // appended to damage line (Sharp, Ancient, enchant)
	inline_defense,     // appended to defence line (enchant)
	inline_weight,      // appended to weight line (Light)
	inline_durability   // applied to durability line (Strong)
};

struct tooltip_effect
{
	std::string label;   // e.g., "Magic Casting Probability "
	std::string value;   // e.g., "+9%"
	effect_category category = effect_category::standalone;
};

struct ItemNameInfo
{
	std::string name;
	std::vector<tooltip_effect> effects;
	bool is_special = false;
	uint8_t tier = 0;                                  // 0 = untiered, 1-4 = Common..Legendary
	hb::shared::render::Color tier_color;              // meaningful only when tier != 0

	// Backward compat: reconstruct combined effect/extra text for the list and
	// dialog surfaces that render two fixed text rows instead of the tooltip's
	// per-line layout. A tiered item can carry four lines, so the leading rows
	// are comma-separated — with the two lines a legacy item has, this returns
	// exactly what it always did.
	std::string effect_text() const
	{
		if (effects.empty()) return "";
		if (effects.size() == 1) return effects[0].label + effects[0].value;
		std::string result;
		for (size_t i = 0; i + 1 < effects.size(); ++i)
		{
			if (!result.empty()) result += ", ";
			result += effects[i].label + effects[i].value;
		}
		return result;
	}

	std::string extra_text() const
	{
		if (effects.size() < 2) return "";
		return effects.back().label + effects.back().value;
	}
};

class item_name_formatter
{
public:
	static item_name_formatter& get();

	// Inject item config list (address must remain stable)
	void set_item_configs(const std::array<std::unique_ptr<CItem>, 5000>& configs);

	// Inject the replicated modifier catalog, keyed by unified modifier ID
	// (pointer must remain stable)
	void set_catalog(const modifier_catalog_entry* catalog);

	// Inject the replicated tier presentation table (indexed tier-1) and the
	// name template that leads the tier word onto the name (both addresses
	// must remain stable). Item Tiers spec §11.
	void set_tier_presentation(const tier_presentation_entry* tiers, const std::string* name_template);

	// Replicated presentation row for a tier, or nullptr when the tier is out of
	// range or the table never arrived. Lets the world renderer read a Tier color
	// per frame without formatting a name it will not draw.
	const tier_presentation_entry* tier_row(uint8_t tier) const;

	// format item name, returning name + attribute strings + special flag
	ItemNameInfo format(CItem* item);
	ItemNameInfo format(short item_id);
	ItemNameInfo format(short item_id, const hb::shared::item::item_instance_data& data);

private:
	item_name_formatter() = default;

	CItem* get_config(int item_id) const;

	// Replicated catalog row for a modifier, or nullptr when the catalog is
	// absent or never carried that row.
	const modifier_catalog_entry* catalog_row(uint8_t modifier) const;

	void append_modifier_effect(std::vector<tooltip_effect>& effects, uint8_t modifier,
		uint32_t rolled_value, uint32_t rolled_value2) const;

	// Fills tier + tier_color and leads the tier word onto the name.
	void apply_tier_presentation(ItemNameInfo& info, uint8_t tier) const;

	const std::array<std::unique_ptr<CItem>, 5000>* m_item_configs = nullptr;
	const modifier_catalog_entry* m_modifier_catalog = nullptr;
	const tier_presentation_entry* m_tier_presentation = nullptr;
	const std::string* m_tier_name_template = nullptr;
};

// The one item-name color picker (spec §11). Precedence: the Tier color for a
// tiered instance, then the legacy dye tint a caller passes for a prefixed
// item, then the special-item green, then the caller's own resting color.
hb::shared::render::Color item_name_color(const ItemNameInfo& info,
	const hb::shared::render::Color& normal_color,
	const std::optional<hb::shared::render::Color>& dye_tint = std::nullopt);
