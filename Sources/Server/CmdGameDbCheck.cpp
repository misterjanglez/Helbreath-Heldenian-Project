// CmdGameDbCheck.cpp: Persistence v8 prover (ADR 0004 + ADR 0003, plan P1.2, #77)
//
// v8 collapsed the per-account database files into one game.db and gave every
// item row a Serial. Both halves are the kind of change that compiles perfectly
// and is wrong in production, because what they alter is what the *file* means:
//
//   - Uniqueness used to be a directory scan and is now a constraint. If the
//     collation on the name columns is wrong, two characters called "Bob" and
//     "bob" can exist, and the scan that used to prevent that is gone.
//   - Scoping used to be the filename. A table that lost its owner column, or a
//     DELETE that lost its WHERE, now reaches every account instead of one.
//   - A Serial that does not survive a save/load round trip turns one item's
//     Biography into two, which is exactly what Reconciliation reports as a dupe.
//   - The mass-save sweep is now one transaction. If nesting were plain
//     BEGIN/COMMIT, one character failing to save would roll back everybody's.
//
// So this proves those properties rather than the code paths that implement
// them. The live game.db is only read (its connection posture); everything that
// needs writes runs against a scratch file carrying the same v8 schema, and the
// pre-v8 refusals get a scratch accounts/ directory of their own.
//
// The machine-readable lines carry no serials, timestamps or row counts on
// purpose: the Linux gate compares them against the Windows run byte for byte.
//
// Design contract: PLANS/ItemLedger_Plan.md P1.2, docs/adr/0004-single-game-db.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdGameDbCheck.h"

#include "AccountSqliteStore.h"
#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "GameDatabase.h"
#include "Item.h"
#include "ItemManager.h"
#include "ServerConsole.h"
#include "sqlite3.h"

#include <filesystem>
#include <memory>
#include <string>

using hb::server::check_tally;
using hb::server::game_database;
using hb::server::game_db;
using hb::server::probe_scalar;
using hb::server::probe_text;

namespace
{
	// Well away from anything a real world would hold, so a probe row that
	// somehow escaped cleanup is recognisable on sight.
	constexpr int64_t probe_serial = 7700000;

	constexpr const char* probe_db = "gamedbcheck_probe.db";
	constexpr const char* probe_accounts_dir = "gamedbcheck_probe_accounts";

	bool exec(sqlite3* db, const std::string& sql)
	{
		return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	// An account plus one character, the minimum a per-character table's foreign
	// key will accept.
	bool seed_character(sqlite3* db, const char* account, const char* character)
	{
		return exec(db, std::string(
			"INSERT INTO accounts(account_name, password_hash, password_salt, email,"
			" created_at, password_changed_at, last_ip)"
			" VALUES('") + account + "','h','s','e','t','t','ip');")
			&& exec(db, std::string(
			"INSERT INTO characters(character_name, account_name, created_at, level, exp,"
			" map_name, map_x, map_y, hp, mp, sp, str, vit, dex, intl, mag, chr,"
			" gender, skin, hairstyle, haircolor, underwear)"
			" VALUES('") + character + "','" + account + "','t',1,0,'default',1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0);");
	}

	void remove_probe_files()
	{
		std::error_code ec;
		hb::server::remove_probe_db(probe_db);
		std::filesystem::remove_all(probe_accounts_dir, ec);
	}

	// Cleans up however this command returns. Every bail-out below is an early
	// return, and a forgotten scratch file would sit next to the real game.db
	// looking like a world.
	struct probe_cleanup
	{
		~probe_cleanup() { remove_probe_files(); }
	};
}

void CmdGameDbCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("GAMEDBCHECK", "gamedbcheck");

	//----------------------------------------------------------------------
	// The live connection: the posture ADR 0004 requires of this process.
	//----------------------------------------------------------------------

	if (!game_db().is_open()) {
		hb::console::error("gamedbcheck: the live game.db is not open.");
		return;
	}
	sqlite3* live = game_db().handle();

	// WAL is not a performance preference here. It is what lets an external
	// process read the live world (P4 reconciliation, analytics) without
	// stopping it, and it is the mode in which a cross-account transaction is
	// atomic at all — which is the entire reason the per-account files went away.
	tally.record("live_wal", probe_text(live, "PRAGMA journal_mode;") == "wal");

	// 1 == NORMAL. Snapshot saves already accept losing the window since the
	// last save, so FULL would buy durability the design does not claim.
	tally.record("live_synchronous_normal", probe_scalar(live, "PRAGMA synchronous;") == 1);

