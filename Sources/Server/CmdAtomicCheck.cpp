// CmdAtomicCheck.cpp: the multi-account atomicity prover (Item Provenance Ledger P3.4, #82)
//
// ADR 0004 replaced the per-account database files with one game.db for a reason
// that no amount of careful code could work around: a SQLite transaction that
// spans two files is not atomic in WAL mode. Every operation touching two
// accounts was therefore two commits with a window between them, and a crash in
// that window duplicated an item or destroyed it. The consolidation made a fix
// POSSIBLE; this ticket is the fix, and this command is what says so.
//
// The claim being proved is one sentence: **no multi-account operation spans two
// commits.** That splits into two things worth catching separately, because a
// regression in either one looks exactly like working code:
//
//   - **The operation reaches the commit at all.** An Exchange used to save
//     neither party. Both halves became durable at whatever unrelated later
//     moments each character happened to be saved — a logout, a map change, an
//     autosave — so the trade was split for as long as that took. A check that
//     only inspected memory would have passed then and passes now.
//   - **The commit is all-or-nothing.** Half of a Trading Post finalize is worse
//     than none of it: the Listing is gone, one side has their items, the other
//     has nothing. So the interesting checks here are the ones where the
//     database REFUSES half way through, which is arranged with a trigger that
//     aborts a particular insert, and the question asked afterwards is not "did
//     it fail cleanly" but "is the world exactly as it was".
//
// Memory is checked as well as disk, and that is not belt-and-braces. A SQLite
// rollback rewinds rows; it does not empty a Warehouse that a delivery already
// filled in RAM. If the undo half is ever dropped, the rows will say the item is
// still listed and the recipient will be holding it — a dupe that a
// disk-only prover would report as a clean rollback.
//
// Three things are moved aside for the run:
//
//   - the ledger sink, redirected to a scratch database (an append-only log
//     cannot have test rows deleted afterwards, so the sink moves instead);
//   - the Trading Post store, pointed at a scratch WORLD rather than the game's
//     — since v9 the escrow tables are part of the game.db schema, so the
//     scratch file has to be a whole one, characters and all;
//   - g_login, nulled for the escrow half, so nothing there can reach the live
//     save path.
//
// The Exchange and Give checks are the exception and are deliberate: they drive
// the real save, which means the live game.db. Two probe accounts go in and are
// deleted again however this command returns, and the run refuses to start if
// either name is already taken. Testing them against a scratch world instead
// would prove the primitive and say nothing about whether the trade path ever
// calls it — which is the half that was missing.
//
// Design contract: PLANS/ItemLedger_Plan.md P3.4, docs/adr/0004-single-game-db.md,
// docs/adr/0001-trading-post-physical-escrow.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdAtomicCheck.h"

#include "AccountSqliteStore.h"
#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "GameDatabase.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "LoginServer.h"
#include "Map.h"
#include "Packet/PacketTradingPost.h"
#include "ServerConsole.h"
#include "ServerMessages.h"
#include "TradingPostStore.h"
#include "sqlite3.h"

#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace item_origin = hb::server::item_origin;
namespace ItemLogAction = hb::server::net::ItemLogAction;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::find_probe_item;
using hb::server::game_database;
using hb::server::probe_client;
using hb::server::seed_probe_account;
using hb::server::trading_post_store;

namespace
{
	constexpr const char* probe_ledger_db = "atomiccheck_probe.db";
	constexpr const char* probe_world_db  = "atomiccheck_world.db";

	// Character, account and map names are char[11], and one that overflows is
	// silently truncated on the way in — a probe with a longer name would compare
	// its untruncated constant against the server's shortened copy and fail
	// everywhere at once, looking exactly like an emitter that never ran.
	constexpr const char* live_account_a = "atmacctA";
	constexpr const char* live_char_a    = "AtomGive";
	constexpr const char* live_account_b = "atmacctB";
	constexpr const char* live_char_b    = "AtomTake";
	constexpr const char* live_account_c = "atmacctC";
	constexpr const char* live_char_c    = "AtomNone";

