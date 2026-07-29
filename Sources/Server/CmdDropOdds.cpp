// CmdDropOdds.cpp: effective drop-odds report (Item Tiers #66, remodelled #73)
//
// Its job used to be multiplying a five-term chain out — a gold chance that
// preempted only the monsters carrying gold dice, two server_config
// multipliers, a per-grade first_drop_chance and the gear share of whichever
// stage-1 table the monster used — because "how often does a player see tiered
// gear" was not a number anywhere in the system.
//
// Under #73 there is no chain. Every drop-table row carries its own absolute
// per-kill chance, so this report's job is to PRINT the stored number and the
// generosity stack that scales it, and to add the view no tool produced
// before: per monster, per item, "1 in N kills".
//
// It resolves through hb::server::resolve_drop_chances — the same function the
// roll calls — so the report and the roller cannot disagree about a rate, the
// saturation clamp or a multiplier. Item classification likewise calls the
// roller's own scope gate. Every term is read live, so `reload tiers` /
// `reload npcs` show up in the next report without a restart.
//
// Design contract: PLANS/ItemTiers_Plan.md §8 Amendment A1, ADR 0005.
//
//////////////////////////////////////////////////////////////////////

#include "CmdDropOdds.h"

#include "DropModel.h"
#include "Game.h"                   // CGame
#include "Item.h"
#include "Npc.h"
#include "RollStrategy.h"
#include "ServerConsole.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"    // ordinary_tier_gear
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hb::shared::item;

namespace
{

namespace drop_category = hb::server::drop_category;
using hb::server::drop_chance_denominator;
using hb::server::drop_table;
using hb::server::tier_validation_context;

// What one monster's slot yields per kill, as absolute probabilities. Nothing
// here is a share of anything: each figure is already the per-kill chance.
struct slot_odds
{
	double anything = 0.0;      // any row at all, gold included - what saturation measures
	double any_item = 0.0;      // any row except gold
	double gear = 0.0;          // tier-scope gear, the only thing that tier-rolls
	int    rows = 0;
	int    gear_rows = 0;
	bool   saturated = false;
};

// Resolve one slot through the roller's own arithmetic.
slot_odds odds_of(const CGame& game, const tier_validation_context& context,
	int table_id, int stage_slot, uint8_t loot_grade, double rep_factor)
{
	slot_odds odds;
	if (context.drop_tables == nullptr) return odds;
	const auto found = context.drop_tables->find(table_id);
	if (found == context.drop_tables->end()) return odds;
	const drop_table& table = found->second;

	std::vector<uint32_t> chances;
	hb::server::resolve_drop_chances(table, game.get_tier_config().generosity,
		stage_slot, loot_grade, rep_factor,
		context.item_configs, context.item_config_count,
		chances, &odds.saturated);

	for (size_t i = 0; i < chances.size(); i++)
	{
		const double p = static_cast<double>(chances[i]) / drop_chance_denominator;
		const int item_id = table.entries[i].item_id;
		odds.anything += p;
		odds.rows++;
		if (item_id == ItemId::Gold) continue;
		odds.any_item += p;
		if (hb::server::ordinary_tier_gear(context, item_id) == nullptr) continue;
		odds.gear += p;
		odds.gear_rows++;
	}
	return odds;
}

// Everything one loot grade's monsters add up to. The averages are over
// npc_configs at the grade — NOT weighted by spawn counts or kill rates,
// which the report states, because a grade holding both a common field mob
// and a rare cave boss averages two very different tables.
struct grade_rollup
{
	int npcs = 0;
	int npcs_without_table = 0;
	int saturated = 0;
	double anything_sum = 0.0;
	double any_item_sum = 0.0;
	double gear_sum = 0.0;
};

// Percent with enough resolution for the rare tail: at launch rates an elite
// Legendary is ~0.003% per kill. A dash means structurally impossible — a zero
// tier weight is the staircase hard gate (spec §8), not a rounding artifact.
std::string pct(double p)
{
	if (p <= 0.0) return "-";
	return std::format("{:.4f}%", p * 100.0);
}

// The same odds as "1 in N kills", which is how rarity is authored and how an
// operator reasons about it. Fractional below 10 because "1 in 3.33" is the
// honest reading of a 30% gold row.
std::string one_in(double p)
{
	if (p <= 0.0) return "-";
	if (p >= 1.0) return "1";
	const double n = 1.0 / p;
	if (n < 10.0)      return std::format("{:.2f}", n);
	if (n < 1000000.0) return std::format("{:.0f}", n);
	return std::format("{:.2g}", n);
}

// A wire-precision number for the machine-readable lines.
std::string raw(double p)
{
	return std::format("{:.8f}", p);
}

// The grade's tier weights normalized into shares. Every share is 0 when the
// grade has no row at all, which a legacy world is allowed to be.
struct tier_split
{
	const hb::server::loot_grade_config* grade;
	int total = 0;