	// Per-connection, not stored in the file: without it every ON DELETE CASCADE
	// in the schema is decoration and DeleteCharacterData silently orphans rows.
	tally.record("live_foreign_keys", probe_scalar(live, "PRAGMA foreign_keys;") == 1);

	tally.record("live_schema_v9",
		probe_text(live, "SELECT value FROM meta WHERE key='schema_version';") == "9");

	//----------------------------------------------------------------------
	// The refusals. Neither has a migration behind it (D6), so both have to
	// stop the boot rather than quietly produce an empty world.
	//----------------------------------------------------------------------

	probe_cleanup cleanup;
	remove_probe_files();

	{
		std::error_code ec;
		std::filesystem::create_directories(probe_accounts_dir, ec);
		std::string example;
		int count = 0;
		const bool empty_dir_ok = !LegacyAccountLayoutPresent(probe_accounts_dir, example, count);

		// A directory with account databases in it is a pre-v8 world.
		sqlite3* legacy = nullptr;
		const std::string legacy_path = std::string(probe_accounts_dir) + "/probeacct.db";
		if (sqlite3_open(legacy_path.c_str(), &legacy) == SQLITE_OK) {
			exec(legacy, "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
			sqlite3_close(legacy);
		}
		const bool detected = LegacyAccountLayoutPresent(probe_accounts_dir, example, count);

		// Both directions, because a detector that always fires would refuse
		// every boot and a detector that never fires would refuse none — and
		// only the second of those is visible in testing.
		tally.record("legacy_layout_absent_ok", empty_dir_ok);
		tally.record("legacy_layout_refused", detected && count == 1 && example == "probeacct.db");
	}

	{
		// A database stamped below 9 must be refused, not upgraded in place.
		// Stamped 8 rather than something absurd: the immediately-previous epoch
		// is the version a real stale world would carry, and it is the one a
		// gate written as "less than" instead of "not equal" would let through.
		sqlite3* stale = nullptr;
		bool refused = false;
		if (sqlite3_open(probe_db, &stale) == SQLITE_OK) {
			if (exec(stale, "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);")
				&& exec(stale, "INSERT INTO meta(key,value) VALUES('schema_version','8');")) {
				refused = !EnsureGameSchema(stale, "gamedbcheck(stale)");
			}
			sqlite3_close(stale);
		}
		tally.record("stale_schema_refused", refused);
		remove_probe_files();
	}

	{
		// The third refusal, new in v9: a surviving tradingpost.db. Every item on
		// that board was physically removed from a character (ADR 0001), so a
		// server that booted past it would present a world in which those items
		// simply do not exist and never say why. Both directions again — a
		// detector that fires on an absent file refuses every boot.
		const bool absent_ok = !LegacyTradingPostFilePresent(probe_db);
		sqlite3* legacy_board = nullptr;
		if (sqlite3_open(probe_db, &legacy_board) == SQLITE_OK) {
			sqlite3_close(legacy_board);
		}
		tally.record("legacy_tradingpost_absent_ok", absent_ok);
		tally.record("legacy_tradingpost_refused", LegacyTradingPostFilePresent(probe_db));
		remove_probe_files();
	}

	//----------------------------------------------------------------------
	// The scratch v8 world: everything that needs writes.
	//----------------------------------------------------------------------

	game_database scratch;
	if (!scratch.open(probe_db) || !EnsureGameSchema(scratch.handle(), probe_db)) {
		hb::console::error("gamedbcheck: could not create the scratch database '{}'.", probe_db);
		return;
	}
	sqlite3* db = scratch.handle();

	tally.record("fresh_schema_v9",
		probe_text(db, "SELECT value FROM meta WHERE key='schema_version';") == "9");

	// v9's whole point: the Trading Post's escrow tables are in THIS database,
	// so a custody move and the character save it pairs with can be one
	// transaction (#82). A schema that laid them down elsewhere would leave
	// every listing two commits away from the inventory it came from.
	tally.record("escrow_tables_present",
		probe_scalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table'"
			" AND name IN ('listings','listing_items','offers','offer_items','notices');") == 5);

	if (!seed_character(db, "probeacct", "ProbeOne")) {
		hb::console::error("gamedbcheck: could not seed the scratch world.");
		return;
	}