	constexpr const char* seller_account = "atmacctS";
	constexpr const char* seller_char    = "AtomSell";
	constexpr const char* buyer_account  = "atmacctW";
	constexpr const char* buyer_char     = "AtomWin";
	constexpr const char* loser_account  = "atmacctL";
	constexpr const char* loser_char     = "AtomLose";

	constexpr const char* probe_map = "atmmap";
	constexpr int probe_x = 10;
	constexpr int probe_y = 10;

	bool exec(sqlite3* db, const std::string& sql)
	{
		return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	// Deleting the account takes the character and every row hanging off it
	// (ON DELETE CASCADE, which game_database::open's foreign_keys pragma is what
	// makes real).
	void forget_account(sqlite3* db, const char* account)
	{
		exec(db, std::string("DELETE FROM accounts WHERE account_name='") + account + "';");
	}

	// Make the next insert into `table` fail, and stop making it fail. This is
	// how an operation is interrupted where it matters — inside the transaction,
	// after some of its work has been done — without a test-only branch in the
	// code being tested. RAISE(ABORT) undoes the statement and leaves the
	// transaction open, which is exactly the shape of a real constraint failure:
	// the code under test is what has to decide to roll back.
	bool arm_insert_failure(sqlite3* db, const char* table)
	{
		return exec(db, std::string("CREATE TRIGGER atomiccheck_abort BEFORE INSERT ON ")
			+ table + " BEGIN SELECT RAISE(ABORT, 'atomiccheck'); END;");
	}

	void disarm_insert_failure(sqlite3* db)
	{
		exec(db, "DROP TRIGGER IF EXISTS atomiccheck_abort;");
	}

	// A minted probe item holding `count` of itself. init_item_attr copies the
	// config and leaves the count at zero — every creation venue supplies its own
	// — and a bundle asking for one of a stack of zero is rejected as invalid
	// before anything is recorded.
	CItem* mint_probe(ItemManager& items, int item_id, uint64_t count)
	{
		CItem* item = items.create_item(item_id, item_origin::gm_mint);
		if (item != nullptr) item->m_instance.count = count;
		return item;
	}

	// Puts an item the caller owns into an inventory slot and hands back its
	// Serial. The client takes ownership: ~CClient frees whatever is still in the
	// slot.
	int64_t place_in_inventory(CGame* game, int client_h, int slot, CItem* item)
	{
		CClient* client = game->m_client_list[client_h];
		client->m_item_list[slot] = item;
		client->m_is_item_equipped[slot] = false;
		return item->m_serial;
	}

	// Enough carry capacity and level for the Exchange's weight test to pass, and
	// a real map index. A probe left at zero strength has a maximum load of zero
	// and the trade is refused for a reason that has nothing to do with what is
	// being proved; a probe left at the CClient default map index of -1 makes
	// every `m_map_list[m_map_index]` in the give path an out-of-bounds read.
	void make_playable(CGame* game, int client_h)
	{
		CClient* client = game->m_client_list[client_h];
		client->m_str = 50;
		client->m_level = 50;
		client->m_hp = 100;
		client->m_mp = 100;
		client->m_sp = 100;
		client->m_map_index = 0;
		client->m_x = static_cast<short>(probe_x);
		client->m_y = static_cast<short>(probe_y);
	}

	// Whether a character's SAVED inventory (or Warehouse) holds this Serial.
	// This is the question the whole ticket turns on: memory holding it proves
	// the operation ran, and only the row proves it survived a restart.
	bool saved_inventory_holds(sqlite3* db, const char* character, int64_t serial)
	{
		std::vector<AccountDbItemRow> rows;
		if (!LoadCharacterItems(db, character, rows)) return false;
		for (const auto& r : rows) if (r.serial == serial) return true;
		return false;
	}

	bool saved_bank_holds(sqlite3* db, const char* character, int64_t serial)
	{
		std::vector<AccountDbBankItemRow> rows;
		if (!LoadCharacterBankItems(db, character, rows)) return false;
		for (const auto& r : rows) if (r.serial == serial) return true;
		return false;
	}

	bool memory_inventory_holds(CGame* game, int client_h, int64_t serial)
	{
		const CClient* client = game->m_client_list[client_h];
		if (client == nullptr) return false;
		for (int i = 0; i < hb::shared::limits::MaxItems; i++)
			if (client->m_item_list[i] != nullptr && client->m_item_list[i]->m_serial == serial)
				return true;
		return false;
	}

	bool memory_bank_holds(CGame* game, int client_h, int64_t serial)
	{
		const CClient* client = game->m_client_list[client_h];
		if (client == nullptr) return false;
		for (int i = 0; i < hb::shared::limits::MaxBankItems; i++)
			if (client->m_item_in_bank_list[i] != nullptr
				&& client->m_item_in_bank_list[i]->m_serial == serial)
				return true;
		return false;
	}

	bool character_row_exists(sqlite3* db, const char* character)
	{
		return hb::server::probe_scalar(db, (std::string(
			"SELECT COUNT(*) FROM characters WHERE character_name='") + character + "';").c_str()) == 1;
	}

	// One escrow bundle as the wire would describe it.
	hb::net::TpEscrowSlot escrow_slot(int slot, int amount)
	{
		hb::net::TpEscrowSlot s{};
		s.inv_slot = static_cast<uint8_t>(slot);
		s.amount = amount;
		return s;
	}

	// Removes the character-save path for the escrow half of the run and puts it
	// back afterwards. The Trading Post saves through its own connection now, so
	// this is a guard rather than the mechanism: nothing the escrow flows touch
	// should reach the live world, and if something new ever does, it does
	// nothing instead of writing a probe character into it.
	struct login_hold
	{
		LoginServer* live = g_login;
		login_hold() { g_login = nullptr; }
		~login_hold() { g_login = live; }

		login_hold(const login_hold&) = delete;
		login_hold& operator=(const login_hold&) = delete;
	};
}

void CmdAtomicCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("ATOMICCHECK", "atomiccheck");

