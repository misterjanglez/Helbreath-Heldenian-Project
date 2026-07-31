#include "DropModel.h"

#include "Item.h"

#include <algorithm>   // std::min, for the head/tail split

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

// The original's arithmetic, read as a factor instead of a branch threshold:
//
//     dTmp1 = rating x m_cRepDropModifier, clamped to +/-1000
//     equipment share = (1000 + dTmp1) / 10000, against a 1000-wide baseline
//
// so the equipment side moves 0.0 .. 2.0 around 1.0 at rating 0. Verified in
// both archives (HB382_CENTUU Game.cpp:47347, HelbreathServer Game.cpp:48279).
// The clamp is on the PRODUCT, not the rating, which is why it bites at
// rating +/-200 when the modifier is 5 even though rating itself runs to +/-500.
double drop_multipliers::reputation_factor(int rating) const
{
	double term = static_cast<double>(rating) * reputation_modifier;
	if (term >  1000.0) term =  1000.0;
	if (term < -1000.0) term = -1000.0;

	double factor = 1.0 + term / 1000.0;

	// Ours, not the original's: it let the equipment side reach exactly zero.
	if (factor < reputation_floor) factor = reputation_floor;
	if (factor > reputation_cap)   factor = reputation_cap;
	return factor;
}

bool drop_multipliers::reputation_applies(uint8_t category_id)
{
	return category_id == drop_category::gear
		|| category_id == drop_category::unique;
}

double drop_multipliers::product(int stage_slot, uint8_t category_id,
	uint8_t loot_grade, double rep_factor) const
{
	double result = global;
	if (stage_slot >= 1 && stage_slot <= max_stage) result *= stage[stage_slot];
	if (category_id < drop_category::count)         result *= category[category_id];
	if (loot_grade >= 1 && loot_grade <= max_grade) result *= grade[loot_grade];
	if (reputation_applies(category_id))            result *= rep_factor;
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
	int stage_slot, uint8_t loot_grade, double rep_factor,
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
				drop_category_of(config, entry.item_id), loot_grade, rep_factor);

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

void resolve_tail_chances(const drop_table& table,
	const std::vector<uint32_t>& head_chances,
	std::vector<uint32_t>& out_chances)
{
	out_chances = head_chances;
	if (table.tail_rarity_divisor <= 1) return;

	const uint32_t divisor = static_cast<uint32_t>(table.tail_rarity_divisor);
	for (uint32_t& chance : out_chances)
		chance /= divisor;
}

void resolve_entry_counts(const drop_entry& entry, int gold_dice_min,
	int gold_dice_max, int& min_count, int& max_count)
{
	min_count = entry.min_count;
	max_count = entry.max_count;
	if (entry.item_id == ItemId::Gold && max_count <= 0)
	{
		min_count = gold_dice_min;
		max_count = gold_dice_max;
	}
	if (min_count < 0) min_count = 0;
	if (max_count < min_count) max_count = min_count;
}

uint64_t total_chance(const std::vector<uint32_t>& chances)
{
	uint64_t total = 0;
	for (const uint32_t chance : chances)
		total += chance;
	return total;
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

double resolved_slot::per_roll(size_t i) const
{
	return static_cast<double>(chances[i]) / drop_chance_denominator;
}

double resolved_slot::per_roll_tail(size_t i) const
{
	return static_cast<double>(tail_chances[i]) / drop_chance_denominator;
}

double resolved_slot::per_kill(size_t i) const
{
	// The guarantee item's per-kill figure IS the head's leftover: its own row
	// carries no chance at all, and it is paid out precisely when a head roll
	// comes up empty.
	if (is_guarantee(i)) return guarantee_per_kill;
	const double head = std::min(static_cast<double>(head_rolls), expected_rolls);
	return head * per_roll(i) + (expected_rolls - head) * per_roll_tail(i);
}

double resolved_slot::expected_yield() const
{
	const double head = std::min(static_cast<double>(head_rolls), expected_rolls);
	return head + (expected_rolls - head) * tail_total;
}

bool resolve_slot(const std::map<int, drop_table>& tables, int table_id,
	const drop_multipliers& multipliers, int stage_slot, uint8_t loot_grade,
	double rep_factor, const CItem* const* item_configs, int item_config_count,
	resolved_slot& out)
{
	const auto found = tables.find(table_id);
	if (found == tables.end()) return false;

	out.table = &found->second;
	resolve_drop_chances(*out.table, multipliers, stage_slot, loot_grade, rep_factor,
		item_configs, item_config_count, out.chances, &out.saturated);

	// The tail's chances come off the head's, exactly as the roller derives them,
	// so no reader of this can disagree with what actually drops.
	resolve_tail_chances(*out.table, out.chances, out.tail_chances);

	out.head_rolls = std::min(out.table->guaranteed_rolls, out.table->roll_count_min);
	out.expected_rolls = 0.5 * (out.table->roll_count_min + out.table->roll_count_max);
	out.head_total = static_cast<double>(total_chance(out.chances))
		/ drop_chance_denominator;
	out.tail_total = static_cast<double>(total_chance(out.tail_chances))
		/ drop_chance_denominator;
	out.guarantee_per_kill = out.head_rolls * (1.0 - out.head_total);
	return true;
}

} // namespace hb::server