	explicit tier_split(const hb::server::loot_grade_config* row) : grade(row)
	{
		if (grade == nullptr) return;
		for (uint8_t tier = 1; tier <= tier_count; tier++) total += grade->tier_weight(tier);
	}

	// Share of tiered drops landing on `tier` (1-based), 0 when the grade
	// cannot reach it — a zero weight is the staircase hard gate (spec §8).
	double share(int tier) const
	{
		return total > 0
			? static_cast<double>(grade->tier_weight(static_cast<uint8_t>(tier))) / total
			: 0.0;
	}
};

// One row of every table below: a label plus its columns, so the header and
// the rows can never drift out of alignment.
std::string row(const std::string& label, const std::vector<std::string>& columns)
{
	std::string line = std::format("  {:<13}", label);
	for (const auto& column : columns) line += std::format("{:>11}", column);
	return line;
}

std::string grade_label(const hb::server::tier_config& config, int grade)
{
	const hb::server::loot_grade_config* r = config.find_loot_grade(static_cast<uint8_t>(grade));
	return r != nullptr ? std::format("{} {}", grade, r->name) : std::format("{} ?", grade);
}

// The generosity stack, printed once so every number below is readable
// against it. A stack of all 1.0 means "exactly as authored".
void write_multiplier_stack(const hb::server::drop_multipliers& m, int rating)
{
	hb::console::write(std::format(
		"  generosity: global x{:.2f} | stage 1 x{:.2f}, stage 2 x{:.2f}",
		m.global, m.stage[1], m.stage[2]));
	std::string categories;
	for (uint8_t c = 0; c < drop_category::count; c++)
		categories += std::format("{}{} x{:.2f}", categories.empty() ? "" : ", ",
			hb::server::drop_category_name(c), m.category[c]);
	hb::console::write("    category: " + categories);
	std::string grades;
	for (int g = 1; g <= hb::server::drop_multipliers::max_grade; g++)
		grades += std::format("{}{} x{:.2f}", grades.empty() ? "" : ", ", g, m.grade[g]);
	hb::console::write("    grade: " + grades);

	// The per-player layer. Spelled out even at the neutral default, because a
	// reader who cannot see it has no way to know the figures below are for
	// rating 0 rather than for everybody.
	hb::console::write(std::format(
		"    reputation: rating {} -> x{:.2f} on gear and unique rows{}",
		rating, m.reputation_factor(rating),
		rating == 0 ? " (neutral - the authored rate)" : ""));

	hb::console::write(
		"  effective_ppb = drop_chance_ppb x (global x stage x category x grade x rep)"
		" - larger is MORE likely");
}

// `rating <n>` may trail any form of the command. Reputation is the one
// per-player term in the stack, so the report has to be told whose kill it is
// pricing; with no argument it prices a neutral player, which is what every
// published figure in this project has always meant.
//
// Clamped to the vote system's own +/-500 so the report can never quote a
// rating the game cannot produce.
int parse_rating_suffix(const char* args, std::string& head)
{
	head = (args != nullptr) ? args : "";

	// Last occurrence, and only on a word boundary — `dropodds npc Grating`
	// must not be read as a rating argument.
	const size_t at = head.rfind("rating");
	if (at == std::string::npos) return 0;
	if (at != 0 && head[at - 1] != ' ' && head[at - 1] != '\t') return 0;

	int rating = 0;
	std::sscanf(head.c_str() + at + 6, "%d", &rating);
	head.erase(at);
	while (!head.empty() && (head.back() == ' ' || head.back() == '\t'))
		head.pop_back();

	if (rating >  500) rating =  500;
	if (rating < -500) rating = -500;
	return rating;
}

// The per-monster per-item listing: every row of both slots as "1 in N", with
// what is left over for "nothing". This is the view the owner asked for and
// that no tool produced under the old model.
void write_monster_rows(const CGame& game, const tier_validation_context& context,
	int npc_id, const CNpc& npc, double rep_factor)
{
	hb::console::write("");
	hb::console::write(std::format("{} '{}' - loot grade {}", npc_id,
		npc.m_npc_name, npc.m_loot_grade), console_color::bright);

	const int slots[2] = { npc.m_stage1_table_id, npc.m_stage2_table_id };
	for (int slot = 0; slot < 2; slot++)
	{
		if (context.drop_tables == nullptr) return;
		const auto found = context.drop_tables->find(slots[slot]);
		if (found == context.drop_tables->end())
		{
			hb::console::write(std::format("  stage {}: no table", slot + 1),
				console_color::muted);
			continue;
		}
		const drop_table& table = found->second;

		std::vector<uint32_t> chances;
		bool saturated = false;
		hb::server::resolve_drop_chances(table, game.get_tier_config().generosity,
			slot + 1, static_cast<uint8_t>(npc.m_loot_grade), rep_factor,
			context.item_configs, context.item_config_count, chances, &saturated);

		std::string header = std::format("  stage {}: table {} '{}'", slot + 1,
			table.id, table.name);
		if (table.roll_count_min != 1 || table.roll_count_max != 1)
			header += std::format(", rolled {}-{}x", table.roll_count_min,
				table.roll_count_max);
		header += std::format(", {}, on {}",
			hb::server::drop_placement_name(table.placement),
			hb::server::drop_delay_name(table.delay));
		if (slot == 0) header += ", tier-rolls";
		hb::console::write(header, console_color::info);
		if (saturated)
			hb::console::write(
				"    SATURATED - rows were scaled back to 1.0; a bigger multiplier"
				" is a no-op here", console_color::warning);

		hb::console::write(std::format("    {:>5}  {:<30}{:>12}{:>14}  {}",
			"item", "name", "per kill", "1 in N", "category"), console_color::muted);

		double total = 0.0;
		for (size_t i = 0; i < chances.size(); i++)
		{
			const int item_id = table.entries[i].item_id;
			const double p = static_cast<double>(chances[i]) / drop_chance_denominator;
			total += p;
			const CItem* config = (item_id >= 0 && item_id < context.item_config_count)
				? context.item_configs[item_id] : nullptr;
			const uint8_t category = hb::server::drop_category_of(config, item_id);
			hb::console::write(std::format("    {:>5}  {:<30}{:>12}{:>14}  {}{}",
				item_id, config != nullptr ? config->m_name : "?",
				pct(p), one_in(p), hb::server::drop_category_name(category),
				hb::server::ordinary_tier_gear(context, item_id) != nullptr
					? " (tier-rolls)" : ""));
		}
		hb::console::write(std::format("    {:>5}  {:<30}{:>12}{:>14}",
			"", "nothing", pct(1.0 - total), one_in(1.0 - total)),
			console_color::muted);
		if (table.roll_count_max > 1)
			hb::console::write(std::format(
				"    expected items per kill: {:.2f}-{:.2f} (the table is rolled {}-{}x;"
				" roll count is never touched by a multiplier)",
				total * table.roll_count_min, total * table.roll_count_max,
				table.roll_count_min, table.roll_count_max), console_color::muted);
	}
}

} // namespace