	if (game == nullptr || game->m_item_manager == nullptr
		|| game->m_item_ledger_store == nullptr || !game->m_item_ledger_store->is_open())
	{
		hb::console::error("atomiccheck: the live ledger is not open.");
		return;
	}
	if (!hb::server::game_db().is_open())
	{
		hb::console::error("atomiccheck: the live game.db is not open.");
		return;
	}
	if (G_pIOPool == nullptr)
	{
		hb::console::error("atomiccheck: no I/O pool for the probe clients.");
		return;
	}

	ItemManager& items = *game->m_item_manager;
	sqlite3* live = hb::server::game_db_handle();

	const int probe_item = find_probe_item(game, false);
	if (!items.is_valid_item_id(probe_item))
	{
		hb::console::error("atomiccheck: no usable probe item id.");
		return;
	}

	//----------------------------------------------------------------------
	// The Exchange and the Give, against the LIVE game.db.
	//
	// Refused rather than merged if any of the three names is already taken:
	// the cleanup below deletes accounts by name, and running on top of a real
	// one would delete a real player.
	//----------------------------------------------------------------------

	const char* live_accounts[] = { live_account_a, live_account_b, live_account_c };
	const char* live_chars[] = { live_char_a, live_char_b, live_char_c };
	for (const char* name : live_accounts)
	{
		if (AccountNameExists(name))
		{
			hb::console::error("atomiccheck: account '{}' exists - refusing to run.", name);
			return;
		}
	}
	for (const char* name : live_chars)
	{
		if (CharacterNameExistsGlobally(name))
		{
			hb::console::error("atomiccheck: character '{}' exists - refusing to run.", name);
			return;
		}
	}

	// However this command returns, including every early bail-out below.
	struct live_cleanup
	{
		sqlite3* db;
		~live_cleanup()
		{
			for (const char* account : { live_account_a, live_account_b, live_account_c })
				forget_account(db, account);
		}
	} cleanup{ live };

