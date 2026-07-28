// TierConfigValidator.h: fail-fast Item Tiers dataset validation (Tiers 2-D)
//
// The spec §2 sweep over BOTH Roll strategies' datasets. Runs at boot —
// any error blocks startup — and on `reload tiers`, where any error
// rejects the candidate and leaves the running config untouched. Every
// error carries table/row precision; no clamping, no skip-and-warn.
//
// Design contract: PLANS/ItemTiers_Plan.md §2/§3/§8; plan cycle 2-D.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <map>
#include <vector>

#include "TierConfigStore.h"

class CItem;
class CNpc;
struct DropTable;
struct attribute_prefix_type_entry;
struct attribute_secondary_type_entry;

namespace hb::server
{

// The running game datasets the tier config is cross-checked against:
// droppable-gear pool coverage and the stage-2 gear rule need item
// configs + drop tables; loot-grade references need npc configs; the
// display-vs-roll multiplier check needs the legacy attribute tables.
struct tier_validation_context
{
	CItem* const* item_configs = nullptr;       // slot index == item id; null slots allowed
	int item_config_count = 0;
	CNpc* const* npc_configs = nullptr;         // slot index == npc id; null slots allowed
	int npc_config_count = 0;
	const std::map<int, DropTable>* drop_tables = nullptr;
	const std::vector<attribute_prefix_type_entry>* attribute_prefix_types = nullptr;
	const std::vector<attribute_secondary_type_entry>* attribute_secondary_types = nullptr;
};

// Full §2 sweep. Each returned string is one error naming its table and
// row; an empty result means the dataset may run in config.item_system.
// Mode-scoped rules: empty tiered tables fail only in tiered mode (the
// unconfigured-world guard), the stage-2 gear rule is tiered-only, and
// pool-coverage / multiplier checks are legacy-only.
std::vector<std::string> validate_tier_config(const tier_config& config,
	const tier_validation_context& context);

// One "tier validator: <error>" log line per error — the shared reporting
// half every caller (boot, reload) pairs with its own outcome summary.
void log_tier_validation_errors(const std::vector<std::string>& errors);

} // namespace hb::server
