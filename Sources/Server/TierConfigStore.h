// TierConfigStore.h: the Item Tiers config model (Tiers 2-B)
//
// Loads every tiered table plus the legacy attribute-pool tables from
// gamedata.db into one immutable in-memory aggregate at boot (and on
// `reload catalog`). The replicated modifier catalog / tier presentation
// stream is sourced from this model; both Roll strategies read their
// datasets from it (legacy pools consumed from Tiers 2-C, tiered rolls
// from Tiers 3-B). Deep validation is the Tiers 2-D boot validator —
// this store only fails on SQL errors and an unreadable item_system.
//
// Design contract: PLANS/ItemTiers_Plan.md §2; plan cycle 2-B.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Item/ModifierIds.h"

struct sqlite3;

namespace hb::server
{

struct tier_bucket_config
{
	uint8_t bucket_id;
	std::string name;
	int sort_order;
};

// Per-tier value window (display units); absent = full band.
struct modifier_window
{
	int min;
	int max;
};

struct modifier_catalog_config
{
	uint8_t modifier_id;              // unified modifier ID (ModifierIds.h)
	std::string name;                 // internal slug
	std::string display_name;         // replicated: item-name prefix word
	std::string effect_label;         // replicated: tooltip line
	std::string effect_format;        // replicated: value format
	uint8_t effect_id;                // behavior key (effect_id enum)
	int effect_param1;
	int effect_param2;
	uint8_t bucket_id;
	uint8_t multiplier;               // display = stored byte x multiplier
	uint8_t min_tier;
	bool marquee;
	int band_min;                     // display units
	int band_max;
	int aggregate_cap;                // display units
	std::optional<modifier_window> windows[hb::shared::item::tier_count];

	// The display-unit range a roll at `tier` draws from: the per-tier
	// window when data sets one, else the full band ("absent = full band"
	// is owned here — every consumer resolves through this).
	modifier_window display_range(uint8_t tier) const
	{
		const auto& window = windows[tier - 1];
		return window.has_value() ? *window : modifier_window{ band_min, band_max };
	}
};

struct modifier_eligibility_config
{
	uint8_t modifier_id;
	uint8_t item_class;               // tier_item_class enum
	int weight;
};

struct bucket_class_rule_config
{
	uint8_t bucket_id;
	uint8_t item_class;
	bool mandatory;
};

struct tier_curve_config
{
	int curve_id;
	std::string name;
	double p50;                       // normalized band position anchors
	double p90;
	double p99;
	int cap_chance_den;               // P(cap) = 1 / cap_chance_den
};

struct tier_curve_override_config
{
	uint8_t modifier_id;
	uint8_t tier;
	int cap_chance_den;
};

struct loot_grade_config
{
	uint8_t grade;                    // 1..5 vermin/standard/veteran/elite/boss
	std::string name;
	int weight_common;                // out of 10000; zero = staircase hard gate
	int weight_rare;
	int weight_epic;
	int weight_legendary;
	int first_drop_chance;            // out of 10000
};

struct enchant_category_config
{
	uint8_t category;
	std::string name;
	int cap;
	int endurance_growth_pct;
};

struct enchant_step_config
{
	uint8_t category;
	int step;                         // attempting +step
	int success_pct;                  // basis points
	bool destroy_on_fail;
};

struct tier_presentation_config
{
	std::string name;                 // player-facing tier word
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

// Legacy Roll strategy tables (spec §2: each strategy owns its tables).
struct attribute_pool_entry_config
{
	uint8_t modifier_id;              // unified (translated at the load boundary)
	int weight;
};

struct attribute_pool_config
{
	int pool_id;
	std::string name;
	int secondary_chance;             // percent gate for the secondary roll
	// Entries grouped per legacy slot, kept in load order (the cumulative
	// walk order); totals precomputed for the rand() % total selection.
	std::vector<attribute_pool_entry_config> prefix_entries;
	std::vector<attribute_pool_entry_config> secondary_entries;
	int prefix_total_weight = 0;
	int secondary_total_weight = 0;
};

// The whole Item Tiers dataset, loaded once and treated as immutable —
// replaced wholesale by load_tier_config, never edited in place.
struct tier_config
{
	uint8_t item_system = hb::shared::item::item_system_mode::legacy;

	std::vector<tier_bucket_config> buckets;
	std::vector<modifier_catalog_config> catalog;            // ordered by modifier_id
	std::vector<modifier_eligibility_config> eligibility;
	std::vector<bucket_class_rule_config> bucket_rules;
	std::vector<tier_curve_config> curves;
	std::vector<tier_curve_override_config> curve_overrides;
	std::vector<loot_grade_config> loot_grades;
	std::vector<enchant_category_config> enchant_categories;
	std::vector<enchant_step_config> enchant_steps;
	tier_presentation_config presentation[hb::shared::item::tier_count];
	std::string tier_name_template;
	std::map<std::string, std::string> settings;             // tier_settings rows

	// The spec §5 Marquee constants, resolved out of `settings` once at load.
	// Their consumers are per-proc and per-cast paths, which have no business
	// doing a string-keyed lookup and a throwing parse; keeping the launch
	// defaults in this one place also stops a code fallback from silently
	// disagreeing with the seeded row.
	struct marquee_settings_config
	{
		int sunder_defense_delta = -50;
		int sunder_duration_ms = 5000;
		int bleed_tick_damage = 5;
		int bleed_tick_interval_ms = 2000;
		int bleed_duration_ms = 8000;
		int cast_check_floor_ms = 1500;
	} marquee;

	// Legacy pools (consumed by the legacy Roll strategy, Tiers 2-C). The
	// per-item pool assignment lives on CItem (items.attribute_pool_id via
	// LoadItemConfigs) so `reload items` keeps item data and pool ids in step.
	std::vector<attribute_pool_config> attribute_pools;

	// Rows the loader had to skip because they resolve to nothing (unknown
	// pool, unmappable legacy type id, out-of-range presentation tier). The
	// 2-D validator reports these as errors — the DB holds rows the model
	// cannot represent, which fail-fast forbids papering over.
	std::vector<std::string> load_anomalies;

	// Tier -> curves row, bound by name (tier_curve_names) once at load;
	// -1 = that curve is absent (a validator error in tiered mode).
	int curve_index_by_tier[hb::shared::item::tier_count] = { -1, -1, -1, -1 };

	// A tier_settings row as an integer, or `fallback` when the key is absent
	// or unparseable. The 2-D validator refuses an empty tier_settings table in
	// tiered mode, so the fallback only covers legacy-mode worlds and live edits.
	int setting_int(const std::string& key, int fallback) const;

	const modifier_catalog_config* find_modifier(uint8_t modifier_id) const;
	const attribute_pool_config* find_attribute_pool(int pool_id) const;
	const tier_bucket_config* find_bucket(uint8_t bucket_id) const;
	const loot_grade_config* find_loot_grade(uint8_t grade) const;
	const tier_curve_config* find_tier_curve(uint8_t tier) const;
};

// Loads the full dataset and replaces `out` wholesale on success; on
// failure `out` is left untouched (reload keeps the running config).
// False on SQL errors or an unrecognized meta.item_system value — callers
// treat that as a boot/reload failure; empty tables load fine (the 2-D
// validator decides whether an empty dataset may boot).
bool load_tier_config(sqlite3* db, tier_config& out);

} // namespace hb::server
