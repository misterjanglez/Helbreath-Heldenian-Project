// GuildProgressionConfig.cpp: load + validate the guild progression dataset (#122)
//
// See the header for the contract. The loader is deliberately unable to skip:
// anything it cannot represent — an unknown settings key, a non-numeric value,
// a missing required knob — is recorded into load_anomalies, and the validator
// turns every anomaly into a boot stopper.
//
//////////////////////////////////////////////////////////////////////

#include "GuildProgressionConfig.h"

#include <charconv>
#include <format>
#include <set>

#include "GameDatabase.h"
#include "Item/ModifierIds.h"
#include "Log.h"
#include "sqlite3.h"

namespace hb::server
{

namespace
{
	// Locale-free full-consume integer parse; "" and trailing junk both fail
	// (from_chars reports an empty range as invalid on its own).
	bool parse_int64(const std::string& text, int64_t& out)
	{
		const char* first = text.data();
		const char* last = first + text.size();
		auto [end, ec] = std::from_chars(first, last, out);
		return ec == std::errc() && end == last;
	}

	// Write one 0/1 switch, or record why it could not be one — a bool field
	// cannot carry "was 7" to the validator.
	void assign_switch(guild_progression_config& config, const char* key,
		int64_t value, bool& field)
	{
		if (value != 0 && value != 1)
		{
			config.load_anomalies.push_back(std::format(
				"guild_settings '{}': value {} must be 0 or 1", key, value));
			return;
		}
		field = value != 0;
	}

	// The knob vocabulary, stated once: the same rows drive assignment AND
	// the missing-key sweep, so a knob structurally cannot be required but
	// unassignable or vice versa. A new knob is one row here (plus its range
	// rule in the validator, whose per-knob semantics live there).
	struct knob_row
	{
		const char* key;
		void (*assign)(guild_progression_config&, int64_t);
	};

	constexpr knob_row knob_vocabulary[] = {
		{ "donations_enabled", [](guild_progression_config& c, int64_t v)
			{ assign_switch(c, "donations_enabled", v, c.donations_enabled); } },
		{ "title_bonuses_enabled", [](guild_progression_config& c, int64_t v)
			{ assign_switch(c, "title_bonuses_enabled", v, c.title_bonuses_enabled); } },
		{ "title_inactivity_ms", [](guild_progression_config& c, int64_t v)
			{ c.title_inactivity_ms = v; } },
		{ "donation_min_gold", [](guild_progression_config& c, int64_t v)
			{ c.donation_min_gold = v; } },
		{ "donation_min_ek", [](guild_progression_config& c, int64_t v)
			{ c.donation_min_ek = v; } },
		{ "donation_min_contribution", [](guild_progression_config& c, int64_t v)
			{ c.donation_min_contribution = v; } },
		{ "huntmaster_damage_pct", [](guild_progression_config& c, int64_t v)
			{ c.huntmaster_damage_pct = static_cast<int>(v); } },
		{ "huntmaster_tier_shift_pct", [](guild_progression_config& c, int64_t v)
			{ c.huntmaster_tier_shift_pct = static_cast<int>(v); } },
		{ "huntmaster_tier_shift_min_tier", [](guild_progression_config& c, int64_t v)
			{ c.huntmaster_tier_shift_min_tier = static_cast<int>(v); } },
		{ "raidmaster_attack_power", [](guild_progression_config& c, int64_t v)
			{ c.raidmaster_attack_power = static_cast<int>(v); } },
	};

