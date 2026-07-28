// TierConfigStore.cpp: gamedata.db -> tier_config loaders (Tiers 2-B)
//
//////////////////////////////////////////////////////////////////////

#include "TierConfigStore.h"

#include <algorithm>
#include <utility>

#include "sqlite3.h"
#include "Log.h"
#include "StringCompat.h"

namespace hb::server
{

namespace
{

std::string column_text(sqlite3_stmt* stmt, int col)
{
	const unsigned char* text = sqlite3_column_text(stmt, col);
	return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

// Prepares `sql`, invokes `row(stmt)` per result row, finalizes. False
// (with a log naming `table`) only on prepare failure.
template <typename F>
bool for_each_row(sqlite3* db, const char* sql, const char* table, F&& row)
{
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
	{
		hb::logger::error("tier config: failed to prepare {} query: {}", table, sqlite3_errmsg(db));
		return false;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
		row(stmt);
	sqlite3_finalize(stmt);
	return true;
}

bool load_item_system(sqlite3* db, tier_config& config)
{
	// Missing row = legacy: the DDL INSERT OR IGNOREs the key, so absence
	// only happens on a hand-stripped DB and legacy is the safe reading.
	bool recognized = true;
	bool ok = for_each_row(db,
		"SELECT value FROM meta WHERE key = 'item_system';", "meta.item_system",
		[&](sqlite3_stmt* stmt)
	{
		std::string value = column_text(stmt, 0);
		if (hb_stricmp(value.c_str(), "legacy") == 0)
			config.item_system = hb::shared::item::item_system_mode::legacy;
		else if (hb_stricmp(value.c_str(), "tiered") == 0)
			config.item_system = hb::shared::item::item_system_mode::tiered;
		else
		{
			hb::logger::error("tier config: meta.item_system is '{}' - expected 'legacy' or 'tiered'", value);
			recognized = false;
		}
	});
	return ok && recognized;
}

bool load_buckets(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT bucket_id, name, sort_order FROM tier_buckets ORDER BY bucket_id;",
		"tier_buckets",
		[&](sqlite3_stmt* stmt)
	{
		tier_bucket_config row{};
		row.bucket_id  = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.name       = column_text(stmt, 1);
		row.sort_order = sqlite3_column_int(stmt, 2);
		config.buckets.push_back(std::move(row));
	});
}

bool load_catalog(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT modifier_id, name, display_name, effect_label, effect_format,"
		" effect_id, effect_param1, effect_param2, bucket_id, multiplier,"
		" min_tier, marquee, band_min, band_max, aggregate_cap,"
		" window_min_t1, window_max_t1, window_min_t2, window_max_t2,"
		" window_min_t3, window_max_t3, window_min_t4, window_max_t4"
		" FROM modifier_catalog ORDER BY modifier_id;",
		"modifier_catalog",
		[&](sqlite3_stmt* stmt)
	{
		int c = 0;  // consumed in SELECT order
		modifier_catalog_config row{};
		row.modifier_id   = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
		row.name          = column_text(stmt, c++);
		row.display_name  = column_text(stmt, c++);
		row.effect_label  = column_text(stmt, c++);
		row.effect_format = column_text(stmt, c++);
		row.effect_id     = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
		row.effect_param1 = sqlite3_column_int(stmt, c++);
		row.effect_param2 = sqlite3_column_int(stmt, c++);
		row.bucket_id     = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
		row.multiplier    = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
		row.min_tier      = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
		row.marquee       = sqlite3_column_int(stmt, c++) != 0;
		row.band_min      = sqlite3_column_int(stmt, c++);
		row.band_max      = sqlite3_column_int(stmt, c++);
		row.aggregate_cap = sqlite3_column_int(stmt, c++);
		for (int t = 0; t < hb::shared::item::tier_count; t++)
		{
			int minCol = c++;
			int maxCol = c++;
			if (sqlite3_column_type(stmt, minCol) != SQLITE_NULL &&
				sqlite3_column_type(stmt, maxCol) != SQLITE_NULL)
			{
				row.windows[t] = modifier_window{
					sqlite3_column_int(stmt, minCol),
					sqlite3_column_int(stmt, maxCol) };
			}
		}
		config.catalog.push_back(std::move(row));
	});
}

bool load_eligibility(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT modifier_id, item_class, weight FROM modifier_eligibility"
		" ORDER BY modifier_id, item_class;",
		"modifier_eligibility",
		[&](sqlite3_stmt* stmt)
	{
		modifier_eligibility_config row{};
		row.modifier_id = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.item_class  = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
		row.weight      = sqlite3_column_int(stmt, 2);
		config.eligibility.push_back(row);
	});
}

bool load_bucket_rules(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT bucket_id, item_class, mandatory FROM bucket_class_rules"
		" ORDER BY bucket_id, item_class;",
		"bucket_class_rules",
		[&](sqlite3_stmt* stmt)
	{
		bucket_class_rule_config row{};
		row.bucket_id  = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.item_class = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
		row.mandatory  = sqlite3_column_int(stmt, 2) != 0;
		config.bucket_rules.push_back(row);
	});
}

