// CmdDropOdds.cpp: effective drop-odds report (Item Tiers #66)
//
// "How often does a player see tiered gear" was not a number anywhere in
// the system: it is the product of a base gold chance that preempts only the
// monsters carrying gold dice, two server_config multipliers, a per-grade
// first_drop_chance, the gear share of whichever stage-1 drop table the
// monster uses, and a per-grade tier weight — spread across a C++ constant,
// server_config.json and two database tables. This command multiplies the
// chain out from the RUNNING config and prints it, so tuning is arithmetic
// instead of a few hundred kills of guesswork.
//
// Every term is read live (loot_grades, npc_configs, drop tables, the
// server multipliers), so `reload tiers` / `reload npcs` / `reload config`
// show up in the next report without a restart. Item classification calls
// the roller's own scope gate — a report that disagreed with the roller
// would be worse than no report — and the tier split it prints is the same
// distribution `rollsmoke` samples, so the two are directly comparable.
//
// Design contract: PLANS/ItemTiers_Plan.md §8.
//
//////////////////////////////////////////////////////////////////////

#include "CmdDropOdds.h"

#include "EntityManager.h"          // base_gold_drop_chance, apply_drop_multiplier
#include "Game.h"                   // CGame, DropTable
#include "Item.h"
#include "Npc.h"
#include "RollStrategy.h"
#include "ServerConsole.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"    // ordinary_tier_gear
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace hb::shared::item;

namespace
{

using hb::server::tier_validation_context;

// What one stage-1 table yields, conditional on the stage-1 roll happening:
// the weight share that lands on any item at all, and the share that lands
// on tier-scope gear. The gear share is the term nobody could see before,
// and the one that dominates the chain.
struct stage1_shares
{
	double any_item = 0.0;
	double gear = 0.0;
	int entries = 0;
	int gear_entries = 0;
};

stage1_shares shares_of(const DropTable& table, const tier_validation_context& context)
{
	stage1_shares shares;
	shares.entries = static_cast<int>(table.stage_entries[1].size());
	const double total = static_cast<double>(table.total_weight[1]);
	if (total <= 0.0) return shares;

	for (const auto& entry : table.stage_entries[1])
	{
		if (entry.item_id == 0) continue;                 // the "nothing" slot
		const double share = static_cast<double>(entry.weight) / total;
		shares.any_item += share;
		if (hb::server::ordinary_tier_gear(context, entry.item_id) == nullptr) continue;
		shares.gear += share;
		shares.gear_entries++;
	}
	return shares;
}

// Everything one loot grade's monsters add up to. The averages are over
// npc_configs at the grade — NOT weighted by spawn counts or kill rates,
// which the report states, because a grade holding both a common field mob
// and a rare cave boss averages two very different tables.
struct grade_rollup
{
	int npcs = 0;
	int npcs_without_table = 0;
	int npcs_without_gold = 0;
	// Each addend is one monster's table share already scaled by ITS OWN gold
	// preempt, because the preempt is conditional on the monster having gold
	// dice (#72) — a term that cannot be factored back out of the sum.
	double any_item_sum = 0.0;
	double gear_sum = 0.0;
	std::set<int> tables;
};

// Percent with enough resolution for the rare tail: at launch weights an
// elite Legendary is ~0.003% per kill. A dash means structurally
// impossible — a zero tier weight is the staircase hard gate (spec §8),
// not a rounding artifact.
std::string pct(double p)
{
	if (p <= 0.0) return "-";
	return std::format("{:.4f}%", p * 100.0);
}

// The same odds as "1 in N kills", which is the shape an operator can feel.
std::string one_in(double p)
{
	if (p <= 0.0) return "-";
	const double n = 1.0 / p;
	return n >= 1000000.0 ? std::format("{:.2g}", n) : std::format("{:.0f}", n);
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
	const hb::server::loot_grade_config* row = config.find_loot_grade(static_cast<uint8_t>(grade));
	return row != nullptr ? std::format("{} {}", grade, row->name) : std::format("{} ?", grade);
}

} // namespace