	const int handle_a = find_free_handle(game, 1);
	const int handle_b = handle_a > 0 ? find_free_handle(game, handle_a + 1) : -1;
	const int handle_c = handle_b > 0 ? find_free_handle(game, handle_b + 1) : -1;
	if (handle_a < 0 || handle_b < 0 || handle_c < 0)
	{
		hb::console::error("atomiccheck: no free client handles for the probes.");
		return;
	}

	{
		const probe_client probe_a(game, handle_a, live_account_a, live_char_a, probe_map, probe_x, probe_y);
		const probe_client probe_b(game, handle_b, live_account_b, live_char_b, probe_map, probe_x, probe_y);
		const probe_client probe_c(game, handle_c, live_account_c, live_char_c, probe_map, probe_x, probe_y);
		make_playable(game, handle_a);
		make_playable(game, handle_b);
		make_playable(game, handle_c);

		if (!seed_probe_account(live, live_account_a) || !seed_probe_account(live, live_account_b))
		{
			hb::console::error("atomiccheck: could not seed the live probe accounts.");
			return;
		}

		//------------------------------------------------------------------
		// The primitive. Character C has no account row, so the schema itself
		// refuses its save — no mock, no injected failure, the same constraint
		// a corrupted world would hit.
		//------------------------------------------------------------------

		{
			const int batch[] = { handle_a, handle_c };
			const bool reported = g_login != nullptr && g_login->save_players_atomic(batch, 2);

			tally.record("atomic_save_reports_failure", !reported);

			// The load-bearing half: A's save SUCCEEDED and must still have been
			// undone. A version that saved each character in its own transaction
			// would leave A written and only report the failure, which is the
			// exact shape of the split this ticket removes.
			tally.record("atomic_save_undoes_the_one_that_worked",
				!character_row_exists(live, live_char_a));

			tally.record("atomic_save_writes_nothing_on_failure",
				!character_row_exists(live, live_char_c));
		}

		{
			// The same batch with both accounts present: now every character is
			// written. Both directions, because a primitive that never wrote
			// anything would pass every check above.
			seed_probe_account(live, live_account_c);
			const int batch[] = { handle_a, handle_c };
			const bool reported = g_login != nullptr && g_login->save_players_atomic(batch, 2);

			tally.record("atomic_save_writes_every_character",
				reported && character_row_exists(live, live_char_a)
				&& character_row_exists(live, live_char_c));
		}

		{
			// A handle whose client has gone is an ordinary case — a trade
			// partner who disconnected inside the operation — and must not fail
			// the batch or stop the rest of it being written.
			CClient* held = game->m_client_list[handle_c];
			game->m_client_list[handle_c] = nullptr;
			const int batch[] = { handle_b, handle_c };
			const bool reported = g_login != nullptr && g_login->save_players_atomic(batch, 2);
			game->m_client_list[handle_c] = held;

			tally.record("atomic_save_skips_a_gone_client",
				reported && character_row_exists(live, live_char_b));
		}

		//------------------------------------------------------------------
		// The Exchange. Both parties' inventories change; both have to be
		// durable when it returns.
		//------------------------------------------------------------------

		{
			CItem* traded = mint_probe(items, probe_item, 1);
			if (traded == nullptr)
			{
				hb::console::error("atomiccheck: could not mint the Exchange probe item.");
				return;
			}
			const int64_t traded_serial = place_in_inventory(game, handle_a, 0, traded);

			// Made durable on the giver FIRST. Without this the giver's saved
			// inventory would be empty either way, and "the giver no longer has
			// it" would pass whether or not the Exchange saved anybody — the
			// vacuous half of a two-sided claim, and the half that would hide a
			// regression where only the receiver is written.
			{
				const int giver_only[] = { handle_a };
				if (g_login != nullptr) g_login->save_players_atomic(giver_only, 1);
			}
			const bool giver_had_it = saved_inventory_holds(live, live_char_a, traded_serial);

			CClient* a = game->m_client_list[handle_a];
			CClient* b = game->m_client_list[handle_b];

			a->m_is_exchange_mode = true;
			a->m_exchange_h = handle_b;
			std::snprintf(a->m_exchange_name, sizeof(a->m_exchange_name), "%s", live_char_b);
			a->exchange_count = 1;
			a->m_exchange_item_index[0] = 0;
			a->m_exchange_item_amount[0] = 1;
			a->m_exchange_item_id[0] = traded->m_id_num;

			b->m_is_exchange_mode = true;
			b->m_exchange_h = handle_a;
			std::snprintf(b->m_exchange_name, sizeof(b->m_exchange_name), "%s", live_char_a);
			b->exchange_count = 0;

			// Both sides have to confirm; the second confirmation is what commits.
			items.confirm_exchange_item(handle_b);
			items.confirm_exchange_item(handle_a);

			tally.record("exchange_moved_the_item",
				memory_inventory_holds(game, handle_b, traded_serial));

			// The two halves of the claim, read off the database rather than off
			// memory. Before this the Exchange saved neither party, so both of
			// these were false until something unrelated happened to save each
			// character — and whichever landed first, a crash after it left the
			// world holding one half of the trade.
			tally.record("exchange_saved_the_receiver",
				saved_inventory_holds(live, live_char_b, traded_serial));
			tally.record("exchange_saved_the_giver",
				giver_had_it && !saved_inventory_holds(live, live_char_a, traded_serial));
		}

		//------------------------------------------------------------------
		// The Give. Same shape, different door.
		//------------------------------------------------------------------

		{
			CItem* handed = mint_probe(items, probe_item, 1);
			if (handed == nullptr)
			{
				hb::console::error("atomiccheck: could not mint the Give probe item.");
				return;
			}
			const int64_t handed_serial = place_in_inventory(game, handle_a, 0, handed);

			CClient* a = game->m_client_list[handle_a];
			CClient* b = game->m_client_list[handle_b];

			// give_item_handler resolves the recipient from the map tile the
			// giver names, so the recipient has to actually stand on one. Both
			// probes go onto whichever map index the client struct starts at,
			// which is a real loaded map — the give path only asks that tile who
			// owns it, and puts the answer back afterwards is nobody's job here
			// because the tile is chosen well away from the spawn corner.
			const short give_x = static_cast<short>(probe_x + 1);
			const short give_y = static_cast<short>(probe_y);
			CMap* map = game->m_map_list[a->m_map_index];
			if (map == nullptr)
			{
				hb::console::error("atomiccheck: no map to stand the Give probes on.");
				return;
			}

			// Whoever owned that tile gets it back. This is a live map, and a
			// prover that left a tile claiming to be owned by a client handle it
			// has just freed would hand the next player to walk there a dangling
			// owner.
			short prior_owner = 0;
			char prior_class = 0;
			map->get_owner(&prior_owner, &prior_class, give_x, give_y);

			b->m_x = give_x;
			b->m_y = give_y;
			map->set_owner(static_cast<short>(handle_b),
				hb::shared::owner_class::Player, give_x, give_y);

			items.give_item_handler(handle_a, 0, 1, give_x, give_y, 0, nullptr);

			map->set_owner(prior_owner, prior_class, give_x, give_y);

			// The transfer is asserted, not assumed: a Give that silently dropped
			// the item on the ground would make every claim about its save
			// trivially true.
			tally.record("give_moved_the_item",
				memory_inventory_holds(game, handle_b, handed_serial));
			tally.record("give_saved_both_parties",
				saved_inventory_holds(live, live_char_b, handed_serial)
				&& !saved_inventory_holds(live, live_char_a, handed_serial));
		}
	}

