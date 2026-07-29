// RollStrategy.h: the Roll strategy seam (Tiers 3-A/3-B)
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
// Design contract: PLANS/ItemTiers_Plan.md §2, §8; plan cycles 3-A/3-B.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>

#include "Item/ItemAttributeData.h"

class CItem;

namespace hb::server
{

// Where a drop came from — the tiered strategy's spec-§8 gates. The
// legacy strategy ignores it (legacy rolls every spawn venue, unchanged).
struct roll_context
{
	// Loot grade (1..5) of the monster whose death produced the drop;
	// 0 = no grade (non-drop venues — GM minting until 3-G).
	uint8_t loot_grade = 0;

	// True only for the stage-1 drop rolled at the moment of death — the
	// only venue that tier-rolls (spec §8: second drops never tiered).
	bool first_drop = false;
};

// The legacy flat base chance for the stage-1 first drop, out of 10000
// (10%). Strategy policy since spec §8 made the tiered replacement a
// per-grade data column — the drop pipeline no longer owns this number.
inline constexpr uint32_t base_primary_drop_chance = 1000;

// The spec §1 scope gate exactly as the tiered roll applies it: gear class
// shape (derive_tier_item_class) plus the named-unique exclusion. It lives on
// the seam rather than inside the strategy because two other readers need the
// roller's own answer — the boot validator's drop-table sweeps and the
// dropodds report, which would be worse than useless if it disagreed with the
// roll about what counts as tierable gear.
bool is_tier_scope_gear(const CItem& item);

class roll_strategy
{
public:
	virtual ~roll_strategy() = default;

	// Rolls attributes onto a freshly initialized drop (spawn_npc_drop_item).
	// False = no attributes rolled; the item keeps its init_item_attr state
	// and spawns plain.
	virtual bool roll(CItem& item, const roll_context& context) = 0;

	// Applies a GM-stated attribute set to a freshly initialized item — the
	// spec §10 creator, which specifies rather than rolls. Empty return =
	// applied; otherwise the structural-legality violation that rejected the
	// request, phrased for the GM. Legality is mode-owned (what a legal item
	// even looks like is exactly what the two strategies disagree about), so
	// the gate lives here beside the roll it must agree with.
	virtual std::string mint(CItem& item,
		const hb::shared::item::item_attribute_data& requested) = 0;

	// The stage-1 base drop chance (out of 10000) this strategy's drop
	// economy uses. Default = the legacy flat base; tiered overrides with
	// the grade's first_drop_chance row (spec §8) so drop odds stay
	// live-reloadable.
	virtual uint32_t first_drop_chance(uint8_t) const
	{
		return base_primary_drop_chance;
	}
};

} // namespace hb::server
