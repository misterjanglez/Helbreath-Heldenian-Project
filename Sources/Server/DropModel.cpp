#include "DropModel.h"

#include "Item.h"

using namespace hb::shared::item;

namespace hb::server
{

const char* drop_category_name(uint8_t category)
{
	switch (category)
	{
	case drop_category::gear:       return "gear";
	case drop_category::consumable: return "consumable";
	case drop_category::gold:       return "gold";
	case drop_category::unique:     return "unique";
	default:                        return "?";
	}
}

const char* drop_placement_name(uint8_t placement)
{
	return placement == drop_placement::spiral ? "spiral" : "single";
}

const char* drop_delay_name(uint8_t delay)
{
	return delay == drop_delay::decay ? "decay" : "death";
}

bool parse_drop_placement(const std::string& value, uint8_t& out)
{
	if (value == "single") { out = drop_placement::single; return true; }
	if (value == "spiral") { out = drop_placement::spiral; return true; }
	return false;
}

bool parse_drop_delay(const std::string& value, uint8_t& out)
{
	if (value == "death") { out = drop_delay::death; return true; }
	if (value == "decay") { out = drop_delay::decay; return true; }
	return false;
}

double drop_multipliers::product(int stage_slot, uint8_t category_id,
	uint8_t loot_grade) const
{
	double result = global;
	if (stage_slot >= 1 && stage_slot <= max_stage) result *= stage[stage_slot];
	if (category_id < drop_category::count)         result *= category[category_id];
	if (loot_grade >= 1 && loot_grade <= max_grade) result *= grade[loot_grade];
	return result;
}

uint8_t drop_category_of(const CItem* item_config, int item_id)
{
	// Gold first: it is the one row whose count comes from the monster rather
	// than the row, and the pipeline already knows it by id.
	if (item_id == ItemId::Gold) return drop_category::gold;
	// The named-unique roster outranks class shape — a Xelima Blade is a
	// weapon, but tuning "gear" must not move it.
	if (is_special_item(static_cast<short>(item_id))) return drop_category::unique;
	if (item_config == nullptr) return drop_category::consumable;

	switch (item_config->get_item_sub_type())
	{
	case item_sub_type::weapon:
	case item_sub_type::armor:
	case item_sub_type::accessory:
		return drop_category::gear;
	default:
		return drop_category::consumable;
	}
}

void resolve_drop_chances(const drop_table& table,
	const drop_multipliers& multipliers,
	int stage_slot, uint8_t loot_grade,
	const CItem* const* item_configs, int item_config_count,
	std::vector<uint32_t>& out_chances, bool* saturated)
{
	out_chances.clear();
	out_chances.reserve(table.entries.size());
	if (saturated != nullptr) *saturated = false;

	uint64_t total = 0;
	for (const auto& entry : table.entries)
	{
		const CItem* config =
			(item_configs != nullptr && entry.item_id >= 0 &&
				entry.item_id < item_config_count)
			? item_configs[entry.item_id] : nullptr;
		const double scaled = static_cast<double>(entry.chance_ppb) *
			multipliers.product(stage_slot,
				drop_category_of(config, entry.item_id), loot_grade);

		// A single row can be pushed past the whole denominator on its own;
		// clamping here keeps the sum below the uint64 range and lets the
		// proportional scale-back below do the rest.
		const uint32_t chance = scaled >= static_cast<double>(drop_chance_denominator)
			? drop_chance_denominator
			: (scaled <= 0.0 ? 0u : static_cast<uint32_t>(scaled + 0.5));
		out_chances.push_back(chance);
		total += chance;
	}

	if (total <= drop_chance_denominator) return;

	// Saturated: scale every row back by the same factor so the table sums to
	// the denominator with "nothing" at zero. Integer arithmetic, so the
	// result is reproducible byte for byte on every platform, and the floor
	// keeps the sum at or under the denominator.
	if (saturated != nullptr) *saturated = true;
	for (uint32_t& chance : out_chances)
		chance = static_cast<uint32_t>(
			static_cast<uint64_t>(chance) * drop_chance_denominator / total);
}

int roll_drop_row(const std::vector<uint32_t>& chances, uint32_t draw)
{
	uint64_t cumulative = 0;
	for (size_t i = 0; i < chances.size(); i++)
	{
		cumulative += chances[i];
		if (draw < cumulative) return static_cast<int>(i);
	}
	return -1;                       // the remainder: nothing dropped
}

} // namespace hb::server