void CmdDropOdds::execute(CGame* game, const char* args)
{
	int grade_filter = 0;
	if (args != nullptr && args[0] != '\0' &&
		(std::sscanf(args, "%d", &grade_filter) != 1 ||
			grade_filter < 1 || grade_filter > loot_grade_count))
	{
		hb::console::error("Usage: dropodds [grade 1-{}]", static_cast<int>(loot_grade_count));
		return;
	}

	const tier_validation_context context = game->running_validation_context();
	if (context.npc_configs == nullptr || context.drop_tables == nullptr)
	{
		hb::console::error("dropodds: npc configs or drop tables are not loaded.");
		return;
	}

	const hb::server::tier_config& config = game->get_tier_config();
	const bool tiered = config.item_system == item_system_mode::tiered;

	// Term 1: the gold preempt. A gold drop skips the stage-1 item roll
	// entirely (npc_dead_item_generator) — but only when gold is actually
	// placed, so this is a per-monster term rather than a constant scaling
	// everything below: a monster with no gold dice spends the roll on nothing
	// and reaches the item roll at full rate. The chance is global; which
	// monsters pay it is not.
	const double gold = hb::server::apply_drop_multiplier(
		hb::server::base_gold_drop_chance, game->m_gold_drop_rate) / 10000.0;
	const double no_gold = 1.0 - gold;
	const auto reaches_item_roll = [no_gold](bool places_gold)
	{
		return places_gold ? no_gold : 1.0;
	};

	// Term 2: the strategy's own base chance for a grade, times the server
	// multiplier — asked of the strategy, so legacy's flat base and tiered's
	// per-grade row are each reported by whoever owns them.
	const auto first_roll = [game](int grade)
	{
		return hb::server::apply_drop_multiplier(
			game->get_roll_strategy().first_drop_chance(static_cast<uint8_t>(grade)),
			game->m_primary_drop_rate) / 10000.0;
	};

	// ---- gather ---------------------------------------------------------
	std::map<int, stage1_shares> shares;              // drop table id -> shares
	std::map<int, std::set<int>> table_grades;        // drop table id -> grades using it
	grade_rollup rollup[loot_grade_count + 1];
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
		const bool places_gold = hb::server::npc_places_gold(*npc);
		if (!places_gold) r.npcs_without_gold++;

		const auto table = context.drop_tables->find(npc->m_drop_table_id);
		if (table == context.drop_tables->end()) { r.npcs_without_table++; continue; }

		auto [entry, inserted] = shares.try_emplace(table->first);
		if (inserted) entry->second = shares_of(table->second, context);

		const double reaches = reaches_item_roll(places_gold);
		r.any_item_sum += reaches * entry->second.any_item;
		r.gear_sum += reaches * entry->second.gear;
		r.tables.insert(table->first);
		table_grades[table->first].insert(grade);
	}

	int priced_npcs = 0;
	int npcs_without_gold = 0;
	for (int grade = 1; grade <= loot_grade_count; grade++)
	{
		priced_npcs += rollup[grade].npcs;
		npcs_without_gold += rollup[grade].npcs_without_gold;
	}

	// ---- header ---------------------------------------------------------
	hb::console::write(std::format("dropodds: item_system = {} ({} roll strategy in force)",
		item_system_mode_name(config.item_system),
		tiered ? "the tiered" : "the legacy"), console_color::bright);
	hb::console::write(std::format(
		"  server_config.json drop_rates: primary x{:.2f}, gold x{:.2f}",
		game->m_primary_drop_rate, game->m_gold_drop_rate));
	hb::console::write(std::format(
		"  gold preempt {} - a gold drop skips the stage-1 item roll, but ONLY for the",
		pct(gold)));
	hb::console::write(std::format(
		"  {} monsters that carry gold dice; the other {} spend the roll on nothing and",
		priced_npcs - npcs_without_gold, npcs_without_gold));
	hb::console::write("  reach the item roll at full rate, so the preempt is priced per monster");
	hb::console::write(
		"  stage 1 (on death) is the only venue that tier-rolls; stage-2 corpse-decay");
	hb::console::write(
		"  drops never do (spec S8), so nothing below counts them");
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

	// ---- per-grade odds --------------------------------------------------
	const int first_grade = grade_filter != 0 ? grade_filter : 1;
	const int last_grade = grade_filter != 0 ? grade_filter : loot_grade_count;

	hb::console::write("");
	hb::console::write("Per-kill odds by loot grade:", console_color::info);
	hb::console::write(row("grade", { "npcs", "first roll", "any item", "gear",
		"Common", "Rare", "Epic", "Legendary" }), console_color::muted);

	// Kept from the loop so the kills-per table and the machine-readable
	// lines re-print these numbers rather than recompute them.
	double any_odds[loot_grade_count + 1] = {};
	double gear_odds[loot_grade_count + 1] = {};
	double tier_odds[loot_grade_count + 1][tier_count] = {};

	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		const grade_rollup& r = rollup[grade];
		const tier_split split(config.find_loot_grade(static_cast<uint8_t>(grade)));

		const double first = first_roll(grade);
		// The gold preempt is already inside the sums, one monster at a time.
		const double any_item = r.npcs > 0 ? first * r.any_item_sum / r.npcs : 0.0;
		const double gear = r.npcs > 0 ? first * r.gear_sum / r.npcs : 0.0;
		any_odds[grade] = any_item;
		gear_odds[grade] = gear;

		std::vector<std::string> columns = { std::to_string(r.npcs),
			std::format("{:.2f}%", first * 100.0), pct(any_item), pct(gear) };
		for (int tier = 1; tier <= tier_count; tier++)
		{
			tier_odds[grade][tier - 1] = tiered ? gear * split.share(tier) : 0.0;
			columns.push_back(tiered ? pct(tier_odds[grade][tier - 1]) : "-");
		}
		hb::console::write(row(grade_label(config, grade), columns));

		if (r.npcs_without_table > 0)
			hb::console::write(std::format(
				"      ({} of them have no drop table and never drop anything)",
				r.npcs_without_table), console_color::muted);
		if (r.npcs_without_gold > 0)
			hb::console::write(std::format(
				"      ({} of them have no gold dice, so nothing preempts their item roll)",
				r.npcs_without_gold), console_color::muted);
	}

	hb::console::write("");
	hb::console::write("Same odds as kills per outcome (1 in N):", console_color::info);
	hb::console::write(row("grade", { "gear", "Common", "Rare", "Epic", "Legendary" }),
		console_color::muted);
	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		std::vector<std::string> columns = { one_in(gear_odds[grade]) };
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

	// ---- the dominant term, per drop table -------------------------------
	hb::console::write("");
	hb::console::write("Stage-1 drop tables in use - gear share is the dominant term:",
		console_color::info);
	hb::console::write(std::format("  {:>5}  {:<26}{:>8}{:>10}{:>12}{:>13}  {}",
		"id", "name", "entries", "gear rows", "any item", "gear share", "grades"),
		console_color::muted);

	int listed = 0;
	for (const auto& [table_id, grades] : table_grades)
	{
		if (grade_filter != 0 && grades.count(grade_filter) == 0) continue;
		const stage1_shares& s = shares[table_id];
		const auto table = context.drop_tables->find(table_id);

		std::string grade_list;
		for (int grade : grades)
			grade_list += (grade_list.empty() ? "" : ",") + std::to_string(grade);

		hb::console::write(std::format("  {:>5}  {:<26}{:>8}{:>10}{:>12}{:>13}  {}",
			table_id, table->second.name, s.entries, s.gear_entries,
			pct(s.any_item), pct(s.gear), grade_list));
		listed++;
	}
	if (grade_filter != 0)
		hb::console::write(std::format("  {} table(s) used by grade {}", listed, grade_filter),
			console_color::muted);
	else
		hb::console::write(std::format(
			"  {} table(s) in use; {} of the {} loaded tables are referenced by no npc_config",
			listed, static_cast<int>(context.drop_tables->size() - table_grades.size()),
			static_cast<int>(context.drop_tables->size())), console_color::muted);

	// ---- the monsters themselves, when one grade was asked for -----------
	if (grade_filter != 0)
	{
		hb::console::write("");
		hb::console::write(std::format("Monsters at grade {} (exact, per monster):",
			grade_label(config, grade_filter)), console_color::info);
		hb::console::write(std::format("  {:>5}  {:<26}{:>7}{:>10}{:>13}{:>13}{:>10}",
			"npc", "name", "table", "preempt", "gear share", "gear/kill", "1 in N"),
			console_color::muted);

		const double first = first_roll(grade_filter);
		const std::string gold_text = pct(gold);

		for (int id = 0; id < context.npc_config_count; id++)
		{
			const CNpc* npc = context.npc_configs[id];
			if (npc == nullptr || npc->m_loot_grade != grade_filter) continue;
			if (hb::server::npc_type_never_drops(npc->m_type)) continue;

			const auto table = context.drop_tables->find(npc->m_drop_table_id);
			const double share = table != context.drop_tables->end()
				? shares[table->first].gear : 0.0;
			// A dash in the preempt column is the whole defect, per monster:
			// no gold dice, so the gold roll costs this monster nothing.
			const bool places_gold = hb::server::npc_places_gold(*npc);
			const double gear = reaches_item_roll(places_gold) * first * share;

			hb::console::write(std::format("  {:>5}  {:<26}{:>7}{:>10}{:>13}{:>13}{:>10}",
				id, npc->m_npc_name, npc->m_drop_table_id,
				places_gold ? gold_text : std::string("-"),
				pct(share), pct(gear), one_in(gear)));
		}
	}

	// ---- machine-readable ------------------------------------------------
	hb::console::write(std::format("DROPODDS MODE {} {} {:.2f} {:.2f}",
		item_system_mode_name(config.item_system), raw(gold),
		game->m_primary_drop_rate, game->m_gold_drop_rate));
	for (int grade = first_grade; grade <= last_grade; grade++)
	{
		std::string line = std::format("DROPODDS GRADE {} {} {} {}", grade,
			rollup[grade].npcs, raw(any_odds[grade]), raw(gear_odds[grade]));
		for (int tier = 0; tier < tier_count; tier++)
			line += " " + raw(tier_odds[grade][tier]);
		// Trailing count: the monsters at this grade the gold preempt does not
		// apply to. Without it the odds above cannot be rederived from the
		// DROPODDS MODE gold chance and the per-table shares.
		line += " " + std::to_string(rollup[grade].npcs_without_gold);
		hb::console::write(line);

		const tier_split split(config.find_loot_grade(static_cast<uint8_t>(grade)));
		std::string split_line = std::format("DROPODDS SPLIT {}", grade);
		for (int tier = 1; tier <= tier_count; tier++)
			split_line += " " + raw(split.share(tier));
		hb::console::write(split_line);
	}
	for (const auto& [table_id, grades] : table_grades)
	{
		if (grade_filter != 0 && grades.count(grade_filter) == 0) continue;
		hb::console::write(std::format("DROPODDS TABLE {} {} {} {}", table_id,
			raw(shares[table_id].any_item), raw(shares[table_id].gear),
			static_cast<int>(grades.size())));
	}
}
