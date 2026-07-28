#pragma once
#include <cstdint>

#include "ItemAttributeData.h"  // the 15-byte attribute POD (spec §12)
#include "ModifierIds.h"        // unified modifier IDs — modifier slot types are keyed on these

namespace hb::shared::item {

// All dynamic/mutable item fields — the per-instance data layered on top of base config.
// item_id is NOT included — it's a lookup key that travels separately
// (CItem::m_id_num, CTile::m_item_id, function parameters, etc.).
struct item_instance_data
{
	// Non-attribute instance fields
	uint64_t count = 0;
	int16_t touch_effect_type = 0;
	int16_t touch_effect_value1 = 0;
	int16_t touch_effect_value2 = 0;
	int16_t touch_effect_value3 = 0;
	int16_t special_effect_value1 = 0;
	int16_t special_effect_value2 = 0;
	int16_t special_effect_value3 = 0;
	uint16_t cur_durability = 0;
	int8_t item_color = 0;

	// Attribute portion — the same POD every item-carrying packet embeds,
	// so encode/decode is plain struct assignment.
	item_attribute_data attributes;

	void clear() { *this = {}; }
	bool has_attributes() const { return attributes.has_any(); }

	// Attribute accessors. Whole-POD moves are plain assignment of
	// `attributes`; these cover per-field access, with prefix/secondary as
	// the legacy-strategy views over the modifier slots: prefix =
	// modifiers[0], secondary = modifiers[1]. Types are unified modifier IDs
	// (ModifierIds.h).
	uint8_t get_tier() const { return attributes.tier; }
	uint8_t get_prefix_type() const { return attributes.modifiers[0].type; }
	uint8_t get_prefix_value() const { return attributes.modifiers[0].value; }
	uint8_t get_secondary_type() const { return attributes.modifiers[1].type; }
	uint8_t get_secondary_value() const { return attributes.modifiers[1].value; }
	uint8_t get_enchant_bonus() const { return attributes.enchant_bonus; }
	bool is_custom_made() const { return attributes.custom_made != 0; }
	bool has_prefix() const { return attributes.modifiers[0].type != 0; }

	void set_tier(uint8_t t) { attributes.tier = t; }
	void set_custom_made(bool custom) { attributes.custom_made = custom ? 1 : 0; }
	void set_prefix(uint8_t type, uint8_t value) { attributes.modifiers[0].type = type; attributes.modifiers[0].value = value; }
	void set_secondary(uint8_t type, uint8_t value) { attributes.modifiers[1].type = type; attributes.modifiers[1].value = value; }
	void set_enchant_bonus(uint8_t value) { attributes.enchant_bonus = value; }
};

}