	//----------------------------------------------------------------------
	// Trading Post escrow, against a scratch world.
	//----------------------------------------------------------------------

	hb::server::ledger_sink_swap swap(game, probe_ledger_db);
	if (!swap.ok())
	{
		hb::console::error("atomiccheck: could not create the scratch ledger '{}'.", probe_ledger_db);
		return;
	}
	hb::server::item_ledger_store& ledger = swap.scratch();

	hb::server::remove_probe_db(probe_world_db);
	struct world_cleanup
	{
		~world_cleanup() { hb::server::remove_probe_db(probe_world_db); }
	} world_guard;

	game_database scratch;
	if (!scratch.open(probe_world_db) || !EnsureGameSchema(scratch.handle(), probe_world_db))
	{
		hb::console::error("atomiccheck: could not create the scratch world '{}'.", probe_world_db);
		return;
	}
	sqlite3* world = scratch.handle();

	if (!seed_probe_account(world, seller_account) || !seed_probe_account(world, buyer_account)
		|| !seed_probe_account(world, loser_account))
	{
		hb::console::error("atomiccheck: could not seed the scratch accounts.");
		return;
	}

	// v9's structural claim, asserted rather than assumed: the escrow tables and
	// the character tables are in the SAME database. Everything below is only
	// atomic because of it — a query that can name both in one statement is a
	// transaction that can write both in one commit.
	tally.record("escrow_and_characters_share_one_database",
		hb::server::probe_scalar(world,
			"SELECT COUNT(*) FROM listings LEFT JOIN characters ON 1=0;") == 0);

