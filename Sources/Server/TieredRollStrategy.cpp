// TieredRollStrategy.cpp: the tiered Roll strategy (Tiers 3-A skeleton)
//
//////////////////////////////////////////////////////////////////////

#include "TieredRollStrategy.h"

namespace hb::server
{

bool tiered_roll_strategy::roll(CItem&)
{
	// 3-B lands the pipeline: Loot grade -> Tier -> Bucket-law modifier
	// selection -> Band/Tier-curve value rolls, reading m_game's tier
	// config. Until then tiered worlds spawn attribute-less drops.
	return false;
}

} // namespace hb::server
