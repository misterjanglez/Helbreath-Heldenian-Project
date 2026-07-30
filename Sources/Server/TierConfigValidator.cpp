// TierConfigValidator.cpp: fail-fast Item Tiers dataset validation (Tiers 2-D)
//
//////////////////////////////////////////////////////////////////////

#include "TierConfigValidator.h"

#include <algorithm>
#include <format>
#include <set>
#include <utility>

#include "Game.h"                   // DropTable
#include "GameConfigSqliteStore.h"  // attribute_prefix/secondary_type_entry
#include "Item.h"
#include "Npc.h"
#include "Log.h"
#include "RollStrategy.h"           // is_tier_scope_gear

namespace hb::server
{

using namespace hb::shared::item;

// One item config out of the context, or null for an id the running world
// has no config for.
static const CItem* item_config(const tier_validation_context& context, int item_id)
{
	if (context.item_configs == nullptr) return nullptr;
	if (item_id < 0 || item_id >= context.item_config_count) return nullptr;
	return context.item_configs[item_id];
}

const CItem* ordinary_tier_gear(const tier_validation_context& context, int item_id)
{
	if (item_id == 0 || item_id == 90) return nullptr;  // nothing / gold
	const CItem* item = item_config(context, item_id);
	if (item == nullptr) return nullptr;
	return is_tier_scope_gear(*item) ? item : nullptr;
}

namespace
{

struct validation_state
{
	std::vector<std::string> errors;
	// Things the operator must SEE but that are not defects. A multiplier
	// no-op on a guaranteed table is the design, not a fault — but it should
	// never be a surprise, so it is named rather than silently true.
	std::vector<std::string> notes;

