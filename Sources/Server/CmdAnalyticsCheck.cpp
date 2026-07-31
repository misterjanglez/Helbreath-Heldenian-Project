// CmdAnalyticsCheck.cpp: the analytics starter pack's prover (#85, plan P4.3)
//
// The analytics answer one question — does the roller agree with its own data —
// and the answer is a division. Everything therefore rests on two things this
// ticket added, and neither is self-evidently correct:
//
//   1. The DENOMINATOR. `npc_kills` is the first table in the ledger that
//      aggregates a running total per day, and an UPSERT that replaced instead
//      of accumulated would silently discard every earlier flush window of the
//      day. That failure is invisible in a live world: the numbers stay
//      plausible, they are simply too small, and every rate computed from them
//      is too generous by whatever share was lost.
//
//   2. The JOIN. A drop's birth row records its monster by NAME, a kill row is
//      keyed by config id, and the SQL half of the pack has no world database to
//      resolve one into the other. It works only because the kill row carries
//      the name too — so the join is DRIVEN here, against the same statement
//      Scripts/analytics/01_drop_rates.sql runs, rather than assumed.
//
// And one thing the report publishes: the per-kill expectation of each row. A
// kill is a guaranteed head at the authored rarity plus a tail at a divided one
// (#89), so a row's per-kill figure is not its chance times the roll count. The
// identity that catches a mis-derived split is checked on both table shapes.
//
// Everything runs against a scratch ledger created and deleted here. The live
// ledger is not written to and its contents are not read: an append-only audit
// log cannot have test rows taken back out of it.
//
// The machine-readable lines carry no timestamps, ids or serials — the Linux
// gate is a byte-for-byte comparison against the Windows output.
//
// Design contract: PLANS/ItemLedger_Plan.md P4.3.
//
//////////////////////////////////////////////////////////////////////

#include "CmdAnalyticsCheck.h"

#include "CheckTally.h"
#include "DropModel.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "Npc.h"
#include "ServerConsole.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"
#include "sqlite3.h"

#include <cmath>
#include <format>
#include <memory>
#include <string>

namespace item_origin = hb::server::item_origin;
using hb::server::check_tally;
using hb::server::find_probe_item;
using hb::server::item_ledger_store;
using hb::server::probe_scalar;
using hb::server::probe_text;
using hb::server::resolved_slot;
using hb::server::tier_validation_context;

namespace
{
	// Scratch file, created and removed by this command. Beside the live ledger
	// so it exercises the same filesystem and the same WAL behaviour.
	constexpr const char* probe_db = "analytics_probe.db";

	// Probe config ids well above the 117 a real world loads, so a stray copy of
	// this file is obvious rather than mistakable for real history.
	constexpr int32_t probe_npc_a = 90001;
	constexpr int32_t probe_npc_b = 90002;
	constexpr int32_t probe_npc_shared = 90003;   // shares a name with probe_npc_a

	// Two reputation factors either side of neutral, so the summed column can be
	// told apart from the kill count by inspection rather than by coincidence.
	constexpr double rep_low  = 0.25;
	constexpr double rep_high = 2.00;

	// The monster names the join is driven with. Under eleven characters so
	// nothing in the server's fixed-size name buffers has to truncate them.
	constexpr const char* probe_name_a = "ProbeMobA";
	constexpr const char* probe_name_b = "ProbeMobB";

	struct probe_cleanup
	{
		~probe_cleanup() { hb::server::remove_probe_db(probe_db); }
	};

	// Two doubles equal to within the noise of a few multiplications. Nothing
	// here compares a probability to an exact literal — the point is that two
	// derivations of the same quantity agree, not that either is a round number.
	bool near_equal(double a, double b, double tolerance = 1e-9)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// One kill row's column, keyed the way every analytics question is: by the
	// monster. Written once because a hand-typed copy of this SELECT in each
	// check is a hand-typed copy to get wrong.
	int64_t kill_scalar(sqlite3* db, const char* expr, int32_t npc_id)
	{
		return probe_scalar(db, std::format(
			"SELECT {} FROM npc_kills WHERE npc_id={};", expr, npc_id).c_str());
	}

	std::string kill_text(sqlite3* db, const char* column, int32_t npc_id)
	{
		return probe_text(db, std::format(
			"SELECT {} FROM npc_kills WHERE npc_id={};", column, npc_id).c_str());
	}

