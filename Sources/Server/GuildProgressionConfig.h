// GuildProgressionConfig.h: the guild progression dataset (gamedata.db, #122)
//
// Two tables hold every number the Phase-2 guild progression machinery reads
// (plan §3.1.2's governing rule: the mechanisms are binding code shape, every
// number is a v1 seed): guild_level_curve — one row per Guild Level, its
// requirement thresholds stored as CUMULATIVE lifetime burn totals plus the
// level's grants — and guild_settings, the scalar knobs including the two
// independent enable switches. Max Guild Level IS the highest curve row:
// adding levels later is an INSERT, never a code change.
//
// The dataset loads whole into an immutable value (tier_config posture),
// is validated fail-fast at boot — the server does not start on a curve it
// cannot explain — and live-reloads via the candidate/reject pattern
// (`reload guilds`): a rejected candidate leaves the running config untouched.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md §3.1.2; ADR 0007.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace hb::server
{
	// One guild_level_curve row. Requirement columns are the cumulative
	// lifetime burn totals a guild's three counters must ALL reach to earn
	// the level (the §3.1.2 AND-gate — no conversion between lanes); grant
	// columns are what holding the level confers.
	struct guild_level_row
	{
		int     level = 0;
		int64_t gold = 0;
		int64_t enemy_kills = 0;
		int64_t contribution = 0;
		int     member_cap = 0;
		int     officer_cap = 0;   // the Guildmaster is never counted
		int     hunt_slots = 0;
		int     raid_slots = 0;
		int     repair_pct = 0;    // % off repair bills, before the Heldenian loser x2
	};

	// The Commander slot count at every level (§3.1.2 item 2): pinned at one —
	// deliberately NOT a curve column — until Phase 3 rules multi-commander
	// rally/construct semantics.
	constexpr int guild_commander_slots = 1;

	struct guild_progression_config
	{
		// Validated contiguous from level 1, so levels[n].level == n + 1.
		std::vector<guild_level_row> levels;

		// The two independent enable switches (§3.1.2 item 6). Claiming,
		// roster, Treasury and chat ship always-on; these gate only the burn
		// lane and the bonus lane.
		bool donations_enabled = false;
		bool title_bonuses_enabled = false;

		int64_t title_inactivity_ms = 600000;    // a held Title's AFK/offline release
		int64_t donation_min_gold = 1000;
		int64_t donation_min_ek = 1;
		int64_t donation_min_contribution = 1;

		int huntmaster_damage_pct = 10;          // +% physical AND magic vs NPCs
		int huntmaster_tier_shift_pct = 125;     // x/100 on rare-or-better tier weights
		int huntmaster_tier_shift_min_tier = 2;  // tiers >= this get the shift; 2 = rare
		int raidmaster_attack_power = 3;         // flat AP vs players

		// Rows/keys the loader could not represent. The validator reports
		// each as an error, so a typo'd knob can never silently mean its
		// in-code default (the event_schedule warn-and-skip posture is
		// exactly what §3.1.2 forbids here).
		std::vector<std::string> load_anomalies;

		int max_level() const
		{
			return levels.empty() ? 0 : levels.back().level;
		}

		// The curve row for one level; nullptr outside 1..max_level(). Boot
		// and reload validation guarantee every existing guild level resolves.
		const guild_level_row* find_level(int level) const;
	};

	// Read both tables into `out`, which is written only on full success — a
	// failed load leaves it untouched. False on SQL failure alone; content
	// problems land in load_anomalies for the validator to report.
	bool load_guild_progression(sqlite3* db, guild_progression_config& out);

	// The fail-fast sweep (TierConfigValidator posture): an empty result is
	// the only result the server may start on, and the only result `reload
	// guilds` may swap on. Checks contiguity from level 1, zero level-1
	// requirements, per-column monotonicity, value ranges, and the knob set.
	std::vector<std::string> validate_guild_progression(
		const guild_progression_config& config);

	// The world-vs-curve rule (§3.1.2: levels already earned are never
	// revoked): no existing guild may sit above the curve's max level. Split
	// from validate_guild_progression because at boot the guild store opens
	// only after gamedata.db closes; reload runs both.
	std::vector<std::string> validate_guild_progression_world(
		const guild_progression_config& config, int highest_guild_level);

	// The shared reporting half every caller (boot, reload) pairs with its
	// own outcome summary — the log_tier_validation_errors shape.
	void log_guild_progression_errors(const std::vector<std::string>& errors);
}