	// Global uniqueness. Before v8 this was a walk over every account file; the
	// PRIMARY KEY is what replaced it, and a second account claiming the same
	// character name has to bounce off the constraint.
	tally.record("character_name_unique",
		exec(db, "INSERT INTO accounts(account_name, password_hash, password_salt, email,"
			" created_at, password_changed_at, last_ip)"
			" VALUES('probetwo','h','s','e','t','t','ip');")
		&& !exec(db, "INSERT INTO characters(character_name, account_name, created_at, level, exp,"
			" map_name, map_x, map_y, hp, mp, sp, str, vit, dex, intl, mag, chr,"
			" gender, skin, hairstyle, haircolor, underwear)"
			" VALUES('ProbeOne','probetwo','t',1,0,'default',1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0);"));

	// ...and case-insensitively, because the scan it replaced compared NOCASE.
	// A BINARY collation here would quietly start allowing "probeone" beside
	// "ProbeOne", which is a name-impersonation hole rather than a schema detail.
	tally.record("character_name_nocase",
		!exec(db, "INSERT INTO characters(character_name, account_name, created_at, level, exp,"
			" map_name, map_x, map_y, hp, mp, sp, str, vit, dex, intl, mag, chr,"
			" gender, skin, hairstyle, haircolor, underwear)"
			" VALUES('probeone','probetwo','t',1,0,'default',1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0);"));

	//----------------------------------------------------------------------
	// Serials through persistence — the other half of the v8 epoch.
	//----------------------------------------------------------------------

	{
		AccountDbItemRow row{};
		row.slot = 0;
		row.item_id = 1;
		row.serial = probe_serial;
		row.count = 1;
		row.cur_durability = 10;

		AccountDbItemRow counted{};
		counted.slot = 1;
		counted.item_id = 2;
		counted.serial = 0;          // Counted: stackables carry no identity (D2)
		counted.count = 25;

		AccountDbBankItemRow bank{};
		bank.slot = 0;
		bank.item_id = 1;
		bank.serial = probe_serial + 1;
		bank.count = 1;
		bank.cur_durability = 10;

		const bool wrote = InsertCharacterItems(db, "ProbeOne", { row, counted })
			&& InsertCharacterBankItems(db, "ProbeOne", { bank });

		std::vector<AccountDbItemRow> loaded;
		std::vector<AccountDbBankItemRow> loadedBank;
		const bool read = wrote
			&& LoadCharacterItems(db, "ProbeOne", loaded)
			&& LoadCharacterBankItems(db, "ProbeOne", loadedBank);

		tally.record("inventory_serial_round_trip",
			read && loaded.size() == 2 && loaded[0].serial == probe_serial);

		// 0 has to survive as 0. A load path that "helpfully" filled it in would
		// mint a Serial for a stack on every login, and the flow counters would
		// be accounting for instances that never existed.
		tally.record("counted_serial_stays_zero",
			read && loaded.size() == 2 && loaded[1].serial == 0);

		tally.record("warehouse_serial_round_trip",
			read && loadedBank.size() == 1 && loadedBank[0].serial == probe_serial + 1);

		// The name lookup is case-insensitive on the child tables too. These
		// columns are not the PRIMARY KEY, so their collation is a separate
		// decision — and getting it wrong loses a character's inventory on load
		// rather than erroring.
		std::vector<AccountDbItemRow> byOtherCase;
		tally.record("item_lookup_nocase",
			LoadCharacterItems(db, "probeone", byOtherCase) && byOtherCase.size() == 2);
	}

	// The same round trip through the path that actually runs in production.
	// The checks above drive InsertCharacterItems, which the login-time character
	// creation and the Trading Post use; every ordinary save goes through
	// SaveCharacterSnapshot instead, and that one reads the Serial off the live
	// CItem rather than off a row somebody already filled in. A column wired into
	// one and not the other would pass everything above and still lose every
	// player's Serials at the first autosave.
	if (game != nullptr && game->m_item_manager != nullptr && G_pIOPool != nullptr) {
		const int probe_item = hb::server::find_probe_item(game, false);
		std::unique_ptr<CClient> probe(new CClient(G_pIOPool->get_context()));
		std::snprintf(probe->m_account_name, sizeof(probe->m_account_name), "%s", "probeacct");
		std::snprintf(probe->m_char_name, sizeof(probe->m_char_name), "%s", "ProbeOne");
		std::snprintf(probe->m_map_name, sizeof(probe->m_map_name), "%s", "default");

		// Minted, not handed a constant: this keeps the live allocator's
		// high-water where the run left it instead of dragging it up to a probe
		// value that then becomes the floor for every later Serial.
		probe->m_item_list[0] = game->m_item_manager->restore_item(probe_item, 0);
		const int64_t minted = probe->m_item_list[0] != nullptr ? probe->m_item_list[0]->m_serial : 0;

		std::vector<AccountDbItemRow> saved;
		const bool round_tripped = minted != 0
			&& SaveCharacterSnapshot(db, probe.get())
			&& LoadCharacterItems(db, "ProbeOne", saved)
			&& saved.size() == 1
			&& saved[0].serial == minted;

		tally.record("snapshot_serial_round_trip", round_tripped);
	}
	else {
		tally.record("snapshot_serial_round_trip", false, "no game/io context");
	}