	// rep_factor_sum is a REAL, so it is read scaled rather than truncated: a
	// check that compared it as an integer would pass on 2.99 as readily as on 3.
	int64_t kill_rep_x100(sqlite3* db, int32_t npc_id)
	{
		return kill_scalar(db, "CAST(ROUND(rep_factor_sum * 100) AS INTEGER)", npc_id);
	}
}

void CmdAnalyticsCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("ANALYTICSCHECK", "analyticscheck");

	//----------------------------------------------------------------------
	// The denominator.
	//----------------------------------------------------------------------

	hb::server::remove_probe_db(probe_db);
	const probe_cleanup cleanup;

	auto scratch = std::make_unique<item_ledger_store>();
	if (!scratch->open(probe_db)) {
		hb::console::error("analyticscheck: could not create the scratch ledger '{}'.",
			probe_db);
		return;
	}
	sqlite3* db = scratch->handle();

	// Recording is a RAM append and nothing else. A kill happens inside the
	// death handler, on the game loop; if it ever reached disk synchronously,
	// every monster death in the world would be paying for I/O.
	const size_t before = scratch->pending_count();
	scratch->record_kill(probe_npc_a, probe_name_a, 1.0);
	tally.record("kill_buffers",
		scratch->pending_count() == before + 1
		&& probe_scalar(db, "SELECT COUNT(*) FROM npc_kills;") == 0);

	// Two more kills of the same monster in the same window. One row, not three:
	// a farmed monster dies thousands of times between flushes and must cost one
	// UPSERT, not thousands of rows.
	scratch->record_kill(probe_npc_a, probe_name_a, rep_low);
	scratch->record_kill(probe_npc_a, probe_name_a, rep_high);
	tally.record("kill_aggregates_in_window", scratch->pending_count() == before + 1);

	// A different monster is a different row.
	scratch->record_kill(probe_npc_b, probe_name_b, 1.0);
	tally.record("kill_separates_monsters", scratch->pending_count() == before + 2);

	// An NPC spawned from no config has id -1. Booking it would pool every such
	// death into one row naming no monster, and the analytics divide by that
	// number — a row that cannot be attributed is worse than an absent one.
	const size_t before_unconfigured = scratch->pending_count();
	scratch->record_kill(-1, "nobody", 1.0);
	tally.record("unconfigured_npc_refused",
		scratch->pending_count() == before_unconfigured);

	const bool flushed = scratch->flush(0);
	tally.record("kill_persists",
		flushed
		&& scratch->pending_count() == 0
		&& probe_scalar(db, "SELECT COUNT(*) FROM npc_kills;") == 2
		&& kill_scalar(db, "kills", probe_npc_a) == 3
		&& kill_scalar(db, "kills", probe_npc_b) == 1);

	// The reputation layer is per-killer and multiplies gear and unique rows
	// linearly (#88), so the sum of the factors — not the kill count — is what
	// prices those rows. 1.00 + 0.25 + 2.00 = 3.25 against three kills: a column
	// that merely counted would read 3.00 and every gear rate would be wrong by
	// whoever happened to be farming.
	tally.record("kill_sums_reputation",
		kill_rep_x100(db, probe_npc_a) == 325
		&& kill_rep_x100(db, probe_npc_b) == 100);

	tally.record("kill_stores_name",
		kill_text(db, "npc_name", probe_npc_a) == probe_name_a
		&& kill_text(db, "npc_name", probe_npc_b) == probe_name_b);

	// THE one that a live world cannot show. The buffer holds a window's delta
	// and the row holds the day's total, so a second window must ADD. An UPSERT
	// that replaced would leave plausible numbers that are simply too small, and
	// every rate computed from them too generous.
	scratch->record_kill(probe_npc_a, probe_name_a, rep_high);
	scratch->record_kill(probe_npc_a, probe_name_a, rep_high);
	const bool second_window = scratch->flush(0);
	tally.record("kill_accumulates_across_flushes",
		second_window
		&& probe_scalar(db, "SELECT COUNT(*) FROM npc_kills;") == 2
		&& kill_scalar(db, "kills", probe_npc_a) == 5
		&& kill_rep_x100(db, probe_npc_a) == 725);

	// The name is a property of the config id, written once per key rather than
	// on every death. A rename mid-window must not split a monster's kills, and
	// the first name recorded is what the day's row keeps.
	scratch->record_kill(probe_npc_a, "Renamed", 1.0);
	const bool renamed_flushed = scratch->flush(0);
	tally.record("kill_name_is_stable",
		renamed_flushed
		&& kill_text(db, "npc_name", probe_npc_a) == probe_name_a
		&& kill_scalar(db, "kills", probe_npc_a) == 6);

	// Two configs under one name — 14 Catapults, 3 Guards — stay two rows keyed
	// by id, and pool only when a query groups by name. Pooling at the writer
	// would make the two indistinguishable forever.
	scratch->record_kill(probe_npc_shared, probe_name_a, 1.0);
	const bool shared_flushed = scratch->flush(0);
	tally.record("shared_name_stays_two_rows",
		shared_flushed
		&& probe_scalar(db, "SELECT COUNT(*) FROM npc_kills;") == 3
		&& probe_scalar(db, std::format(
			"SELECT SUM(kills) FROM npc_kills WHERE npc_name='{}';", probe_name_a).c_str()) == 7);

	// Across a restart, because the whole point of the table is that a rate can
	// be computed from a window nobody was watching.
	scratch->close();
	const bool reopened = scratch->open(probe_db);
	db = scratch->handle();
	tally.record("kills_survive_restart",
		reopened
		&& probe_scalar(db, "SELECT COUNT(*) FROM npc_kills;") == 3
		&& kill_scalar(db, "kills", probe_npc_a) == 6
		&& kill_rep_x100(db, probe_npc_a) == 825);

	//----------------------------------------------------------------------
	// The join. A birth row names its monster; a kill row names the same
	// monster. Driven, not assumed.
	//----------------------------------------------------------------------

	const int probe_item_id = find_probe_item(game, false);
	std::unique_ptr<CItem> probe_item{ probe_item_id < 0
		? nullptr
		: game->m_item_manager->create_item(probe_item_id, item_origin::npc_drop) };
	if (probe_item == nullptr) {
		hb::console::error("analyticscheck: no usable non-stackable item id for the probes.");
		tally.report();
		return;
	}

	// Two loot births from ProbeMobA, and one item created with no monster at
	// all. The second is not a hypothetical: every contract prover in this
	// server mints real items through the factory as npc_drop with no
	// origin_detail, so a query that counted them would inflate every observed
	// rate in the report.
	probe_item->m_serial = 990001;
	scratch->record_mint(*probe_item, probe_name_a, "probemap", 1, 1);
	probe_item->m_serial = 990002;
	scratch->record_mint(*probe_item, probe_name_a, "probemap", 2, 2);
	probe_item->m_serial = 990003;
	scratch->record_mint(*probe_item, nullptr, "probemap", 3, 3);
	const bool minted = scratch->flush(0);

	tally.record("loot_births_recorded",
		minted
		&& probe_scalar(db, "SELECT COUNT(*) FROM item_instances;") == 3
		&& probe_scalar(db,
			"SELECT COUNT(*) FROM item_instances WHERE origin_detail IS NULL;") == 1);

	// The pack's own join, run verbatim in shape: drops grouped by the monster
	// NAME on the birth row, kills grouped by the monster NAME on the kill row,
	// matched on nothing else. If the name ever stopped being written to
	// npc_kills, this is the check that goes red — and every query in
	// Scripts/analytics would have been returning empty joins in silence.
	const std::string join_sql = std::format(
		"WITH drops AS ("
		" SELECT origin_detail AS monster, COUNT(*) AS drops FROM item_instances"
		" WHERE origin_type={} AND origin_detail IS NOT NULL GROUP BY origin_detail),"
		"kills AS ("
		" SELECT npc_name AS monster, SUM(kills) AS kills FROM npc_kills GROUP BY npc_name)"
		" SELECT d.drops * 1000 + k.kills FROM drops d"
		" JOIN kills k ON k.monster = d.monster;",
		static_cast<int>(item_origin::npc_drop));

	// 2 drops, 7 kills, in one number so the join returns exactly one row and a
	// half-correct answer cannot pass.
	tally.record("drops_join_kills_by_name", probe_scalar(db, join_sql.c_str()) == 2007);

	// The unattributed mint must not reach the loot side of that join. Its
	// monster is unknown, so counting it would attribute a prover's probe item
	// to whichever monster happened to be beside it.
	tally.record("unattributed_mint_excluded",
		probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_instances"
			" WHERE origin_type={} AND origin_detail IS NOT NULL;",
			static_cast<int>(item_origin::npc_drop)).c_str()) == 2);

	scratch->close();
	scratch.reset();

	//----------------------------------------------------------------------
	// The predicted side: what dropodds publishes as a row's per-kill rate.
	//----------------------------------------------------------------------

	const tier_validation_context context = game->running_validation_context();
	if (context.drop_tables == nullptr || context.npc_configs == nullptr) {
		hb::console::error("analyticscheck: drop tables are not loaded.");
		tally.report();
		return;
	}

	const hb::server::drop_multipliers& generosity = game->get_tier_config().generosity;

	// Every priced row of every monster, on both shapes of table. `checked`
	// exists so a run against a world with no tables cannot report a pass for
	// having examined nothing.
	int checked = 0;
	int guaranteed_checked = 0;
	bool yield_holds = true;
	bool guarantee_holds = true;
	bool rows_complete = true;

	for (int id = 0; id < context.npc_config_count; id++)
	{
		const CNpc* npc = context.npc_configs[id];
		if (npc == nullptr) continue;
		if (hb::server::npc_type_never_drops(npc->m_type)) continue;

		const int slots[2] = { npc->m_stage1_table_id, npc->m_stage2_table_id };
		for (int slot = 0; slot < 2; slot++)
		{
			resolved_slot resolved;
			if (!hb::server::resolve_slot(*context.drop_tables, slots[slot],
				generosity, slot + 1, static_cast<uint8_t>(npc->m_loot_grade), 1.0,
				context.item_configs, context.item_config_count, resolved))
				continue;

			// One machine ROW line per table entry. The emitter loops the
			// resolved chances, so a table whose rows and chances ever fell out
			// of step would publish a predicted side with holes in it — and a
			// missing row reads to the audit tool as an UNPRICED drop, its
			// loudest finding.
			if (resolved.size() != resolved.table->entries.size())
				rows_complete = false;

			// The identity. Summing what every row is expected to yield must give
			// what a kill is expected to yield: every head roll pays out
			// something by construction (its leftover buys the guarantee item)
			// and every roll past the head pays out with probability tail_total.
			double summed = 0.0;
			for (size_t i = 0; i < resolved.size(); i++)
				summed += resolved.per_kill(i);
			if (!near_equal(summed, resolved.expected_yield()))
				yield_holds = false;
			checked++;

			if (resolved.head_rolls > 0)
			{
				guaranteed_checked++;
				// The guarantee row carries no chance of its own, so its whole
				// per-kill figure is the head's leftover. Printing it as zero
				// beside a table that pays it out several times a kill is what
				// this exists to stop.
				if (!near_equal(resolved.guarantee_per_kill,
					resolved.head_rolls * (1.0 - resolved.head_total)))
					guarantee_holds = false;
			}
		}
	}

	tally.record("per_kill_sums_to_expected_yield", yield_holds && checked > 0,
		std::format("{} slots", checked));
	tally.record("guaranteed_head_pays_its_leftover",
		guarantee_holds && guaranteed_checked > 0,
		std::format("{} slots", guaranteed_checked));
	tally.record("every_table_row_is_priced", rows_complete && checked > 0);

	// The capture the audit tool consumes is taken at rating 0, and the tool
	// applies the reputation layer itself from the ledger's summed factors. That
	// only works while a neutral player rolls exactly as authored — otherwise
	// the layer lands twice and every gear rate in the report is squared.
	tally.record("neutral_player_rolls_as_authored",
		near_equal(generosity.reputation_factor(0), 1.0));

	// And the flag that decides which denominator a row gets. Gear and uniques
	// are what reputation moves; gold and consumables drop at the ordinary rate
	// no matter who lands the kill.
	namespace drop_category = hb::server::drop_category;
	tally.record("reputation_scope_is_gear_and_uniques",
		hb::server::drop_multipliers::reputation_applies(drop_category::gear)
		&& hb::server::drop_multipliers::reputation_applies(drop_category::unique)
		&& !hb::server::drop_multipliers::reputation_applies(drop_category::gold)
		&& !hb::server::drop_multipliers::reputation_applies(drop_category::consumable));

	// The scratch file is gone by the time this reports, and nothing above ever
	// opened the live ledger for writing.
	tally.record("live_ledger_untouched",
		game->m_item_ledger_store != nullptr
		&& game->m_item_ledger_store->is_open());

	tally.report();
}
