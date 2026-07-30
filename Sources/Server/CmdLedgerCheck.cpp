// CmdLedgerCheck.cpp: Provenance Ledger prover (Item Provenance Ledger P2.1, #76)
//
// The ledger's whole value is that it has no holes, so "it compiles and the file
// exists" is not evidence. What matters is the set of properties a forensic query
// will later depend on: that recording costs the game loop nothing but RAM, that a
// window reaches disk whole or not at all, that a failed write is retried instead
// of discarded, that a restart cannot re-issue a Serial some item already holds,
// and that the file can be read from outside while the server writes to it.
//
// Most of that is only observable across a close/reopen boundary, which the live
// store cannot demonstrate without stopping the server. So the bulk of the run
// happens against a scratch database this command creates, drives through a full
// restart (including a simulated crash and a simulated lost flush), and deletes.
// The live store is checked only for the two things that must be true of it right
// now: it is in WAL mode, and boot actually lifted the Serial allocator.
//
// Nothing here touches the live ledger's contents. Deleting rows out of an
// append-only audit log to clean up after a test would be a worse habit than the
// coverage it buys.
//
// The machine-readable lines carry no absolute serials, timestamps or ids — the
// Linux gate is a byte-for-byte comparison against the Windows output.
//
// Design contract: PLANS/ItemLedger_Plan.md P2.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdLedgerCheck.h"

#include "CheckTally.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "ServerConsole.h"
#include "sqlite3.h"

#include <filesystem>
#include <format>
#include <memory>
#include <string>

namespace item_origin = hb::server::item_origin;
namespace ledger_event = hb::server::ledger_event;
using hb::server::check_tally;
using hb::server::find_probe_item;
using hb::server::probe_scalar;
using hb::server::probe_text;
using hb::server::item_ledger_store;
using hb::server::ledger_event_record;

namespace
{
	// Scratch file, created and removed by this command. Lives beside the live
	// ledger so it exercises the same filesystem and the same WAL behaviour.
	constexpr const char* probe_db = "itemledger_probe.db";

	// Probe serials well above anything a real world will reach for a long time.
	// They only ever land in the scratch file, but keeping them out of the plausible
	// range means a stray copy is obvious rather than mistakable for real history.
	constexpr int64_t probe_serial_a = 900001;
	constexpr int64_t probe_high_water = 900500;
	constexpr int64_t probe_meta_above = 999999;
	constexpr int32_t probe_flow_item = 90001;
	constexpr int32_t probe_flow_type = 1;

	// Reaches into the closed scratch file to reproduce an on-disk state a crash
	// leaves behind. Going through raw SQL rather than the store's API is the
	// point: these are exactly the states no code path is supposed to produce.
	bool poke_probe_db(const std::string& sql)
	{
		sqlite3* db = nullptr;
		if (sqlite3_open(probe_db, &db) != SQLITE_OK) {
			if (db != nullptr) sqlite3_close(db);
			return false;
		}
		const bool ok = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
		sqlite3_close(db);
		return ok;
	}

	// A meta row rewritten behind the store's back, the way a crash would leave it.
	std::string set_meta_sql(const char* key, int64_t value)
	{
		return "UPDATE meta SET value='" + std::to_string(value) + "' WHERE key='" + key + "';";
	}

	void remove_probe_files()
	{
		std::error_code ec;
		std::filesystem::remove(probe_db, ec);
		std::filesystem::remove(std::string(probe_db) + "-wal", ec);
		std::filesystem::remove(std::string(probe_db) + "-shm", ec);
	}

	// Removes the scratch files however this command returns. Every bail-out below
	// is an early return, and a forgotten cleanup would leave a database full of
	// nine-hundred-thousand-range serials beside the real ledger — which is the
	// one outcome the closing scratch_removed check exists to notice.
	struct probe_cleanup
	{
		~probe_cleanup() { remove_probe_files(); }
	};
}

void CmdLedgerCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("LEDGERCHECK", "ledgercheck");

	//----------------------------------------------------------------------
	// The live store: the two properties that must hold in this process.
	//----------------------------------------------------------------------

	item_ledger_store* live = game->m_item_ledger_store.get();
	if (live == nullptr || !live->is_open()) {
		hb::console::error("ledgercheck: the live ledger is not open.");
		return;
	}

	tally.record("live_wal", probe_text(live->handle(), "PRAGMA journal_mode;") == "wal");

	// Boot must have lifted the allocator above whatever was on disk. Without
	// this the first mint of the run re-issues a Serial a stored item already
	// holds — one Serial in two inventories, which is indistinguishable from a
	// dupe to the tooling that exists to find dupes.
	tally.record("live_allocator_lifted",
		game->m_item_manager->serial_high_water() >= live->recovered_high_water());

	//----------------------------------------------------------------------
	// The scratch store: everything that needs a restart to be observable.
	//----------------------------------------------------------------------

	// A world-less item to hand to record_mint. The factory gives it real config
	// data; the Serial is then overwritten per probe, because the scratch file
	// needs serials it controls and the live allocator must not be dragged into
	// the probe range to supply them.
	const int probe_item_id = find_probe_item(game, false);
	std::unique_ptr<CItem> probe_item{ probe_item_id < 0
		? nullptr
		: game->m_item_manager->create_item(probe_item_id, item_origin::npc_drop) };
	if (probe_item == nullptr) {
		hb::console::error("ledgercheck: no usable non-stackable item id for the probes.");
		return;
	}

	remove_probe_files();
	const probe_cleanup cleanup;

	auto scratch = std::make_unique<item_ledger_store>();

	// Close, optionally rewrite a meta row behind the store's back, reopen. Every
	// recovery case below is that sequence with a different staged state, and the
	// staging is the interesting part — so the sequence itself is written once.
	auto restart = [&](const char* meta_key = nullptr, int64_t value = 0) -> bool
	{
		scratch->close();
		if (meta_key != nullptr && !poke_probe_db(set_meta_sql(meta_key, value))) {
			return false;
		}
		return scratch->open(probe_db);
	};

	// The newest boundary event's detail, for asking what it says about the
	// previous run. One copy of the query, and the event type comes from the enum
	// rather than a literal 103 that only greps as a number.
	auto newest_boundary_says = [&](const char* word)
	{
		return probe_text(scratch->handle(),
			std::format("SELECT detail FROM item_events WHERE event_type={}"
				" ORDER BY event_id DESC LIMIT 1;", static_cast<int>(ledger_event::boundary)).c_str())
			.find(word) != std::string::npos;
	};

	if (!scratch->open(probe_db)) {
		hb::console::error("ledgercheck: could not create the scratch ledger '{}'.", probe_db);
		return;
	}

	tally.record("scratch_wal", probe_text(scratch->handle(), "PRAGMA journal_mode;") == "wal");

	// Recording is a RAM append and nothing else. If a transition ever reached
	// disk synchronously, an item action would be paying for I/O — which is the
	// exact cost D3 refuses to put on the critical path.
	const size_t before_record = scratch->pending_count();
	probe_item->m_serial = probe_serial_a;
	scratch->record_mint(*probe_item, "ledgercheck", "probemap", 10, 20);

	ledger_event_record moved;
	moved.serial = probe_serial_a;
	moved.at = 1;
	moved.event_type = ledger_event::despawned;
	moved.map = "probemap";
	scratch->record_event(moved);

	tally.record("record_buffers",
		scratch->pending_count() == before_record + 3
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_events;") == 0
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_instances;") == 0);

	// One flush, one transaction, whole window on disk. The event count is
	// three because open() buffered the run-boundary marker alongside the mint's
	// birth event and the transition above.
	const bool flushed = scratch->flush(probe_high_water);
	tally.record("flush_persists",
		flushed
		&& scratch->pending_count() == 0
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_instances;") == 1
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_events;") == 3);

	tally.record("high_water_durable",
		probe_scalar(scratch->handle(), "SELECT CAST(value AS INTEGER) FROM meta WHERE key='serial_high_water';")
			== probe_high_water);

	// A Counted item has no identity to be born with, and an event with no Serial
	// is an orphan. Both must be dropped at the door: a birth row for a stackable
	// would claim an instance that stack merges will dissolve, and an event
	// pointing at no instance reads as corruption to Reconciliation.
	probe_item->m_serial = 0;
	scratch->record_mint(*probe_item, "ledgercheck", "probemap", 10, 20);
	tally.record("counted_refused", scratch->pending_count() == 0);

	ledger_event_record orphan;
	orphan.serial = 0;
	orphan.at = 1;
	orphan.event_type = ledger_event::destroyed;
	scratch->record_event(orphan);
	tally.record("orphan_event_refused", scratch->pending_count() == 0);

	// Flow rows are a running total, not a snapshot: the buffer only ever holds
	// one window's delta, so a flush that replaced the row would throw away every
	// earlier window of that day.
	scratch->record_flow(probe_flow_item, probe_flow_type, 3);
	scratch->record_flow(probe_flow_item, probe_flow_type, 3);
	const bool flow_first = scratch->flush(0)
		&& probe_scalar(scratch->handle(), "SELECT qty FROM item_flows;") == 6;
	scratch->record_flow(probe_flow_item, probe_flow_type, 4);
	const bool flow_second = scratch->flush(0)
		&& probe_scalar(scratch->handle(), "SELECT qty FROM item_flows;") == 10
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_flows;") == 1;
	tally.record("flow_accumulates", flow_first && flow_second);

	// The Done-when clause, literally: a separate read-only connection queries the
	// file while the store still holds its own connection open. This is what WAL
	// buys — the P4 reconciliation and analytics tooling never has to stop the
	// world to read history.
	{
		sqlite3* reader = nullptr;
		bool reader_ok = false;
		if (sqlite3_open_v2(probe_db, &reader, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
			reader_ok = probe_scalar(reader, "SELECT COUNT(*) FROM item_events;")
				== probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_events;");
		}
		if (reader != nullptr) sqlite3_close(reader);
		tally.record("external_reader", reader_ok);
	}

	// A flush that cannot write must keep its buffer. Re-minting a Serial the
	// instance table already holds is a deterministic way to make the transaction
	// fail; what is being proven is that the events survive it, because an audit
	// log that discards what it could not write has exactly the gap it exists to
	// prevent.
	probe_item->m_serial = probe_serial_a;
	scratch->record_mint(*probe_item, "ledgercheck", "probemap", 10, 20);
	const size_t held = scratch->pending_count();
	const bool failed_as_expected = scratch->flush(0) == false;
	tally.record("flush_failure_keeps_buffer",
		failed_as_expected
		&& held == 2
		&& scratch->pending_count() == held
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_instances;") == 1);

	// ...and the retry lands once the obstruction is gone. Held-for-later is only
	// a real promise if something eventually collects.
	sqlite3_exec(scratch->handle(), "DELETE FROM item_instances;", nullptr, nullptr, nullptr);
	tally.record("flush_retry_succeeds",
		scratch->flush(0)
		&& scratch->pending_count() == 0
		&& probe_scalar(scratch->handle(), "SELECT COUNT(*) FROM item_instances;") == 1);

	//----------------------------------------------------------------------
	// Restart safety. Each block closes the store, optionally reproduces an
	// on-disk state, and reopens — which is the only way to observe recovery.
	//----------------------------------------------------------------------

	// A plain restart, nothing staged: the durable mark must survive it.
	if (!restart()) {
		hb::console::error("ledgercheck: could not reopen the scratch ledger.");
		return;
	}
	scratch->flush(0);   // land this run's boundary event so it can be read back

	tally.record("restart_recovers_high_water",
		scratch->recovered_high_water() == probe_high_water);

	// A clean stop is distinguishable from a crash, which is what makes the
	// boundary marker worth writing: it tells a forensic query whether the events
	// before it are all there or whether the tail of that run was lost.
	tally.record("boundary_after_clean_stop", newest_boundary_says("\"clean\""));

	// A lost flush leaves the durable mark behind the instances it was supposed
	// to cover. Recovery must follow the table, or the next mint re-issues a
	// Serial that is already on disk.
	if (!restart("serial_high_water", 1)) {
		hb::console::error("ledgercheck: could not stage the stale-high-water state.");
		return;
	}
	tally.record("recovery_takes_instance_max",
		scratch->recovered_high_water() == probe_serial_a);

	// And the mirror image: an instance row whose flush was lost leaves the table
	// behind the mark, so recovery must follow the mark instead.
	if (!restart("serial_high_water", probe_meta_above)) {
		hb::console::error("ledgercheck: could not stage the stale-instances state.");
		return;
	}
	tally.record("recovery_takes_meta",
		scratch->recovered_high_water() == probe_meta_above);

	// The crash branch of the boundary marker. Reproducing it means leaving the
	// run marker set, which is precisely what a process that never reached its
	// shutdown path leaves behind.
	if (!restart("clean_shutdown", 0)) {
		hb::console::error("ledgercheck: could not stage the crash state.");
		return;
	}
	scratch->flush(0);
	tally.record("boundary_after_crash", newest_boundary_says("\"crash\""));

	// The scratch database has served its purpose; leaving it behind would put a
	// file full of nine-hundred-thousand-range serials next to the real ledger.
	// (The cleanup guard would do this on every exit path — done explicitly here
	// only so the removal can be asserted.)
	scratch.reset();
	remove_probe_files();
	tally.record("scratch_removed", !std::filesystem::exists(probe_db));

	tally.report();
}