void CmdDropOdds::execute(CGame* game, const char* args)
{
	const tier_validation_context context = game->running_validation_context();
	if (context.npc_configs == nullptr || context.drop_tables == nullptr)
	{
		hb::console::error("dropodds: npc configs or drop tables are not loaded.");
		return;
	}

	const hb::server::tier_config& config = game->get_tier_config();
	const bool tiered = config.item_system == item_system_mode::tiered;

	// Strip the optional trailing `rating <n>` before anything else parses args,
	// so it composes with every form of the command.
	std::string head;
	const int rating = parse_rating_suffix(args, head);
	const double rep_factor = config.generosity.reputation_factor(rating);
	args = head.c_str();

	// ---- dropodds npc <id|name>: the per-item rarity listing ---------------
	if (std::strncmp(args, "npc", 3) == 0)
	{
		const char* which = args + 3;
		while (*which == ' ') which++;
		if (*which == '\0')
		{
			hb::console::error("Usage: dropodds npc <npc_id|name> [rating <n>]");
			return;
		}
		int wanted = -1;
		std::sscanf(which, "%d", &wanted);

		hb::console::write(std::format("dropodds: item_system = {}",
			item_system_mode_name(config.item_system)), console_color::bright);
		write_multiplier_stack(config.generosity, rating);

		int listed = 0;
		for (int id = 0; id < context.npc_config_count; id++)
		{
			const CNpc* npc = context.npc_configs[id];
			if (npc == nullptr) continue;
			if (id != wanted && hb_stricmp(npc->m_npc_name, which) != 0) continue;
			write_monster_rows(*game, context, id, *npc, rep_factor);
			listed++;
		}
		if (listed == 0)
			hb::console::error("dropodds: no npc_config matches '{}'", which);
		return;
	}

	int grade_filter = 0;
	if (args[0] != '\0' &&
		(std::sscanf(args, "%d", &grade_filter) != 1 ||
			grade_filter < 1 || grade_filter > loot_grade_count))
	{
		hb::console::error("Usage: dropodds [grade 1-{}] [rating <n>] | dropodds npc <id|name> [rating <n>]",
			static_cast<int>(loot_grade_count));
		return;
	}

	// ---- gather -----------------------------------------------------------
	grade_rollup rollup[loot_grade_count + 1];
	// Keyed on (table id, grade): the grade multiplier is part of the
	// resolution, so one table reached from two grades has two answers. Four
	// stage-1 tables are shared across grades today, and keying on the table
	// alone would print whichever monster happened to be enumerated last.
	std::map<std::pair<int, int>, slot_odds> table_odds;
	int never_drop_configs = 0;
	std::vector<std::string> ungraded;

	for (int id = 0; id < context.npc_config_count; id++)
	{
		const CNpc* npc = context.npc_configs[id];
		if (npc == nullptr) continue;
		if (hb::server::npc_type_never_drops(npc->m_type)) { never_drop_configs++; continue; }

		const int grade = npc->m_loot_grade;
		// A grade outside the ladder is the boot validator's error to report,
		// but it silently removes a monster from the economy — so it is named
		// here rather than folded into an "excluded" tally.
		if (grade < 1 || grade > loot_grade_count)
		{
			ungraded.push_back(std::format("{} '{}' (loot_grade {})", id, npc->m_npc_name, grade));
			continue;
		}

		grade_rollup& r = rollup[grade];
		r.npcs++;
		if (npc->m_stage1_table_id == 0) { r.npcs_without_table++; continue; }

		const slot_odds odds = odds_of(*game, context, npc->m_stage1_table_id, 1,
			static_cast<uint8_t>(grade), rep_factor);
		r.anything_sum += odds.anything;
		r.any_item_sum += odds.any_item;
		r.gear_sum += odds.gear;
		if (odds.saturated) r.saturated++;
		table_odds[{ npc->m_stage1_table_id, grade }] = odds;
	}

	int priced_npcs = 0;
	for (int grade = 1; grade <= loot_grade_count; grade++) priced_npcs += rollup[grade].npcs;

	// ---- header -----------------------------------------------------------
	hb::console::write(std::format("dropodds: item_system = {} ({} roll strategy in force)",
		item_system_mode_name(config.item_system),
		tiered ? "the tiered" : "the legacy"), console_color::bright);
	write_multiplier_stack(config.generosity, rating);
	hb::console::write(
		"  every figure below is an ABSOLUTE per-kill chance read off the drop");
	hb::console::write(
		"  table - there is no gold preempt and no per-stage gate left to multiply");
	hb::console::write(
		"  stage 1 (the tier-rolling venue, spec S8) is what these columns price;");
	hb::console::write(
		"  use `dropodds npc <id>` for both stages of one monster, row by row");
	hb::console::write(std::format(
		"  per-grade figures average over the {} npc_configs that can drop, one vote each -",
		priced_npcs));
	hb::console::write(std::format(
		"  NOT weighted by spawn counts or kill rates ({} guard/dummy/crop configs never",
		never_drop_configs));
	hb::console::write("  drop at all and are excluded)");
	for (const auto& npc : ungraded)
		hb::console::write(std::format(
			"  NOT PRICED - npc_config {} is outside the loot-grade ladder", npc),
			console_color::error);
	if (!tiered)
	{
		hb::console::write("  legacy mode: nothing tier-rolls, so the tier columns are blank.",
			console_color::warning);
		hb::console::write("  The gear column is what WOULD tier-roll in tiered mode.",
			console_color::warning);
	}

	// ---- per-grade odds ---------------------------------------------------
	const int first_grade = grade_filter != 0 ? grade_filter : 1;
	const int last_grade = grade_filter != 0 ? grade_filter : loot_grade_count;

	hb::console::write("");
	hb::console::write("Per-kill odds by loot grade (stage 1):", console_color::info);
	hb::console::write(row("grade", { "npcs", "something", "any item", "gear",
		"Common", "Rare", "Epic", "Legendary" }), console_color::muted);

	double any_odds[loot_grade_count + 1] = {};
	double gear_odds[loot_grade_count + 1] = {};
	double anything_odds[loot_grade_count + 1] = {};
	double tier_odds[loot_grade_count + 1][tier_count] = {};

	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		const grade_rollup& r = rollup[grade];
		const tier_split split(config.find_loot_grade(static_cast<uint8_t>(grade)));

		anything_odds[grade] = r.npcs > 0 ? r.anything_sum / r.npcs : 0.0;
		any_odds[grade] = r.npcs > 0 ? r.any_item_sum / r.npcs : 0.0;
		gear_odds[grade] = r.npcs > 0 ? r.gear_sum / r.npcs : 0.0;

		std::vector<std::string> columns = { std::to_string(r.npcs),
			pct(anything_odds[grade]), pct(any_odds[grade]), pct(gear_odds[grade]) };
		for (int tier = 1; tier <= tier_count; tier++)
		{
			tier_odds[grade][tier - 1] = tiered ? gear_odds[grade] * split.share(tier) : 0.0;
			columns.push_back(tiered ? pct(tier_odds[grade][tier - 1]) : "-");
		}
		hb::console::write(row(grade_label(config, grade), columns));

		if (r.npcs_without_table > 0)
			hb::console::write(std::format(
				"      ({} of them have no stage-1 table and never drop anything there)",
				r.npcs_without_table), console_color::muted);
		if (r.saturated > 0)
			hb::console::write(std::format(
				"      ({} of them are SATURATED - stage-1 rows sum to 1.0, so the"
				" stage-1 multiplier cannot move them; see #87)", r.saturated),
				console_color::warning);
	}

	hb::console::write("");
	hb::console::write("Same odds as kills per outcome (1 in N):", console_color::info);
	hb::console::write(row("grade", { "something", "gear", "Common", "Rare", "Epic", "Legendary" }),
		console_color::muted);
	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		std::vector<std::string> columns = { one_in(anything_odds[grade]),
			one_in(gear_odds[grade]) };
		for (int tier = 1; tier <= tier_count; tier++)
			columns.push_back(tiered ? one_in(tier_odds[grade][tier - 1]) : "-");
		hb::console::write(row(grade_label(config, grade), columns));
	}

	if (tiered)
	{
		hb::console::write("");
		hb::console::write("Tier split of a tiered drop (compare a rollsmoke T histogram):",
			console_color::info);
		hb::console::write(row("grade", { "Common", "Rare", "Epic", "Legendary" }),
			console_color::muted);
		for (int grade = first_grade; grade <= last_grade; grade++)
		{
			const tier_split split(config.find_loot_grade(static_cast<uint8_t>(grade)));
			std::vector<std::string> columns;
			for (int tier = 1; tier <= tier_count; tier++)
				columns.push_back(pct(split.share(tier)));
			hb::console::write(row(grade_label(config, grade), columns));
		}
	}

	// ---- the stage-1 tables themselves ------------------------------------
	hb::console::write("");
	hb::console::write("Stage-1 drop tables in use:", console_color::info);
	hb::console::write(std::format("  {:>5}  {:<30}{:>7}{:>10}{:>12}{:>12}  {}",
		"id", "name", "rows", "gear rows", "something", "gear", "grade"),
		console_color::muted);

	int listed = 0;
	for (const auto& [key, odds] : table_odds)
	{
		const auto& [table_id, grade] = key;
		if (grade_filter != 0 && grade != grade_filter) continue;
		const auto table = context.drop_tables->find(table_id);

		hb::console::write(std::format("  {:>5}  {:<30}{:>7}{:>10}{:>12}{:>12}  {}{}",
			table_id, table->second.name, odds.rows, odds.gear_rows,
			pct(odds.anything), pct(odds.gear), grade,
			odds.saturated ? "  SATURATED" : ""));
		listed++;
	}
	if (grade_filter != 0)
		hb::console::write(std::format("  {} table(s) used by grade {}", listed, grade_filter),
			console_color::muted);
	else
		hb::console::write(std::format(
			"  {} (table, grade) pairing(s) in use, out of {} loaded tables", listed,
			static_cast<int>(context.drop_tables->size())), console_color::muted);

	// ---- the monsters themselves, when one grade was asked for ------------
	if (grade_filter != 0)
	{
		hb::console::write("");
		hb::console::write(std::format("Monsters at grade {} (exact, per monster):",
			grade_label(config, grade_filter)), console_color::info);
		hb::console::write(std::format("  {:>5}  {:<26}{:>8}{:>8}{:>12}{:>12}{:>10}",
			"npc", "name", "s1", "s2", "something", "gear/kill", "1 in N"),
			console_color::muted);

		for (int id = 0; id < context.npc_config_count; id++)
		{
			const CNpc* npc = context.npc_configs[id];
			if (npc == nullptr || npc->m_loot_grade != grade_filter) continue;
			if (hb::server::npc_type_never_drops(npc->m_type)) continue;

			const slot_odds s = odds_of(*game, context, npc->m_stage1_table_id, 1,
				static_cast<uint8_t>(grade_filter), rep_factor);
			hb::console::write(std::format("  {:>5}  {:<26}{:>8}{:>8}{:>12}{:>12}{:>10}",
				id, npc->m_npc_name, npc->m_stage1_table_id, npc->m_stage2_table_id,
				pct(s.anything), pct(s.gear), one_in(s.gear)));
		}
	}

	// ---- machine-readable -------------------------------------------------
	const hb::server::drop_multipliers& m = config.generosity;
	// The rating and its resolved factor ride the MODE line so a diff of two
	// runs shows which player the figures below were priced for. #73's baseline
	// had no such fields; the gate this feeds is Windows-vs-Linux agreement on
	// the same invocation, not agreement with an older format.
	hb::console::write(std::format("DROPODDS MODE {} {:.2f} {:.2f} {:.2f} {} {:.2f}",
		item_system_mode_name(config.item_system), m.global, m.stage[1], m.stage[2],
		rating, rep_factor));
	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		std::string line = std::format("DROPODDS GRADE {} {} {} {}", grade,
			rollup[grade].npcs, raw(any_odds[grade]), raw(gear_odds[grade]));
		for (int tier = 0; tier < tier_count; tier++)
			line += " " + raw(tier_odds[grade][tier]);
		// Trailing: the "something drops" figure, which is what saturation is
		// measured against, and how many of the grade's monsters are saturated.
		line += " " + raw(anything_odds[grade]) + " " + std::to_string(rollup[grade].saturated);
		hb::console::write(line);

		const tier_split split(config.find_loot_grade(static_cast<uint8_t>(grade)));
		std::string split_line = std::format("DROPODDS SPLIT {}", grade);
		for (int tier = 1; tier <= tier_count; tier++)
			split_line += " " + raw(split.share(tier));
		hb::console::write(split_line);
	}
	for (const auto& [key, odds] : table_odds)
	{
		const auto& [table_id, grade] = key;
		if (grade_filter != 0 && grade != grade_filter) continue;
		hb::console::write(std::format("DROPODDS TABLE {} {} {} {}", table_id,
			grade, raw(odds.any_item), raw(odds.gear)));
	}
}
