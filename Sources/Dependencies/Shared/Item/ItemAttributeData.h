#pragma once
#include <cstdint>

#include "Packet/PacketCommon.h"

namespace hb::shared::item {

// The locked 15-byte all-uint8_t attribute POD from the Item Tiers design
// contract (PLANS/ItemTiers_Plan.md §12). This exact struct is embedded in
// item_instance_data and in every item-carrying packet — the wire carries it
// verbatim, so it must stay packed, byte-ordered as declared, and 15 bytes.
//
// Modifier slot types are unified modifier IDs (ModifierIds.h); 0 = empty
// slot. Both Roll strategies write it; tier stays 0 and slots [1]..[3] stay
// empty until the tiered strategy rolls them (Phase 3).
HB_PACK_BEGIN
struct HB_PACKED item_modifier
{
	uint8_t type = 0;             // unified modifier ID; 0 = empty slot
	uint8_t value = 0;
	uint8_t value2 = 0;           // second roll for attribute pairs; 0 otherwise
};

struct HB_PACKED item_attribute_data
{
	uint8_t custom_made = 0;
	uint8_t tier = 0;             // 0 = untiered, 1-4 = Common..Legendary
	item_modifier modifiers[4];
	uint8_t enchant_bonus = 0;

	void clear() { *this = {}; }

	bool has_any() const
	{
		return custom_made || tier || enchant_bonus
			|| modifiers[0].type || modifiers[1].type || modifiers[2].type || modifiers[3].type;
	}

	// Bridge for the legacy prefix/secondary DB columns (character_items,
	// character_bank_items, trading-post escrow) until the 1-E DDL swap moves
	// storage to the flat 13-column layout. Values are unified modifier IDs;
	// tier and slots [2]/[3] cannot be represented in the legacy columns and
	// stay 0 (always true before the tiered strategy exists — nothing lost).
	// Takes int so sqlite column reads pass through uncast.
	static item_attribute_data from_legacy_fields(int custom_made, int prefix_type, int prefix_value,
		int secondary_type, int secondary_value, int enchant_bonus)
	{
		item_attribute_data a;
		a.custom_made = static_cast<uint8_t>(custom_made);
		a.modifiers[0].type = static_cast<uint8_t>(prefix_type);
		a.modifiers[0].value = static_cast<uint8_t>(prefix_value);
		a.modifiers[1].type = static_cast<uint8_t>(secondary_type);
		a.modifiers[1].value = static_cast<uint8_t>(secondary_value);
		a.enchant_bonus = static_cast<uint8_t>(enchant_bonus);
		return a;
	}
};
HB_PACK_END

static_assert(sizeof(item_attribute_data) == 15, "spec §12: the attribute POD is exactly 15 bytes");

}