	trading_post_store board;
	board.set_game(game);
	if (!board.open(world))
	{
		hb::console::error("atomiccheck: could not open the scratch board.");
		return;
	}

	const login_hold no_live_saves;

	const int seller_h = find_free_handle(game, 1);
	const int buyer_h = seller_h > 0 ? find_free_handle(game, seller_h + 1) : -1;
	const int loser_h = buyer_h > 0 ? find_free_handle(game, buyer_h + 1) : -1;
	if (seller_h < 0 || buyer_h < 0 || loser_h < 0)
	{
		hb::console::error("atomiccheck: no free client handles for the escrow probes.");
		return;
	}

	const probe_client seller_probe(game, seller_h, seller_account, seller_char, probe_map, probe_x, probe_y);
	const probe_client buyer_probe(game, buyer_h, buyer_account, buyer_char, probe_map, probe_x, probe_y);
	const probe_client loser_probe(game, loser_h, loser_account, loser_char, probe_map, probe_x, probe_y);
	make_playable(game, seller_h);
	make_playable(game, buyer_h);
	make_playable(game, loser_h);

	uint16_t result = 0;
	const hb::net::TpEscrowSlot one_from_slot_zero[] = { escrow_slot(0, 1) };

	//----------------------------------------------------------------------
	// Escrow-in: the reduced inventory and the Listing are one commit.
	//----------------------------------------------------------------------

	{
		CItem* listed = mint_probe(items, probe_item, 1);
		if (listed == nullptr)
		{
			hb::console::error("atomiccheck: could not mint the escrow-in probe item.");
			return;
		}
		const int64_t serial = place_in_inventory(game, seller_h, 0, listed);

		int64_t listing_id = 0;
		const bool listed_ok = board.create_listing(seller_h, "probe", one_from_slot_zero, 1,
			listing_id, result);

		tally.record("escrow_in_commits_the_listing",
			listed_ok && hb::server::probe_scalar(world,
				"SELECT COUNT(*) FROM listings;") == 1);

		// The other half of the same commit. A world where the Listing exists and
		// the character's saved inventory still holds the item is a dupe waiting
		// for the next restart.
		tally.record("escrow_in_commits_the_reduced_inventory",
			!saved_inventory_holds(world, seller_char, serial));

		// Put it back for the rollback run below.
		board.delist(listing_id, result);
	}