	const knob_row* find_knob(const std::string& key)
	{
		for (const knob_row& row : knob_vocabulary)
		{
			if (key == row.key)
			{
				return &row;
			}
		}
		return nullptr;
	}
}

const guild_level_row* guild_progression_config::find_level(int level) const
{
	// Validated contiguity makes the vector index the level itself.
	if (level < 1 || level > static_cast<int>(levels.size()))
	{
		return nullptr;
	}
	return &levels[level - 1];
}

bool load_guild_progression(sqlite3* db, guild_progression_config& out)
{
	if (db == nullptr)
	{
		return false;
	}

	guild_progression_config fresh;

	{
		sql_query rows(db,
			"SELECT level, gold, enemy_kills, contribution, member_cap,"
			" officer_cap, hunt_slots, raid_slots, repair_pct"
			" FROM guild_level_curve ORDER BY level;",
			"guild progression");
		if (!rows)
		{
			return false;
		}
		while (rows.step())
		{
			guild_level_row row;
			row.level        = static_cast<int>(rows.as_int(0));
			row.gold         = rows.as_int(1);
			row.enemy_kills  = rows.as_int(2);
			row.contribution = rows.as_int(3);
			row.member_cap   = static_cast<int>(rows.as_int(4));
			row.officer_cap  = static_cast<int>(rows.as_int(5));
			row.hunt_slots   = static_cast<int>(rows.as_int(6));
			row.raid_slots   = static_cast<int>(rows.as_int(7));
			row.repair_pct   = static_cast<int>(rows.as_int(8));
			fresh.levels.push_back(row);
		}
	}

	{
		std::set<std::string> seen;
		sql_query rows(db, "SELECT key, value FROM guild_settings;",
			"guild progression");
		if (!rows)
		{
			return false;
		}
		while (rows.step())
		{
			const std::string key = rows.as_text(0);
			const std::string value = rows.as_text(1);
			seen.insert(key);
			const knob_row* knob = find_knob(key);
			if (knob == nullptr)
			{
				fresh.load_anomalies.push_back(std::format(
					"guild_settings: unknown key '{}' (a knob nothing reads silently does nothing)",
					key));
				continue;
			}
			int64_t parsed = 0;
			if (!parse_int64(value, parsed))
			{
				fresh.load_anomalies.push_back(std::format(
					"guild_settings '{}': value '{}' is not an integer", key, value));
				continue;
			}
			knob->assign(fresh, parsed);
		}
		for (const knob_row& knob : knob_vocabulary)
		{
			if (seen.find(knob.key) == seen.end())
			{
				fresh.load_anomalies.push_back(std::format(
					"guild_settings: required key '{}' is missing", knob.key));
			}
		}
	}

	out = std::move(fresh);
	return true;
}

std::vector<std::string> validate_guild_progression(
	const guild_progression_config& config)
{
	std::vector<std::string> errors = config.load_anomalies;

	if (config.levels.empty())
	{
		errors.push_back("guild_level_curve: no rows (the curve defines every"
			" Guild Level; Level 1 must exist to found a guild at all)");
		return errors;
	}

	// Contiguous from 1 — the level engine climbs one row at a time and
	// find_level() indexes by position, so a hole would strand every guild
	// beneath it. Later sweeps assume this indexing; stop here on a breach.
	for (size_t i = 0; i < config.levels.size(); i++)
	{
		if (config.levels[i].level != static_cast<int>(i) + 1)
		{
			errors.push_back(std::format(
				"guild_level_curve: levels must be contiguous from 1 - expected"
				" level {}, found {}", i + 1, config.levels[i].level));
			return errors;
		}
	}

	const guild_level_row& first = config.levels.front();
	if (first.gold != 0 || first.enemy_kills != 0 || first.contribution != 0)
	{
		errors.push_back(std::format(
			"guild_level_curve level 1: requirements {}/{}/{} must all be 0"
			" (a guild is born at Level 1)",
			first.gold, first.enemy_kills, first.contribution));
	}

	for (const guild_level_row& row : config.levels)
	{
		if (row.gold < 0 || row.enemy_kills < 0 || row.contribution < 0)
		{
			errors.push_back(std::format(
				"guild_level_curve level {}: negative requirement"
				" gold {} / enemy_kills {} / contribution {}",
				row.level, row.gold, row.enemy_kills, row.contribution));
		}
		if (row.member_cap < 1)
		{
			errors.push_back(std::format(
				"guild_level_curve level {}: member_cap {} (a guild always"
				" holds at least its master)", row.level, row.member_cap));
		}
		if (row.officer_cap < 0 || row.hunt_slots < 0 || row.raid_slots < 0)
		{
			errors.push_back(std::format(
				"guild_level_curve level {}: negative capacity"
				" officer_cap {} / hunt_slots {} / raid_slots {}",
				row.level, row.officer_cap, row.hunt_slots, row.raid_slots));
		}
		if (row.repair_pct < 0 || row.repair_pct > 100)
		{
			errors.push_back(std::format(
				"guild_level_curve level {}: repair_pct {} outside 0..100",
				row.level, row.repair_pct));
		}
	}

	// Per-column monotonicity. Requirements, because §3.1.2 checks them only
	// on the way up — a later level cheaper than an earlier one could never
	// be the binding gate. Grants, because levels are earned-and-kept — a
	// grant that shrank on level-up would revoke what an earlier level
	// conferred.
	for (size_t i = 1; i < config.levels.size(); i++)
	{
		const guild_level_row& prev = config.levels[i - 1];
		const guild_level_row& row = config.levels[i];
		auto require_non_decreasing = [&](int64_t below, int64_t here,
			const char* column)
		{
			if (here < below)
			{
				errors.push_back(std::format(
					"guild_level_curve level {}: {} {} below level {}'s {}"
					" (every column is cumulative and must never decrease)",
					row.level, column, here, prev.level, below));
			}
		};
		require_non_decreasing(prev.gold, row.gold, "gold");
		require_non_decreasing(prev.enemy_kills, row.enemy_kills, "enemy_kills");
		require_non_decreasing(prev.contribution, row.contribution, "contribution");
		require_non_decreasing(prev.member_cap, row.member_cap, "member_cap");
		require_non_decreasing(prev.officer_cap, row.officer_cap, "officer_cap");
		require_non_decreasing(prev.hunt_slots, row.hunt_slots, "hunt_slots");
		require_non_decreasing(prev.raid_slots, row.raid_slots, "raid_slots");
		require_non_decreasing(prev.repair_pct, row.repair_pct, "repair_pct");
	}

	if (config.title_inactivity_ms <= 0)
	{
		errors.push_back(std::format(
			"guild_settings title_inactivity_ms: {} must be positive"
			" (a zero timer would release every Title on the first sweep)",
			config.title_inactivity_ms));
	}
	if (config.donation_min_gold < 1 || config.donation_min_ek < 1
		|| config.donation_min_contribution < 1)
	{
		errors.push_back(std::format(
			"guild_settings donation minimums: {}/{}/{} must each be at least 1"
			" (§3.1.2 refuses non-positive donations outright)",
			config.donation_min_gold, config.donation_min_ek,
			config.donation_min_contribution));
	}
	if (config.huntmaster_damage_pct < 0 || config.huntmaster_damage_pct > 100)
	{
		errors.push_back(std::format(
			"guild_settings huntmaster_damage_pct: {} outside 0..100",
			config.huntmaster_damage_pct));
	}
	if (config.huntmaster_tier_shift_pct < 100
		|| config.huntmaster_tier_shift_pct > 1000)
	{
		errors.push_back(std::format(
			"guild_settings huntmaster_tier_shift_pct: {} outside 100..1000"
			" (100 = neutral; below 100 would penalize the Title)",
			config.huntmaster_tier_shift_pct));
	}
	if (config.huntmaster_tier_shift_min_tier < 1
		|| config.huntmaster_tier_shift_min_tier > hb::shared::item::tier_count)
	{
		errors.push_back(std::format(
			"guild_settings huntmaster_tier_shift_min_tier: {} outside 1..{}",
			config.huntmaster_tier_shift_min_tier,
			static_cast<int>(hb::shared::item::tier_count)));
	}
	if (config.raidmaster_attack_power < 0 || config.raidmaster_attack_power > 100)
	{
		errors.push_back(std::format(
			"guild_settings raidmaster_attack_power: {} outside 0..100",
			config.raidmaster_attack_power));
	}

	return errors;
}

std::vector<std::string> validate_guild_progression_world(
	const guild_progression_config& config, int highest_guild_level)
{
	std::vector<std::string> errors;
	if (highest_guild_level > config.max_level())
	{
		errors.push_back(std::format(
			"guild_level_curve: a guild already holds Level {} but the curve"
			" ends at {} (levels earned are never revoked - restore the"
			" missing rows)", highest_guild_level, config.max_level()));
	}
	return errors;
}

void log_guild_progression_errors(const std::vector<std::string>& errors)
{
	for (const std::string& error : errors)
	{
		hb::logger::error("guild progression validator: {}", error);
	}
}

} // namespace hb::server
