// RollStrategy.h: the Roll strategy seam (Tiers 3-A)
//
// One interface, two pluggable implementations of item attribute
// generation: legacy (the retail-faithful prefix + secondary roll) and
// tiered (the Common/Rare/Epic/Legendary modifier system). The strategy
// is selected exactly once per process from meta.item_system — the mode
// is restart-only by design (`reload tiers` pins the running mode), so
// the selection never goes stale. Strategies differ in what they roll,
// never in what they store: both emit unified modifier IDs onto the
// item instance.
//
// Design contract: PLANS/ItemTiers_Plan.md §2; plan cycle 3-A.
//
//////////////////////////////////////////////////////////////////////

#pragma once

class CItem;

namespace hb::server
{

class roll_strategy
{
public:
	virtual ~roll_strategy() = default;

	// Rolls attributes onto a freshly initialized drop (spawn_npc_drop_item;
	// GM minting routes here in 3-G). False = no attributes rolled; the
	// item keeps its init_item_attr state and spawns plain.
	virtual bool roll(CItem& item) = 0;
};

} // namespace hb::server