	{
		// Now the same escrow-in with the database refusing the escrow rows. The
		// items have already left the character's memory by then, which is the
		// case that used to be a designed LOSS: the inventory was committed
		// first, so only the escrow half could be undone.
		CItem* listed = mint_probe(items, probe_item, 1);
		if (listed == nullptr)
		{
			hb::console::error("atomiccheck: could not mint the rollback probe item.");
			return;
		}
		const int64_t serial = place_in_inventory(game, seller_h, 0, listed);

		// Made durable FIRST, so the rollback has something to undo. Without this
		// the item would only ever have been in memory, and "the saved inventory
		// still holds it" would be a claim about a row that never existed —
		// passing for a reason unrelated to whether the character save inside the
		// transaction was rewound.
		SaveCharacterSnapshot(world, game->m_client_list[seller_h]);
		const bool was_saved_before = saved_inventory_holds(world, seller_char, serial);

		int64_t listing_id = 0;
		bool listed_ok = true;
		if (arm_insert_failure(world, "listing_items"))
		{
			listed_ok = board.create_listing(seller_h, "probe", one_from_slot_zero, 1,
				listing_id, result);
		}
		disarm_insert_failure(world);

		tally.record("escrow_in_rollback_reports_failure", !listed_ok);

		tally.record("escrow_in_rollback_leaves_no_listing",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 0);

		// The item is back where it was, in memory AND on disk. Either one alone
		// is a broken world: rows without the object means the player cannot see
		// what they own, and the object without the rows means it vanishes at the
		// next restart.
		tally.record("escrow_in_rollback_returns_the_item",
			memory_inventory_holds(game, seller_h, serial));
		tally.record("escrow_in_rollback_keeps_the_saved_inventory",
			was_saved_before && saved_inventory_holds(world, seller_char, serial));

		// And nothing was recorded. The ledger is append-only, so a custody event
		// written for a transaction that rewound would name the board as the
		// holder of an item the player is carrying, for as long as the ledger
		// exists.
		ledger.flush(0);
		tally.record("escrow_in_rollback_records_nothing",
			hb::server::event_scalar(ledger.handle(), "COUNT(*)",
				static_cast<int>(ItemLogAction::TpList), serial) == 0);
	}

	//----------------------------------------------------------------------
	// Escrow-out: the Listing going and the items arriving are one commit.
	//----------------------------------------------------------------------

	{
		// An OFFLINE recipient, which is the path that writes Warehouse rows
		// straight into the account tables.
		CItem* listed = mint_probe(items, probe_item, 1);
		if (listed == nullptr)
		{
			hb::console::error("atomiccheck: could not mint the escrow-out probe item.");
			return;
		}
		const int64_t serial = place_in_inventory(game, seller_h, 0, listed);

		int64_t listing_id = 0;
		board.create_listing(seller_h, "probe", one_from_slot_zero, 1, listing_id, result);
		game->m_client_list[seller_h]->m_is_init_complete = false;

		bool delisted = true;
		if (arm_insert_failure(world, "character_bank_items"))
		{
			delisted = board.delist(listing_id, result);
		}
		disarm_insert_failure(world);

		tally.record("escrow_out_rollback_reports_failure", !delisted);

		// Still listed, and nobody gained anything. The alternative — the row
		// deleted and the delivery failed — is the loss ADR 0001's ordering used
		// to choose deliberately, because it was the better of two bad outcomes.
		tally.record("escrow_out_rollback_keeps_the_listing",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 1);
		tally.record("escrow_out_rollback_leaves_no_warehouse_row",
			!saved_bank_holds(world, seller_char, serial));

		// The same delist with nothing armed: now both halves land.
		delisted = board.delist(listing_id, result);
		tally.record("escrow_out_commits_the_delivery",
			delisted && saved_bank_holds(world, seller_char, serial));
		tally.record("escrow_out_commits_the_removal",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 0);

		game->m_client_list[seller_h]->m_is_init_complete = true;
	}

	{
		// An ONLINE recipient. This is the case a disk-only prover would pass
		// while the world duplicated the item: the rollback rewinds the rows, and
		// only the undo half takes the item back out of the Warehouse in memory.
		CItem* listed = mint_probe(items, probe_item, 1);
		if (listed == nullptr)
		{
			hb::console::error("atomiccheck: could not mint the online-delivery probe item.");
			return;
		}
		const int64_t serial = place_in_inventory(game, seller_h, 0, listed);

		int64_t listing_id = 0;
		board.create_listing(seller_h, "probe", one_from_slot_zero, 1, listing_id, result);

		bool delisted = true;
		if (arm_insert_failure(world, "character_bank_items"))
		{
			delisted = board.delist(listing_id, result);
		}
		disarm_insert_failure(world);

		tally.record("online_rollback_reports_failure", !delisted);
		tally.record("online_rollback_keeps_the_listing",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 1);

		// The one that matters.
		tally.record("online_rollback_empties_the_warehouse",
			!memory_bank_holds(game, seller_h, serial));

		// Clear the board for the Trade below.
		board.delist(listing_id, result);
	}

