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

	// Whether this drop tier-rolls. True for anything the STAGE-1 slot
	// produced and false for the stage-2 slot — spec §8's one deliberate
	// asymmetry, and the only thing left that distinguishes the two stages.
	// It is NOT "the drop at the moment of death": delay is a table property
	// now (#73), so a stage-1 table set to corpse-decay still tier-rolls.
	bool tier_rolls = false;

	// The Huntmaster tier-shift (#122, §3.1.2): x/100 applied to tier
	// weights from `tier_shift_min_tier` up (2 = rare-or-better). 100 =
	// neutral. BOTH halves are resolved ONCE per kill beside the reputation
	// factor — the pct ledger-recorded with it — then carried here (and
	// through the corpse-decay queue) so every drop of that kill rolls the
	// exact shift the kill was priced at, and the roll strategy never reads
	// the guild dataset (a reload between kill and decay changes nothing).
	int tier_shift_pct = 100;
	int tier_shift_min_tier = 2;
};

// The stage-1 base drop chance retired with #73: every drop-table row now
// carries its own absolute per-kill rarity, so there is no flat base left for
// a strategy to own. What survives here is the tier-scope gate below.

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
};

} // namespace hb::server
