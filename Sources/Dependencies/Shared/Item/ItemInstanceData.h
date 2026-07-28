#pragma once
#include <algorithm>
#include <cstdint>

namespace hb::shared::item {

// All dynamic/mutable item fields — the per-instance data layered on top of base config.
// item_id is NOT included — it's a lookup key that travels separately
// (CItem::m_id_num, CTile::m_item_id, function parameters, etc.).
// Field names match CItem::copy_attributes_to / load_attributes_from templates
// for the 6 attribute fields (custom_made through enchant_bonus).
struct item_instance_data
{
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
	uint8_t custom_made = 0;
	uint8_t prefix_type = 0;
	uint8_t prefix_value = 0;
	uint8_t secondary_type = 0;
	uint8_t secondary_value = 0;
	uint8_t enchant_bonus = 0;

	void clear() { *this = {}; }
	bool has_attributes() const { return custom_made || prefix_type || secondary_type || enchant_bonus; }

	// Attribute accessors — the single funnel for the attribute portion
	// (custom_made through enchant_bonus). All outside code must go through
	// these (or the CItem delegates) instead of touching the fields directly,
	// so the Item Tiers struct reshape only reimplements these bodies.
	uint8_t get_prefix_type() const { return prefix_type; }
	uint8_t get_prefix_value() const { return prefix_value; }
	uint8_t get_secondary_type() const { return secondary_type; }
	uint8_t get_secondary_value() const { return secondary_value; }
	uint8_t get_enchant_bonus() const { return enchant_bonus; }
	bool is_custom_made() const { return custom_made != 0; }
	bool has_prefix() const { return prefix_type != 0; }

	void set_custom_made(bool custom) { custom_made = custom ? 1 : 0; }
	void set_prefix(uint8_t type, uint8_t value) { prefix_type = type; prefix_value = value; }
	void set_secondary(uint8_t type, uint8_t value) { secondary_type = type; secondary_value = value; }
	void set_enchant_bonus(uint8_t value) { enchant_bonus = value; }

	// Copy the attribute portion to a packet or DB struct with matching field names.
	template <typename T>
	void copy_attributes_to(T& target) const
	{
		target.custom_made = custom_made;
		target.prefix_type = prefix_type;
		target.prefix_value = prefix_value;
		target.secondary_type = secondary_type;
		target.secondary_value = secondary_value;
		target.enchant_bonus = enchant_bonus;
	}

	// Load the attribute portion from a packet or DB struct with matching field names.
	template <typename T>
	void load_attributes_from(const T& source)
	{
		custom_made = source.custom_made;
		prefix_type = source.prefix_type;
		prefix_value = source.prefix_value;
		secondary_type = source.secondary_type;
		secondary_value = source.secondary_value;
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
