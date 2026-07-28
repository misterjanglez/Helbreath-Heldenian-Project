// TieredRollStrategy.h: the tiered Roll strategy (Tiers 3-A skeleton)
//
// The Common/Rare/Epic/Legendary roll: Loot grade -> Tier -> Bucket-law
// modifier selection -> Band/Tier-curve value rolls (plan cycle 3-B).
// This is the 3-A seam skeleton: selectable at boot in tiered mode, but
// it rolls nothing yet — every drop spawns attribute-less until 3-B
// lands the pipeline.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RollStrategy.h"

class CGame;

namespace hb::server
{

class tiered_roll_strategy : public roll_strategy
{
public:
	explicit tiered_roll_strategy(CGame& game) : m_game(game) {}

	bool roll(CItem& item) override;

private:
	CGame& m_game;
};

} // namespace hb::server