bool load_curves(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT curve_id, name, p50, p90, p99, cap_chance_den FROM tier_curves"
		" ORDER BY curve_id;",
		"tier_curves",
		[&](sqlite3_stmt* stmt)
	{
		tier_curve_config row{};
		row.curve_id       = sqlite3_column_int(stmt, 0);
		row.name           = column_text(stmt, 1);
		row.p50            = sqlite3_column_double(stmt, 2);
		row.p90            = sqlite3_column_double(stmt, 3);
		row.p99            = sqlite3_column_double(stmt, 4);
		row.cap_chance_den = sqlite3_column_int(stmt, 5);
		config.curves.push_back(std::move(row));
	});
}

bool load_curve_overrides(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT modifier_id, tier, cap_chance_den FROM tier_curve_overrides"
		" ORDER BY modifier_id, tier;",
		"tier_curve_overrides",
		[&](sqlite3_stmt* stmt)
	{
		tier_curve_override_config row{};
		row.modifier_id    = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.tier           = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
		row.cap_chance_den = sqlite3_column_int(stmt, 2);
		config.curve_overrides.push_back(row);
	});
}

bool load_loot_grades(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT grade, name, weight_common, weight_rare, weight_epic,"
		" weight_legendary, first_drop_chance FROM loot_grades ORDER BY grade;",
		"loot_grades",
		[&](sqlite3_stmt* stmt)
	{
		loot_grade_config row{};
		row.grade             = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.name              = column_text(stmt, 1);
		row.weight_common     = sqlite3_column_int(stmt, 2);
		row.weight_rare       = sqlite3_column_int(stmt, 3);
		row.weight_epic       = sqlite3_column_int(stmt, 4);
		row.weight_legendary  = sqlite3_column_int(stmt, 5);
		row.first_drop_chance = sqlite3_column_int(stmt, 6);
		config.loot_grades.push_back(std::move(row));
	});
}

bool load_enchant_categories(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT category, name, cap, endurance_growth_pct FROM enchant_categories"
		" ORDER BY category;",
		"enchant_categories",
		[&](sqlite3_stmt* stmt)
	{
		enchant_category_config row{};
		row.category             = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.name                 = column_text(stmt, 1);
		row.cap                  = sqlite3_column_int(stmt, 2);
		row.endurance_growth_pct = sqlite3_column_int(stmt, 3);
		config.enchant_categories.push_back(std::move(row));
	});
}

bool load_enchant_steps(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT category, step, success_pct, destroy_on_fail FROM enchant_steps"
		" ORDER BY category, step;",
		"enchant_steps",
		[&](sqlite3_stmt* stmt)
	{
		enchant_step_config row{};
		row.category        = static_cast<uint8_t>(sqlite3_column_int(stmt, 0));
		row.step            = sqlite3_column_int(stmt, 1);
		row.success_pct     = sqlite3_column_int(stmt, 2);
		row.destroy_on_fail = sqlite3_column_int(stmt, 3) != 0;
		config.enchant_steps.push_back(row);
	});
}

bool load_presentation(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT tier, name, color_r, color_g, color_b, name_template"
		" FROM tier_presentation ORDER BY tier;",
		"tier_presentation",
		[&](sqlite3_stmt* stmt)
	{
		int tier = sqlite3_column_int(stmt, 0);
		if (tier < 1 || tier > hb::shared::item::tier_count)
		{
			hb::logger::warn("tier config: tier_presentation row {} outside 1..{} - skipped",
				tier, (int)hb::shared::item::tier_count);
			return;
		}
		auto& row = config.presentation[tier - 1];
		row.name = column_text(stmt, 1);
		row.r = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
		row.g = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
		row.b = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
		// One global template on the wire; row 1's value is authoritative.
		if (tier == 1)
			config.tier_name_template = column_text(stmt, 5);
	});
}

bool load_settings(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT key, value FROM tier_settings;", "tier_settings",
		[&](sqlite3_stmt* stmt)
	{
		config.settings[column_text(stmt, 0)] = column_text(stmt, 1);
	});
}

