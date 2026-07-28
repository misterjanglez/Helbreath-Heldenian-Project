#pragma once
#include <algorithm>
#include <cstdint>

#include "ModifierIds.h"  // unified modifier IDs — modifier slot types are keyed on these

namespace hb::shared::item {

// All dynamic/mutable item fields — the per-instance data layered on top of base config.
// item_id is NOT included — it's a lookup key that travels separately
// (CItem::m_id_num, CTile::m_item_id, function parameters, etc.).
//
// The attribute portion (custom_made through enchant_bonus) is the locked
// 15-byte all-uint8_t POD from the Item Tiers design contract
// (PLANS/ItemTiers_Plan.md §12). Both Roll strategies write it; tier stays
// 0 and slots [1]..[3] stay empty until the tiered strategy rolls them.
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

	// Attribute portion — spec §12 verbatim
	uint8_t custom_made = 0;
	uint8_t tier = 0;                 // 0 = untiered, 1-4 = Common..Legendary
	struct modifier
	{
		uint8_t type = 0;             // unified modifier ID; 0 = empty slot
		uint8_t value = 0;
		uint8_t value2 = 0;           // second roll for attribute pairs; 0 otherwise
	} modifiers[4];
	uint8_t enchant_bonus = 0;

	void clear() { *this = {}; }
	bool has_attributes() const
	{
		return custom_made || tier || enchant_bonus
			|| modifiers[0].type || modifiers[1].type || modifiers[2].type || modifiers[3].type;
	}

	// Attribute accessors — the single funnel for the attribute portion.
	// All outside code must go through these (or the CItem delegates)
	// instead of touching the fields directly.
	//
	// The prefix/secondary views are the legacy-strategy semantics over the
	// modifier slots: prefix = modifiers[0], secondary = modifiers[1]. Types
	// are unified modifier IDs (ModifierIds.h).
	uint8_t get_tier() const { return tier; }
	uint8_t get_prefix_type() const { return modifiers[0].type; }
	uint8_t get_prefix_value() const { return modifiers[0].value; }
	uint8_t get_secondary_type() const { return modifiers[1].type; }
	uint8_t get_secondary_value() const { return modifiers[1].value; }
	uint8_t get_enchant_bonus() const { return enchant_bonus; }
	bool is_custom_made() const { return custom_made != 0; }
	bool has_prefix() const { return modifiers[0].type != 0; }

	void set_tier(uint8_t t) { tier = t; }
	void set_custom_made(bool custom) { custom_made = custom ? 1 : 0; }
	void set_prefix(uint8_t type, uint8_t value) { modifiers[0].type = type; modifiers[0].value = value; }
	void set_secondary(uint8_t type, uint8_t value) { modifiers[1].type = type; modifiers[1].value = value; }
	void set_enchant_bonus(uint8_t value) { enchant_bonus = value; }

	// Copy the attribute portion to a packet or DB struct with matching field
	// names. The legacy-named wire/DB fields carry the first two modifier
	// slots (unified IDs) until the packet reshape (1-C) / DDL swap (1-E)
	// move the whole POD; tier and slots [2]/[3] are always empty before the
	// tiered strategy exists, so nothing is lost in transit.
	template <typename T>
	void copy_attributes_to(T& target) const
	{
		target.custom_made = custom_made;
		target.prefix_type = modifiers[0].type;
		target.prefix_value = modifiers[0].value;
		target.secondary_type = modifiers[1].type;
		target.secondary_value = modifiers[1].value;
		target.enchant_bonus = enchant_bonus;
	}

	// Load the attribute portion from a packet or DB struct with matching field names.
	template <typename T>
	void load_attributes_from(const T& source)
	{
		custom_made = source.custom_made;
		modifiers[0].type = source.prefix_type;
		modifiers[0].value = source.prefix_value;
		modifiers[1].type = source.secondary_type;
		modifiers[1].value = source.secondary_value;
		enchant_bonus = source.enchant_bonus;
	}

	// Instance-to-instance copy keeps the whole POD (tier + all four slots),
	// not just the legacy view.
	void load_attributes_from(const item_instance_data& source)
	{
		custom_made = source.custom_made;
		tier = source.tier;
		for (int i = 0; i < 4; i++)
			modifiers[i] = source.modifiers[i];
		enchant_bonus = source.enchant_bonus;
	}

	// Populate from any packet type that has matching field names
	// (e.g. PacketEventGroundItem). Keeps this header dependency-free.
	// item_id is NOT set — caller reads pkt.item_id separately.
	template<typename Packet>
	static item_instance_data from_ground_item_packet(const Packet& pkt)
	{
		item_instance_data d;
		d.count = static_cast<uint64_t>(std::max<int16_t>(pkt.count, 0));
		d.item_color = static_cast<int8_t>(pkt.item_color);
		d.touch_effect_type = pkt.touch_effect_type;
		d.touch_effect_value1 = pkt.touch_effect_value1;
		d.touch_effect_value2 = pkt.touch_effect_value2;
		d.touch_effect_value3 = pkt.touch_effect_value3;
		d.special_effect_value1 = pkt.special_effect_value1;
		d.special_effect_value2 = pkt.special_effect_value2;
		d.special_effect_value3 = pkt.special_effect_value3;
		d.cur_durability = pkt.cur_durability;
		d.load_attributes_from(pkt);
		return d;
	}
};

}
