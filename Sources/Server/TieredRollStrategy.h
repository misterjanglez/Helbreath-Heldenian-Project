// TieredRollStrategy.h: the tiered Roll strategy (Tiers 3-B)
//
// The Common/Rare/Epic/Legendary roll for stage-1 monster drops:
// Loot grade -> Tier (staircase weights) -> Bucket-law modifier
// selection (per-class eligibility + weights, min-tier ladder) ->
// Band/Tier-curve value rolls. Reads its dataset through
// CGame::get_tier_config() at roll time so `reload tiers` is picked up
// without re-selection.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RollStrategy.h"
#include <random>

class CGame;

namespace hb::server
{

struct loot_grade_config;
struct modifier_catalog_config;
struct tier_config;
struct tier_curve_config;

class tiered_roll_strategy : public roll_strategy
{
public:
	explicit tiered_roll_strategy(CGame& game) : m_game(game) {}

	bool roll(CItem& item, const roll_context& context) override;

	// Tiered legality: the item must be tierable gear, and the requested
	// instance must survive the same structural audit a rolled one does
	// (validate_tiered_instance). Rarity is not a constraint — a GM may mint
	// any legal shape at any tier (spec §10).
	std::string mint(CItem& item,
		const hb::shared::item::item_attribute_data& requested) override;


private:
	uint8_t roll_tier(const loot_grade_config& grade, int tier_shift_pct,
		int tier_shift_min_tier);
	uint8_t roll_modifier_value(const tier_config& config,
		const modifier_catalog_config& modifier, uint8_t tier,
		const tier_curve_config& curve);

	CGame& m_game;

	// The tiered roll uses <random>, not the legacy rand() helpers: cap
	// chances run to 1 in 2,000,000 and MSVC's RAND_MAX of 32767 cannot
	// express them.
	std::mt19937 m_rng{ std::random_device{}() };
};

} // namespace hb::server