bool load_attribute_pools(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT pool_id, name, secondary_chance FROM attribute_pools"
		" ORDER BY pool_id;",
		"attribute_pools",
		[&](sqlite3_stmt* stmt)
	{
		attribute_pool_config row{};
		row.pool_id          = sqlite3_column_int(stmt, 0);
		row.name             = column_text(stmt, 1);
		row.secondary_chance = sqlite3_column_int(stmt, 2);
		config.attribute_pools.push_back(std::move(row));
	});
}

bool load_attribute_pool_entries(sqlite3* db, tier_config& config)
{
	return for_each_row(db,
		"SELECT pool_id, is_secondary, type_id, weight FROM attribute_pool_entries"
		" ORDER BY pool_id, is_secondary, type_id;",
		"attribute_pool_entries",
		[&](sqlite3_stmt* stmt)
	{
		int pool_id       = sqlite3_column_int(stmt, 0);
		bool is_secondary = sqlite3_column_int(stmt, 1) != 0;
		uint8_t type_id   = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
		int weight        = sqlite3_column_int(stmt, 3);

		auto pool = std::find_if(config.attribute_pools.begin(), config.attribute_pools.end(),
			[pool_id](const attribute_pool_config& p) { return p.pool_id == pool_id; });
		if (pool == config.attribute_pools.end())
		{
			hb::logger::warn("tier config: attribute_pool_entries references unknown pool {} - skipped", pool_id);
			return;
		}

		// Rows are keyed by the legacy prefix/secondary type ids; translate
		// to unified modifier IDs here at the load boundary (ModifierIds.h).
		uint8_t modifier = is_secondary
			? hb::shared::item::legacy_secondary_to_modifier_id(type_id)
			: hb::shared::item::legacy_prefix_to_modifier_id(type_id);
		if (modifier == hb::shared::item::modifier_id::empty)
		{
			hb::logger::warn("tier config: attribute_pool_entries pool {} {} type {} maps to no modifier - skipped",
				pool_id, is_secondary ? "secondary" : "prefix", (int)type_id);
			return;
		}

		auto& slot  = is_secondary ? pool->secondary_entries : pool->prefix_entries;
		auto& total = is_secondary ? pool->secondary_total_weight : pool->prefix_total_weight;
		slot.push_back({ modifier, weight });
		total += weight;
	});
}

} // namespace

const modifier_catalog_config* tier_config::find_modifier(uint8_t modifier_id) const
{
	auto it = std::find_if(catalog.begin(), catalog.end(),
		[modifier_id](const modifier_catalog_config& row) { return row.modifier_id == modifier_id; });
	return it != catalog.end() ? &*it : nullptr;
}

const attribute_pool_config* tier_config::find_attribute_pool(int pool_id) const
{
	auto it = std::find_if(attribute_pools.begin(), attribute_pools.end(),
		[pool_id](const attribute_pool_config& row) { return row.pool_id == pool_id; });
	return it != attribute_pools.end() ? &*it : nullptr;
}

bool load_tier_config(sqlite3* db, tier_config& out)
{
	if (db == nullptr) return false;

	tier_config fresh;
	bool ok = load_item_system(db, fresh)
		&& load_buckets(db, fresh)
		&& load_catalog(db, fresh)
		&& load_eligibility(db, fresh)
		&& load_bucket_rules(db, fresh)
		&& load_curves(db, fresh)
		&& load_curve_overrides(db, fresh)
		&& load_loot_grades(db, fresh)
		&& load_enchant_categories(db, fresh)
		&& load_enchant_steps(db, fresh)
		&& load_presentation(db, fresh)
		&& load_settings(db, fresh)
		&& load_attribute_pools(db, fresh)
		&& load_attribute_pool_entries(db, fresh);
	if (!ok) return false;

	if (fresh.catalog.empty())
		hb::logger::warn("tier config: modifier_catalog is empty - item tooltips will have no labels");

	int pool_entries = 0;
	for (const auto& pool : fresh.attribute_pools)
		pool_entries += (int)(pool.prefix_entries.size() + pool.secondary_entries.size());

	hb::logger::log("Tier config loaded ({} mode): {} catalog rows, {} buckets, {} eligibility rows, "
		"{} curves, {} grades, {} enchant steps, {} settings; legacy pools: {} pools, {} entries",
		fresh.item_system == hb::shared::item::item_system_mode::tiered ? "tiered" : "legacy",
		(int)fresh.catalog.size(), (int)fresh.buckets.size(), (int)fresh.eligibility.size(),
		(int)fresh.curves.size(), (int)fresh.loot_grades.size(), (int)fresh.enchant_steps.size(),
		(int)fresh.settings.size(), (int)fresh.attribute_pools.size(), pool_entries);

	out = std::move(fresh);
	return true;
}

} // namespace hb::server
