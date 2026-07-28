// LegacyRollStrategy.h: the legacy Roll strategy (Tiers 3-A)
//
// The retail-faithful roll, extracted from ItemManager and pool-driven
// since Tiers 2-C: weighted prefix + secondary pick from the item's
// attribute pool, value rolls from the legacy attribute-type tables.
// Reads its dataset through CGame::get_tier_config() at roll time so
// `reload tiers` is picked up without re-selection.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RollStrategy.h"

class CGame;

namespace hb::server
{

class legacy_roll_strategy : public roll_strategy
{
public:
	explicit legacy_roll_strategy(CGame& game) : m_game(game) {}

	// The context is deliberately unused: legacy rolls every spawn venue
	// (stage-1 and stage-2 drops alike), exactly as before the seam.
	// first_drop_chance keeps the seam's flat-base default.
	bool roll(CItem& item, const roll_context& context) override;

private:
	CGame& m_game;
};

} // namespace hb::server