	// One database means one place for a character's rows, so the CASCADE has to
	// actually reach them: an orphaned inventory in a shared table is a row set
	// that the next character to take that name would inherit.
	{
		const bool deleted = DeleteCharacterData(db, "ProbeOne");
		tally.record("delete_cascades_items",
			deleted
			&& probe_scalar(db, "SELECT COUNT(*) FROM character_items;") == 0
			&& probe_scalar(db, "SELECT COUNT(*) FROM character_bank_items;") == 0);
	}

	//----------------------------------------------------------------------
	// Block lists: the one table that had no owner column before v8, because
	// the file it lived in was the owner.
	//----------------------------------------------------------------------

	{
		const std::vector<std::pair<std::string, std::string>> one = { {"badguy", "BadGuy"} };
		const std::vector<std::pair<std::string, std::string>> two = { {"pest", "Pest"} };

		std::vector<std::pair<std::string, std::string>> readBack;
		const bool saved = SaveBlockList(db, "probeacct", one)
			&& SaveBlockList(db, "probetwo", two);

		tally.record("block_list_scoped_read",
			saved && LoadBlockList(db, "probeacct", readBack)
			&& readBack.size() == 1 && readBack[0].first == "badguy");

		// The save is delete-then-insert. Unscoped, the delete would clear every
		// player's list on any player's save — silently, and only at logout.
		SaveBlockList(db, "probeacct", {});
		tally.record("block_list_scoped_write",
			LoadBlockList(db, "probetwo", readBack) && readBack.size() == 1
			&& readBack[0].first == "pest");
	}

	//----------------------------------------------------------------------
	// The connection's own machinery.
	//----------------------------------------------------------------------

	{
		// Same SQL twice compiles once. This is the difference between the save
		// sweep preparing fifteen statements per character and preparing them
		// once for the life of the process.
		const size_t before = scratch.cached_statement_count();
		sqlite3_stmt* first = scratch.cached("SELECT COUNT(*) FROM accounts;");
		sqlite3_stmt* second = scratch.cached("SELECT COUNT(*) FROM accounts;");
		tally.record("statement_cache_reuses",
			first != nullptr && first == second
			&& scratch.cached_statement_count() == before + 1);
	}

	{
		// The nesting rule, which is what makes a batched sweep safe. An inner
		// scope that fails must undo only itself: with plain BEGIN/COMMIT the
		// inner ROLLBACK would end the outer transaction and take every other
		// character's snapshot with it, which is strictly worse than the
		// per-file layout this replaced.
		const bool outer = scratch.begin();
		exec(db, "INSERT INTO accounts(account_name, password_hash, password_salt, email,"
			" created_at, password_changed_at, last_ip)"
			" VALUES('outeracct','h','s','e','t','t','ip');");

		const bool inner = scratch.begin();
		const bool nested_deeper = scratch.transaction_depth() == 2;
		exec(db, "INSERT INTO accounts(account_name, password_hash, password_salt, email,"
			" created_at, password_changed_at, last_ip)"
			" VALUES('inneracct','h','s','e','t','t','ip');");
		const bool undone = scratch.rollback();

		const bool committed = scratch.commit();

		tally.record("nested_scope_deepens", outer && inner && nested_deeper);
		tally.record("nested_rollback_isolated",
			undone && committed
			&& probe_scalar(db, "SELECT COUNT(*) FROM accounts WHERE account_name='inneracct';") == 0
			&& probe_scalar(db, "SELECT COUNT(*) FROM accounts WHERE account_name='outeracct';") == 1);
		tally.record("nested_depth_unwinds", scratch.transaction_depth() == 0);
	}

	scratch.close();

	// The cleanup itself is a check: a prover that leaves a database called
	// gamedbcheck_probe.db beside the real world has produced a second world.
	remove_probe_files();
	tally.record("scratch_removed",
		!std::filesystem::exists(probe_db) && !std::filesystem::exists(probe_accounts_dir));

	tally.report();
}
