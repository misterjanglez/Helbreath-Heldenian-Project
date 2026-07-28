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
		return custom_made || tier || enchant_bonus || modifier_count() != 0;
	}

	uint8_t modifier_count() const
	{
		uint8_t count = 0;
		for (const auto& mod : modifiers)
			if (mod.type != 0) count++;
		return count;
	}

	// The spec §3 invariant *tiered => modifier count == tier* — enforced at
	// roll time and on persistence load paths, never by the storage format.
	// Tier 0 (untiered/legacy) is unconstrained; a tier above the 4 slots can
	// never match a count and correctly fails.
	bool tier_invariant_ok() const
	{
		return tier == 0 || modifier_count() == tier;
	}
};
HB_PACK_END

static_assert(sizeof(item_attribute_data) == 15, "spec §12: the attribute POD is exactly 15 bytes");

}