	template <typename... Args>
	void add(std::format_string<Args...> fmt, Args&&... args)
	{
		errors.push_back(std::format(fmt, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void add_note(std::format_string<Args...> fmt, Args&&... args)
	{
		notes.push_back(std::format(fmt, std::forward<Args>(args)...));
	}
};

bool known_effect_id(uint8_t id)
{
	return id >= effect_id::critical && id <= effect_id::move_speed;
}

bool valid_item_class(uint8_t item_class)
{
	return item_class >= tier_item_class::melee_weapon && item_class <= tier_item_class::cape;
}

// Anti-cheat knobs (spec §5), both modes — the move and cast checks run
// against every world. A typo'd posture would silently fall back to the code
// default, so it is an error here rather than a quiet mismatch between what
// the DB says and what the server does. A grace of 0 would make every legal
// move a violation; above 100 the floor exceeds the client's own pacing and
// every legal move is a violation too.
void check_anticheat_settings(const tier_config& config, validation_state& v)
{
	for (const char* key : { "posture_move", "posture_cast" })
	{
		auto it = config.settings.find(key);
		if (it == config.settings.end()) continue;
		if (tier_config::parse_posture(it->second)) continue;
		v.add("tier_settings {}: '{}' is not a posture (expected 'log' or 'disconnect')", key, it->second);
	}

	if (config.anticheat.move_grace_pct < 1 || config.anticheat.move_grace_pct > 100)
		v.add("tier_settings move_grace_pct: {} outside 1..100 (the floor is a fraction of the expected tile time)",
			config.anticheat.move_grace_pct);
}

void check_buckets(const tier_config& config, validation_state& v)
{
	std::set<uint8_t> seen;
	for (const auto& bucket : config.buckets)
	{
		if (!seen.insert(bucket.bucket_id).second)
			v.add("tier_buckets bucket {}: duplicate bucket_id", (int)bucket.bucket_id);
		if (bucket.name.empty())
			v.add("tier_buckets bucket {}: empty name", (int)bucket.bucket_id);
	}
}

void check_catalog(const tier_config& config, validation_state& v)
{
	// Required in BOTH modes since Tiers 3-C: equip effect application
	// dispatches on modifier_catalog.effect_id whichever Roll strategy runs,
	// and the catalog has been the only source of tooltip labels since 1-D. An
	// empty catalog would silently stop every rolled line working — exactly
	// what fail-fast exists to prevent.
	if (config.catalog.empty())
		v.add("modifier_catalog: empty - both modes require seeded data");

	std::set<uint8_t> seen;
	for (const auto& row : config.catalog)
	{
		int id = row.modifier_id;
		if (id == modifier_id::empty)
			v.add("modifier_catalog '{}': modifier_id 0 is reserved (empty slot)", row.name);
		if (!seen.insert(row.modifier_id).second)
			v.add("modifier_catalog id {}: duplicate modifier_id", id);
		if (row.name.empty())
			v.add("modifier_catalog id {}: empty name", id);
		if (!known_effect_id(row.effect_id))
			v.add("modifier_catalog id {} '{}': unknown effect_id {}", id, row.name, (int)row.effect_id);
		if (config.find_bucket(row.bucket_id) == nullptr)
			v.add("modifier_catalog id {} '{}': bucket_id {} not in tier_buckets", id, row.name, (int)row.bucket_id);
		if (row.min_tier < 1 || row.min_tier > tier_count)
			v.add("modifier_catalog id {} '{}': min_tier {} outside 1..{}", id, row.name, (int)row.min_tier, (int)tier_count);

		if (row.multiplier == 0)
		{
			// Value-less modifier (Righteous/Agile today): no rolled value, so
			// the whole band axis must be zeroed.
			if (row.band_min != 0 || row.band_max != 0 || row.aggregate_cap != 0)
				v.add("modifier_catalog id {} '{}': multiplier 0 (value-less) requires band {}..{} and aggregate_cap {} all zero",
					id, row.name, row.band_min, row.band_max, row.aggregate_cap);
		}
		else
		{
			if (row.band_min > row.band_max)
				v.add("modifier_catalog id {} '{}': band_min {} > band_max {}", id, row.name, row.band_min, row.band_max);
			if (row.band_min % row.multiplier != 0 || row.band_max % row.multiplier != 0)
				v.add("modifier_catalog id {} '{}': band {}..{} not divisible by multiplier {} (stored byte = display / multiplier)",
					id, row.name, row.band_min, row.band_max, (int)row.multiplier);
			if (row.aggregate_cap < row.band_min)
				v.add("modifier_catalog id {} '{}': aggregate_cap {} below band floor {}", id, row.name, row.aggregate_cap, row.band_min);
		}

		for (int t = 0; t < tier_count; t++)
		{
			if (!row.windows[t].has_value()) continue;
			const auto& window = *row.windows[t];
			if (window.min > window.max)
				v.add("modifier_catalog id {} '{}' tier-{} window: min {} > max {}", id, row.name, t + 1, window.min, window.max);
			if (window.min < row.band_min || window.max > row.band_max)
				v.add("modifier_catalog id {} '{}' tier-{} window: {}..{} outside band {}..{}",
					id, row.name, t + 1, window.min, window.max, row.band_min, row.band_max);
			if (row.multiplier != 0 && (window.min % row.multiplier != 0 || window.max % row.multiplier != 0))
				v.add("modifier_catalog id {} '{}' tier-{} window: {}..{} not divisible by multiplier {}",
					id, row.name, t + 1, window.min, window.max, (int)row.multiplier);
		}
	}
}

void check_eligibility(const tier_config& config, validation_state& v)
{
	std::set<std::pair<uint8_t, uint8_t>> seen;
	for (const auto& row : config.eligibility)
	{
		if (config.find_modifier(row.modifier_id) == nullptr)
			v.add("modifier_eligibility modifier {} class {}: modifier not in modifier_catalog",
				(int)row.modifier_id, (int)row.item_class);
		if (!valid_item_class(row.item_class))
			v.add("modifier_eligibility modifier {} class {}: item_class outside 1..{} (tier_item_class)",
				(int)row.modifier_id, (int)row.item_class, (int)tier_item_class::cape);
		if (row.weight <= 0)
			v.add("modifier_eligibility modifier {} class {}: weight {} must be positive",
				(int)row.modifier_id, (int)row.item_class, row.weight);
		if (!seen.insert({ row.modifier_id, row.item_class }).second)
			v.add("modifier_eligibility modifier {} class {}: duplicate row",
				(int)row.modifier_id, (int)row.item_class);
	}
}

void check_bucket_rules(const tier_config& config, validation_state& v)
{
	std::set<std::pair<uint8_t, uint8_t>> seen;
	for (const auto& rule : config.bucket_rules)
	{
		if (config.find_bucket(rule.bucket_id) == nullptr)
			v.add("bucket_class_rules bucket {} class {}: bucket not in tier_buckets",
				(int)rule.bucket_id, (int)rule.item_class);
		if (!valid_item_class(rule.item_class))
			v.add("bucket_class_rules bucket {} class {}: item_class outside 1..{}",
				(int)rule.bucket_id, (int)rule.item_class, (int)tier_item_class::cape);
		if (!seen.insert({ rule.bucket_id, rule.item_class }).second)
			v.add("bucket_class_rules bucket {} class {}: duplicate row",
				(int)rule.bucket_id, (int)rule.item_class);

		if (!rule.mandatory) continue;

		// A mandatory Bucket must be fillable at every tier, so the class
		// needs at least one eligible modifier in it rollable from Common up.
		bool any = false;
		bool any_common = false;
		for (const auto& e : config.eligibility)
		{
			if (e.item_class != rule.item_class) continue;
			const auto* modifier = config.find_modifier(e.modifier_id);
			if (modifier == nullptr || modifier->bucket_id != rule.bucket_id) continue;
			any = true;
			if (modifier->min_tier <= 1) any_common = true;
		}
		if (!any)
			v.add("bucket_class_rules bucket {} class {}: mandatory but no eligible modifier fills it (empty required bucket)",
				(int)rule.bucket_id, (int)rule.item_class);
		else if (!any_common)
			v.add("bucket_class_rules bucket {} class {}: mandatory but no eligible modifier has min_tier 1 (Common rolls cannot fill it)",
				(int)rule.bucket_id, (int)rule.item_class);
	}
}

void check_curves(const tier_config& config, validation_state& v)
{
	std::set<int> seen;
	for (const auto& curve : config.curves)
	{
		if (!seen.insert(curve.curve_id).second)
			v.add("tier_curves curve {}: duplicate curve_id", curve.curve_id);
		if (curve.name.empty())
			v.add("tier_curves curve {}: empty name", curve.curve_id);
		if (curve.cap_chance_den < 1)
			v.add("tier_curves curve {} '{}': cap_chance_den {} must be >= 1", curve.curve_id, curve.name, curve.cap_chance_den);
		if (curve.p50 < 0.0 || curve.p99 > 1.0 || curve.p50 > curve.p90 || curve.p90 > curve.p99)
			v.add("tier_curves curve {} '{}': anchors p50 {} / p90 {} / p99 {} must be ascending within 0..1",
				curve.curve_id, curve.name, curve.p50, curve.p90, curve.p99);
	}

	std::set<std::pair<uint8_t, uint8_t>> override_seen;
	for (const auto& row : config.curve_overrides)
	{
		if (config.find_modifier(row.modifier_id) == nullptr)
			v.add("tier_curve_overrides modifier {} tier {}: modifier not in modifier_catalog",
				(int)row.modifier_id, (int)row.tier);
		if (row.tier < 1 || row.tier > tier_count)
			v.add("tier_curve_overrides modifier {} tier {}: tier outside 1..{}",
				(int)row.modifier_id, (int)row.tier, (int)tier_count);
		if (row.cap_chance_den < 1)
			v.add("tier_curve_overrides modifier {} tier {}: cap_chance_den {} must be >= 1",
				(int)row.modifier_id, (int)row.tier, row.cap_chance_den);
		if (!override_seen.insert({ row.modifier_id, row.tier }).second)
			v.add("tier_curve_overrides modifier {} tier {}: duplicate row",
				(int)row.modifier_id, (int)row.tier);
	}
}

void check_loot_grades(const tier_config& config, validation_state& v)
{
	std::set<uint8_t> seen;
	for (const auto& grade : config.loot_grades)
	{
		int g = grade.grade;
		if (!seen.insert(grade.grade).second)
			v.add("loot_grades grade {}: duplicate grade", g);
		if (g < 1 || g > loot_grade_count)
			v.add("loot_grades grade {} '{}': grade outside 1..{}", g, grade.name, (int)loot_grade_count);
		if (grade.weight_common < 0 || grade.weight_rare < 0 || grade.weight_epic < 0 || grade.weight_legendary < 0)
			v.add("loot_grades grade {} '{}': negative tier weight", g, grade.name);
		else if (grade.weight_common + grade.weight_rare + grade.weight_epic + grade.weight_legendary == 0)
			v.add("loot_grades grade {} '{}': all tier weights zero (grade can never roll a tier)", g, grade.name);
	}
}

// The per-player reputation layer (#88). Its three knobs are the one part of
// the generosity stack where the neutral value is not 1.0, so a typo here
// silently retunes every player in the world rather than just one lever.
void check_reputation_layer(const tier_config& config, validation_state& v)
{
	// A negative modifier is not checked here: load_drop_multipliers rejects a
	// negative value on ANY multiplier row before it reaches the struct, so the
	// field can never hold one and a check here would be a dead branch.
	const hb::server::drop_multipliers& m = config.generosity;

	// A neutral player must roll exactly as authored. If the window excludes
	// 1.0, rating 0 is silently scaled and every published drop rate is wrong.
	if (m.reputation_floor > 1.0)
		v.add("drop_multipliers reputation/floor: {} > 1.0 (a neutral player would get MORE than the authored rate)",
			m.reputation_floor);
	if (m.reputation_cap < 1.0)
		v.add("drop_multipliers reputation/cap: {} < 1.0 (a neutral player would get LESS than the authored rate)",
			m.reputation_cap);
	if (m.reputation_floor > m.reputation_cap)
		v.add("drop_multipliers reputation: floor {} exceeds cap {}",
			m.reputation_floor, m.reputation_cap);

	if (m.reputation_modifier == 0.0)
		v.add_note("drop_multipliers reputation/modifier is 0 - the reputation layer is disabled and every player rolls at the authored rate");
	else
		v.add_note("drop_multipliers reputation: gear and unique rows scale x{:.2f}..x{:.2f} with the killer's rating, saturating at rating +/-{:.0f}",
			m.reputation_factor(-500), m.reputation_factor(500),
			1000.0 / m.reputation_modifier);
}

// Both modes: since cycle 3-F the enchant handler reads caps, per-step
// success and destroy flags from these tables in legacy mode too, so an
// unseeded world would silently stop enchanting. Fail the boot instead.
void check_enchant_tables(const tier_config& config, validation_state& v)
{
	std::set<uint8_t> categories;
	for (const auto& category : config.enchant_categories)
	{
		if (!categories.insert(category.category).second)
			v.add("enchant_categories category {}: duplicate category", (int)category.category);
		if (category.cap < 0)
			v.add("enchant_categories category {} '{}': negative cap {}", (int)category.category, category.name, category.cap);
		if (category.endurance_growth_pct < 0)
			v.add("enchant_categories category {} '{}': negative endurance_growth_pct {}",
				(int)category.category, category.name, category.endurance_growth_pct);
	}

	// Every category the handler can derive must exist, and its ladder must
	// cover +1..+cap with no holes — a missing step reads as "maxed" and
	// would cap the item short of its own row.
	for (uint8_t category = enchant_category::weapon; category <= enchant_category::crafted_weapon; category++)
	{
		const auto* row = config.find_enchant_category(category);
		if (row == nullptr)
		{
			v.add("enchant_categories: category {} missing (categories {}..{} are required)",
				(int)category, (int)enchant_category::weapon, (int)enchant_category::crafted_weapon);
			continue;
		}
		for (int step = 1; step <= row->cap; step++)
			if (config.find_enchant_step(category, step) == nullptr)
				v.add("enchant_steps category {} '{}': no row for step {} (cap is {})",
					(int)category, row->name, step, row->cap);
	}

	std::set<std::pair<uint8_t, int>> seen;
	for (const auto& step : config.enchant_steps)
	{
		if (categories.find(step.category) == categories.end())
			v.add("enchant_steps category {} step {}: category not in enchant_categories", (int)step.category, step.step);
		if (step.step < 1)
			v.add("enchant_steps category {} step {}: step must be >= 1", (int)step.category, step.step);
		if (step.success_pct < 0 || step.success_pct > 10000)
			v.add("enchant_steps category {} step {}: success_pct {} outside 0..10000 basis points",
				(int)step.category, step.step, step.success_pct);
		if (!seen.insert({ step.category, step.step }).second)
			v.add("enchant_steps category {} step {}: duplicate row", (int)step.category, step.step);
	}
}

// Legacy Roll strategy tables: pool sanity plus the Unified-ID collision
// sweep — one modifier appearing twice in a slot double-counts weight, and
// appearing in both slots lets one item roll the same modifier twice.
void check_attribute_pools(const tier_config& config, validation_state& v)
{
	std::set<int> seen;
	for (const auto& pool : config.attribute_pools)
	{
		if (!seen.insert(pool.pool_id).second)
			v.add("attribute_pools pool {}: duplicate pool_id", pool.pool_id);
		if (pool.secondary_chance < 0 || pool.secondary_chance > 100)
			v.add("attribute_pools pool {} '{}': secondary_chance {} outside 0..100", pool.pool_id, pool.name, pool.secondary_chance);

		std::set<uint8_t> slot_ids[2];
		for (int slot = 0; slot < 2; slot++)
		{
			const bool secondary = slot == 1;
			const char* slot_name = secondary ? "secondary" : "prefix";
			for (const auto& entry : secondary ? pool.secondary_entries : pool.prefix_entries)
			{
				if (entry.weight <= 0)
					v.add("attribute_pool_entries pool {} {} modifier {}: weight {} must be positive",
						pool.pool_id, slot_name, (int)entry.modifier_id, entry.weight);
				if (config.find_modifier(entry.modifier_id) == nullptr)
					v.add("attribute_pool_entries pool {} {} modifier {}: modifier not in modifier_catalog",
						pool.pool_id, slot_name, (int)entry.modifier_id);
				if (!slot_ids[slot].insert(entry.modifier_id).second)
					v.add("attribute_pool_entries pool {} {}: unified modifier {} appears twice (ID collision)",
						pool.pool_id, slot_name, (int)entry.modifier_id);
			}
		}
		for (uint8_t id : slot_ids[0])
			if (slot_ids[1].count(id) != 0)
				v.add("attribute_pool_entries pool {}: unified modifier {} in both prefix and secondary slots (one item could roll it twice)",
					pool.pool_id, (int)id);
	}
}

// Legacy-only cross-checks against the running item/drop datasets: every
// item pool reference must resolve, and droppable tier-scope gear that the
// legacy roll gate would accept must carry a pool (spec §2).
void check_legacy_item_coverage(const tier_config& config,
	const tier_validation_context& context, validation_state& v)
{
	for (int id = 0; id < context.item_config_count; id++)
	{
		const CItem* item = item_config(context, id);
		if (item == nullptr || item->m_attribute_pool_id <= 0) continue;
		if (config.find_attribute_pool(item->m_attribute_pool_id) == nullptr)
			v.add("items id {} '{}': attribute_pool_id {} not in attribute_pools",
				id, item->m_name, item->m_attribute_pool_id);
	}

	if (config.item_system != item_system_mode::legacy) return;
	if (context.drop_tables == nullptr) return;

	std::set<int> droppable;
	for (const auto& [table_id, table] : *context.drop_tables)
		for (const auto& entry : table.entries)
			droppable.insert(entry.item_id);

	for (int id : droppable)
	{
		const CItem* item = ordinary_tier_gear(context, id);
		if (item == nullptr) continue;
		if (!is_legacy_rollable_effect_type(item->get_item_effect_type())) continue;
		if (item->m_attribute_pool_id <= 0)
			v.add("items id {} '{}': droppable tier-scope gear with no attribute_pool_id (legacy roll can never roll it)",
				id, item->m_name);
	}
}

// Legacy-only: the catalog's replicated multiplier scales tooltip display,
// the legacy tables' multiplier scales the actual roll — divergence means
// tooltips lie about rolls (2-B handoff: the validator owns this check).
void check_legacy_multipliers(const tier_config& config,
	const tier_validation_context& context, validation_state& v)
{
	if (config.item_system != item_system_mode::legacy) return;

	auto check = [&](uint8_t id, uint8_t legacy_multiplier, const char* table)
	{
		const auto* row = config.find_modifier(id);
		if (row != nullptr && row->multiplier != legacy_multiplier)
			v.add("modifier_catalog id {} '{}': multiplier {} != {} multiplier {} (tooltips would scale differently than rolls)",
				(int)id, row->name, (int)row->multiplier, table, (int)legacy_multiplier);
	};
	if (context.attribute_prefix_types != nullptr)
		for (const auto& e : *context.attribute_prefix_types)
			check(e.prefix_id, e.multiplier, "attribute_prefix_types");
	if (context.attribute_secondary_types != nullptr)
		for (const auto& e : *context.attribute_secondary_types)
			check(e.secondary_id, e.multiplier, "attribute_secondary_types");
}

// Tiered-only: empty tiered tables block boot — the intended
// unconfigured-world guard (spec §2) — plus the fixed rows the tiered
// roll depends on: the four tier curves, the five grades, and the
// presentation table.
void check_tiered_required_data(const tier_config& config, validation_state& v)
{
	if (config.buckets.empty()) v.add("tier_buckets: empty - tiered mode requires seeded data");
	if (config.eligibility.empty()) v.add("modifier_eligibility: empty - tiered mode requires seeded data");
	if (config.curves.empty()) v.add("tier_curves: empty - tiered mode requires seeded data");
	if (config.loot_grades.empty()) v.add("loot_grades: empty - tiered mode requires seeded data");
	if (config.settings.empty()) v.add("tier_settings: empty - tiered mode requires seeded data");
	// The enchant tables are required in BOTH modes — check_enchant_tables.

	// Checked through the load-time tier -> curve binding (find_tier_curve)
	// so name matching has exactly one implementation.
	if (!config.curves.empty())
		for (uint8_t tier = 1; tier <= tier_count; tier++)
			if (config.find_tier_curve(tier) == nullptr)
				v.add("tier_curves: no '{}' curve (the four tier curves are required)", tier_curve_names[tier - 1]);

	if (!config.loot_grades.empty())
		for (uint8_t grade = 1; grade <= loot_grade_count; grade++)
			if (config.find_loot_grade(grade) == nullptr)
				v.add("loot_grades: grade {} missing (grades 1..{} are required)", (int)grade, (int)loot_grade_count);

	for (int tier = 1; tier <= tier_count; tier++)
		if (config.presentation[tier - 1].name.empty())
			v.add("tier_presentation tier {}: missing or unnamed row", tier);
	if (config.tier_name_template.empty())
		v.add("tier_presentation: empty name_template");
}

// Tiered-only: every npc's loot grade must resolve to a loot_grades row —
// the grade IS the whole drop-economics dial (spec §8).
void check_npc_loot_grades(const tier_config& config,
	const tier_validation_context& context, validation_state& v)
{
	if (context.npc_configs == nullptr) return;
	for (int id = 0; id < context.npc_config_count; id++)
	{
		const CNpc* npc = context.npc_configs[id];
		if (npc == nullptr) continue;
		if (config.find_loot_grade(static_cast<uint8_t>(npc->m_loot_grade)) == nullptr)
			v.add("npc_configs id {} '{}': loot_grade {} not in loot_grades", id, npc->m_npc_name, npc->m_loot_grade);
	}
}

// Tiered-only: every gear class must be able to fill count == tier at
// every tier — t distinct Buckets with an eligible modifier rollable at
// tier t (min-tier ladder applied). Venue-independent by design: the §3
// invariant is a property of the eligibility/catalog dataset alone, and
// GM minting (3-G) reaches classes no drop table currently holds. This is
// what lets the 3-B roll never degrade a rolled tier.
void check_class_fillability(const tier_config& config, validation_state& v)
{
	for (uint8_t item_class = tier_item_class::melee_weapon;
		item_class <= tier_item_class::cape; item_class++)
	{
		// One eligibility pass per class: each Bucket's lowest rollable tier.
		std::map<uint8_t, uint8_t> bucket_min_tier;
		for (const auto& e : config.eligibility)
		{
			if (e.item_class != item_class) continue;
			const auto* modifier = config.find_modifier(e.modifier_id);
			if (modifier == nullptr) continue;
			auto [it, inserted] = bucket_min_tier.try_emplace(modifier->bucket_id, modifier->min_tier);
			if (!inserted && modifier->min_tier < it->second) it->second = modifier->min_tier;
		}

		for (int tier = 1; tier <= tier_count; tier++)
		{
			int buckets = 0;
			for (const auto& [bucket_id, min_tier] : bucket_min_tier)
				if (min_tier <= tier) buckets++;
			if (buckets < tier)
				v.add("modifier_eligibility class {}: only {} bucket(s) rollable at tier {} - gear of this class cannot satisfy count == tier",
					(int)item_class, buckets, tier);
		}
	}
}

// Tiered-only §8 stage-2 rule: stage-2 drop tables hold uniques, specials
// and consumables only — ordinary tier-eligible gear must live in stage 1
// so "hunted gear is always tiered" holds structurally. This reads the
// table's DECLARED stage, which is the only thing that column is for: the
// roll, the rate model and the multipliers never branch on it.
void check_stage2_gear(const tier_validation_context& context, validation_state& v)
{
	if (context.drop_tables == nullptr) return;
	for (const auto& [table_id, table] : *context.drop_tables)
	{
		if (table.stage != 2) continue;
		for (const auto& entry : table.entries)
		{
			const CItem* item = ordinary_tier_gear(context, entry.item_id);
			if (item == nullptr) continue;
			v.add("drop_entries table {} '{}' (stage 2): item {} '{}' is tier-eligible gear (stage-2 tables hold uniques/specials/consumables only)",
				table_id, table.name, entry.item_id, item->m_name);
		}
	}
}

// The guaranteed head and the tail penalty (#89). The head is what puts a floor
// under a scatter boss's yield, so every way of writing it wrong silently costs
// items — a guarantee naming an item with no row would substitute nothing at
// all, and a head longer than the roll count would promise rolls that never run.
void check_drop_guarantee(const tier_validation_context& context,
	int table_id, const drop_table& table, validation_state& v)
{
	// The guarantee's payload row: 0 ppb, present for its count spec alone.
	const drop_entry* payload = nullptr;
	for (const auto& entry : table.entries)
		if (entry.item_id == table.guarantee_item_id) payload = &entry;

	if (table.guaranteed_rolls < 0)
		v.add("drop_tables {} '{}': guaranteed_rolls {} is negative",
			table_id, table.name, table.guaranteed_rolls);
	else if (table.guaranteed_rolls > table.roll_count_min)
		v.add("drop_tables {} '{}': guaranteed_rolls {} exceeds roll_count_min {} - the guarantee would cover rolls that never happen",
			table_id, table.name, table.guaranteed_rolls, table.roll_count_min);

	// The two halves must agree, both ways round: a head with nothing to pay out
	// silently yields nothing, and an item nominated with no head never pays.
	if (table.guaranteed_rolls > 0 && table.guarantee_item_id == 0)
		v.add("drop_tables {} '{}': guaranteed_rolls {} with no guarantee_item_id - the head has nothing to pay out",
			table_id, table.name, table.guaranteed_rolls);
	if (table.guarantee_item_id != 0)
	{
		if (table.guaranteed_rolls == 0)
			v.add("drop_tables {} '{}': guarantee_item_id {} with guaranteed_rolls 0 - the item can never be paid out",
				table_id, table.name, table.guarantee_item_id);
		if (item_config(context, table.guarantee_item_id) == nullptr)
			v.add("drop_tables {} '{}': guarantee_item_id {} is not in item_configs",
				table_id, table.name, table.guarantee_item_id);
		if (payload == nullptr)
			v.add("drop_tables {} '{}': guarantee_item_id {} has no drop_entries row - the row is where its count is authored, so the head would pay out a single unit",
				table_id, table.name, table.guarantee_item_id);
		else if (payload->chance_ppb != 0)
			v.add("drop_entries table {} '{}' item {}: the guarantee item carries {} ppb - it must be 0, or it competes on the tail rolls too, where the guarantee does not apply",
				table_id, table.name, table.guarantee_item_id, payload->chance_ppb);
	}

	// A 0-ppb row anywhere else is a row that can never be won.
	for (const auto& entry : table.entries)
		if (entry.chance_ppb == 0 && entry.item_id != table.guarantee_item_id)
			v.add("drop_entries table {} '{}' item {}: 0 ppb but not this table's guarantee item - the row can never be won",
				table_id, table.name, entry.item_id);

	if (table.tail_rarity_divisor < 1)
		v.add("drop_tables {} '{}': tail_rarity_divisor {} is under 1 - a divisor cannot make the tail more likely than the head",
			table_id, table.name, table.tail_rarity_divisor);
	else if (table.tail_rarity_divisor > 1 && table.guaranteed_rolls == 0)
		v.add("drop_tables {} '{}': tail_rarity_divisor {} with guaranteed_rolls 0 - every roll is a tail roll, so the divisor quietly divides the whole table",
			table_id, table.name, table.tail_rarity_divisor);
}

// The #73 drop-table rules, both modes. Structure is an error; a table a live
// multiplier cannot move is reported by name but is NOT an error — it is
// deliberate for the five bosses' guaranteed stage-2 tables, and a defect at
// stage 1 (#87). The validator's job is to make sure it is never a surprise.
void check_drop_tables(const tier_validation_context& context,
	const std::map<int, drop_table>* tables, validation_state& v)
{
	if (context.drop_table_anomalies != nullptr)
		for (const auto& anomaly : *context.drop_table_anomalies)
			v.add("{}", anomaly);
	if (tables == nullptr) return;

	for (const auto& [table_id, table] : *tables)
	{
		if (table.stage < 1 || table.stage > drop_multipliers::max_stage)
			v.add("drop_tables {} '{}': stage {} is not 1 or 2",
				table_id, table.name, table.stage);
		if (table.roll_count_min < 1 || table.roll_count_max < table.roll_count_min)
			v.add("drop_tables {} '{}': roll_count {}..{} is not a positive range",
				table_id, table.name, table.roll_count_min, table.roll_count_max);
		if (table.entries.empty())
		{
			v.add("drop_tables {} '{}': no rows - a table that can never yield anything",
				table_id, table.name);
			continue;
		}

		// Authored sums are compared as EXACT integers. That is the whole
		// reason the column is ppb rather than the "1 in N" it is authored in:
		// "rows sum to exactly 1.0" is what makes a drop guaranteed, and a
		// float tolerance cannot carry it.
		if (table.total_ppb > drop_chance_denominator)
			v.add("drop_tables {} '{}': rows sum to {} ppb, over the {} denominator - the table is malformed, not merely saturated",
				table_id, table.name, table.total_ppb, drop_chance_denominator);
		else if (table.total_ppb == drop_chance_denominator)
			v.add_note("drop_tables {} '{}' (stage {}): rows sum to exactly 1.0 - something drops on every roll, so a generosity multiplier is a no-op here",
				table_id, table.name, table.stage);

		check_drop_guarantee(context, table_id, table, v);

		std::set<int> seen;
		for (const auto& entry : table.entries)
		{
			if (!seen.insert(entry.item_id).second)
				v.add("drop_entries table {} '{}': item {} appears twice",
					table_id, table.name, entry.item_id);
			if (item_config(context, entry.item_id) == nullptr)
				v.add("drop_entries table {} '{}': item {} is not in item_configs",
					table_id, table.name, entry.item_id);
			if (entry.min_count < 0 || entry.max_count < entry.min_count)
				v.add("drop_entries table {} '{}' item {}: count {}..{} is not a valid range",
					table_id, table.name, entry.item_id, entry.min_count, entry.max_count);
			if (entry.count_throws < 1)
				v.add("drop_entries table {} '{}' item {}: count_throws {} is under 1 - a row that can never produce a stack",
					table_id, table.name, entry.item_id, entry.count_throws);
		}
	}
}

// Every monster's stage-1 rows must sum to under 1.0, so the stage-1
// multiplier can actually move them. Saturation is correct for a guaranteed
// SECOND drop and wrong for a first one — the same number with opposite
// verdicts, which is why the two slots are checked separately (#87).
void check_npc_drop_slots(const tier_validation_context& context,
	const std::map<int, drop_table>* tables, validation_state& v)
{
	if (tables == nullptr || context.npc_configs == nullptr) return;

	for (int id = 0; id < context.npc_config_count; id++)
	{
		const CNpc* npc = context.npc_configs[id];
		if (npc == nullptr) continue;

		const int slots[2] = { npc->m_stage1_table_id, npc->m_stage2_table_id };
		for (int slot = 0; slot < 2; slot++)
		{
			if (slots[slot] == 0) continue;
			const auto found = tables->find(slots[slot]);
			if (found == tables->end())
			{
				v.add("npc_configs id {} '{}': stage{}_table_id {} is not in drop_tables",
					id, npc->m_npc_name, slot + 1, slots[slot]);
				continue;
			}
			// A table authored for one slot and referenced from the other is
			// legal — one engine, and the slot is the only meaning of stage —
			// but it silently changes which multiplier moves it and whether it
			// tier-rolls, so it is worth naming.
			if (found->second.stage != slot + 1)
				v.add_note("npc_configs id {} '{}': stage{}_table_id {} '{}' is authored for stage {}",
					id, npc->m_npc_name, slot + 1, slots[slot],
					found->second.name, found->second.stage);
			// Saturation WARNS rather than blocking boot. Whether it is a defect
			// is a data question the log makes askable: it is the design for a
			// guaranteed SECOND drop and an accident for a first one (#87). A
			// table that is genuinely malformed - rows summing past 1.0 - is
			// the error, and check_drop_tables raises it.
			if (slot == 0 && found->second.total_ppb >= drop_chance_denominator &&
				npc_type_never_drops(npc->m_type) == false)
				v.add_note("npc_configs id {} '{}': stage-1 table {} '{}' is saturated (rows sum to 1.0), so the stage-1 multiplier can never move it - stage 1 was never meant to be a certainty (#87)",
					id, npc->m_npc_name, slots[slot], found->second.name);
		}
	}
}

} // namespace

void log_tier_validation_errors(const std::vector<std::string>& errors)
{
	for (const auto& error : errors)
		hb::logger::error("tier validator: {}", error);
}

std::string validate_tiered_instance(const tier_config& config,
	const hb::shared::item::item_attribute_data& attributes)
{
	using namespace hb::shared::item;

	const int tier = attributes.tier;
	if (tier < 1 || tier > tier_count)
		return std::format("tier {} outside 1..{}", tier, (int)tier_count);
	if (!attributes.tier_invariant_ok())
		return std::format("modifier count {} != tier {}", (int)attributes.modifier_count(), tier);

	bool bucket_seen[256] = {};
	for (const auto& mod : attributes.modifiers)
	{
		if (mod.type == 0) continue;
		const auto* row = config.find_modifier(mod.type);
		if (row == nullptr)
			return std::format("modifier {} not in catalog", (int)mod.type);
		if (row->min_tier > tier)
			return std::format("modifier {} min_tier {} above tier {}", (int)mod.type, (int)row->min_tier, tier);
		if (bucket_seen[row->bucket_id])
			return std::format("two modifiers from bucket {}", (int)row->bucket_id);
		bucket_seen[row->bucket_id] = true;

		const bool pair = row->effect_id == effect_id::add_attribute_pair;
		if (!pair && mod.value2 != 0)
			return std::format("modifier {} has value2 {} but is not a pair", (int)mod.type, (int)mod.value2);
		if (row->multiplier == 0)
		{
			if (mod.value != 0)
				return std::format("value-less modifier {} rolled value {}", (int)mod.type, (int)mod.value);
			continue;
		}

		const modifier_window range = row->display_range(static_cast<uint8_t>(tier));
		const int display = mod.value * row->multiplier;
		if (display < range.min || display > range.max)
			return std::format("modifier {} value {} (display {}) outside {}..{}",
				(int)mod.type, (int)mod.value, display, range.min, range.max);
		if (pair)
		{
			const int display2 = mod.value2 * row->multiplier;
			if (display2 < range.min || display2 > range.max)
				return std::format("modifier {} value2 {} (display {}) outside {}..{}",
					(int)mod.type, (int)mod.value2, display2, range.min, range.max);
		}
	}
	return {};
}

std::vector<std::string> validate_tier_config(const tier_config& config,
	const tier_validation_context& context, std::vector<std::string>* notes)
{
	// Row anomalies the loader had to skip (unresolvable references) are
	// dataset errors here — the rows exist in the DB but not in the model.
	validation_state v{ config.load_anomalies };

	check_anticheat_settings(config, v);
	check_buckets(config, v);
	check_catalog(config, v);
	check_eligibility(config, v);
	check_bucket_rules(config, v);
	check_curves(config, v);
	check_loot_grades(config, v);
	check_reputation_layer(config, v);
	check_enchant_tables(config, v);
	check_attribute_pools(config, v);
	check_legacy_item_coverage(config, context, v);
	check_legacy_multipliers(config, context, v);
	check_drop_tables(context, context.drop_tables, v);
	check_npc_drop_slots(context, context.drop_tables, v);

	if (config.item_system == item_system_mode::tiered)
	{
		check_tiered_required_data(config, v);
		check_npc_loot_grades(config, context, v);
		check_class_fillability(config, v);
		check_stage2_gear(context, v);
	}

	if (notes != nullptr) *notes = std::move(v.notes);
	return v.errors;
}

void log_tier_validation_notes(const std::vector<std::string>& notes)
{
	for (const auto& note : notes)
		hb::logger::warn("drop tables: {}", note);
}

} // namespace hb::server