	//----------------------------------------------------------------------
	// The composed operation: a finalized Trade. Four bundles, three
	// characters, and before v9 five or more commits across two files.
	//----------------------------------------------------------------------

	{
		CItem* listed = mint_probe(items, probe_item, 1);
		CItem* winning = mint_probe(items, probe_item, 1);
		CItem* losing = mint_probe(items, probe_item, 1);
		if (listed == nullptr || winning == nullptr || losing == nullptr)
		{
			hb::console::error("atomiccheck: could not mint the Trade probe items.");
			return;
		}
		const int64_t listed_serial = place_in_inventory(game, seller_h, 0, listed);
		const int64_t winning_serial = place_in_inventory(game, buyer_h, 0, winning);
		const int64_t losing_serial = place_in_inventory(game, loser_h, 0, losing);

		int64_t listing_id = 0;
		int64_t winning_offer = 0;
		int64_t losing_offer = 0;
		board.create_listing(seller_h, "probe", one_from_slot_zero, 1, listing_id, result);
		board.place_offer(buyer_h, static_cast<int32_t>(listing_id), one_from_slot_zero, 1,
			winning_offer, result);
		board.place_offer(loser_h, static_cast<int32_t>(listing_id), one_from_slot_zero, 1,
			losing_offer, result);

		// Fail the Trade at the first delivery it attempts.
		bool finalized = true;
		if (arm_insert_failure(world, "character_bank_items"))
		{
			finalized = board.finalize(listing_id, winning_offer, result);
		}
		disarm_insert_failure(world);

		tally.record("finalize_rollback_reports_failure", !finalized);

		// Nothing at all. Not the Listing, not one Offer, not one Warehouse row —
		// including the losing Offer, which is refunded before either half of the
		// Trade is delivered and would otherwise be the one piece left standing.
		tally.record("finalize_rollback_keeps_the_listing",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 1);
		tally.record("finalize_rollback_keeps_every_offer",
			hb::server::probe_scalar(world, "SELECT COUNT(*) FROM offers;") == 2);
		tally.record("finalize_rollback_delivers_nothing",
			!saved_bank_holds(world, seller_char, winning_serial)
			&& !saved_bank_holds(world, buyer_char, listed_serial)
			&& !memory_bank_holds(game, loser_h, losing_serial));

		// The same Trade with nothing armed. Both directions, because a finalize
		// that never moved anything would pass every check above.
		finalized = board.finalize(listing_id, winning_offer, result);

		tally.record("finalize_commits_the_whole_trade",
			finalized
			&& hb::server::probe_scalar(world, "SELECT COUNT(*) FROM listings;") == 0
			&& hb::server::probe_scalar(world, "SELECT COUNT(*) FROM offers;") == 0);

		tally.record("finalize_commits_both_halves",
			memory_bank_holds(game, seller_h, winning_serial)
			&& memory_bank_holds(game, buyer_h, listed_serial));

		tally.record("finalize_refunds_the_loser",
			memory_bank_holds(game, loser_h, losing_serial));
	}

	//----------------------------------------------------------------------
	// Hygiene: every scope that opened was closed. A transaction left open on
	// the live connection makes the NEXT write anywhere in the server part of
	// a scope nobody owns, and game_database::close rolls it back at shutdown.
	//----------------------------------------------------------------------

	tally.record("no_transaction_left_open_live",
		hb::server::game_db().transaction_depth() == 0);
	tally.record("no_transaction_left_open_scratch",
		scratch.transaction_depth() == 0);

	board.close();
	scratch.close();
	swap.restore();
	hb::server::remove_probe_db(probe_ledger_db);
	hb::server::remove_probe_db(probe_world_db);

	tally.record("scratch_removed",
		!std::filesystem::exists(probe_ledger_db) && !std::filesystem::exists(probe_world_db));

	tally.report(std::format("probe item {}", probe_item));
}
