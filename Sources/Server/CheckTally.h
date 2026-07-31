// CheckTally.h: the shared scaffold for the headless contract provers
//
// `serialcheck`, `ledgercheck` and the provers the rest of epic #74 will add all
// answer the same shape of question: run a list of named checks against a
// contract, print one machine-readable line each, and finish with a total. The
// output format is not cosmetic — the Linux gate compares it against the Windows
// run byte for byte — so it lives in one place. A per-prover copy would let a
// format tweak land in one and silently desynchronise the other's baseline.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ServerConsole.h"

struct sqlite3;
class CGame;
class CItem;

namespace hb::server
{
	class item_ledger_store;

	// Runs a prover's checks and reports them. `prefix` is the machine-line tag
	// ("SERIALCHECK", "LEDGERCHECK"); it is also what the closing summary names.
	class check_tally
	{
	public:
		explicit check_tally(const char* prefix, const char* command)
			: m_prefix(prefix), m_command(command) {}

		void record(const char* name, bool ok, const std::string& detail = {})
		{
			++m_total;
			if (ok) ++m_passed;
			if (detail.empty())
				hb::console::write("{} {} {}", m_prefix, name, ok ? "PASS" : "FAIL");
			else
				hb::console::write("{} {} {} {}", m_prefix, name, ok ? "PASS" : "FAIL", detail);
		}

		// The closing lines: the machine-readable total, then a human summary.
		// `context` is an optional parenthetical for the success line (the probe
		// ids a run happened to pick, say) — deliberately kept off the machine
		// line, which must stay comparable across platforms.
		void report(const std::string& context = {}) const
		{
			hb::console::write("{} RESULT {} {}", m_prefix, m_passed, m_total);

			if (m_passed == m_total)
			{
				if (context.empty())
					hb::console::success("{}: {}/{} checks passed.", m_command, m_passed, m_total);
				else
					hb::console::success("{}: {}/{} checks passed ({}).",
						m_command, m_passed, m_total, context);
			}
			else
			{
				hb::console::error("{}: {}/{} checks passed - the contract is broken.",
					m_command, m_passed, m_total);
			}
		}

		int passed() const { return m_passed; }
		int total() const { return m_total; }

	private:
		const char* m_prefix;
		const char* m_command;
		int m_passed = 0;
		int m_total = 0;
	};

	// The first loaded item-config row with the wanted stackability, or -1.
	// Deriving probe items from data beats hard-coded ids that a content edit
	// could quietly turn into the wrong kind of item — and both provers must pick
	// the same way, or their outputs stop being evidence about the same thing.
	int find_probe_item(CGame* game, bool want_stackable);

	// The first non-stackable item the text log's noise filter does NOT cover, or
	// -1. What `itemlogcheck` needs to tell the two sinks apart: an item the
	// channels are meant to skip and the ledger is meant to keep. Beside
	// find_probe_item for the same reason it exists — probe selection is how one
	// prover's evidence stays comparable with another's.
	int find_unlogged_probe_item(CGame* game);

	// Delete a scratch SQLite database and the two files WAL leaves beside it.
	// Every prover that writes to a scratch file needs this on every exit path,
	// and three copies of it is three places for one of them to forget the -shm.
	void remove_probe_db(const char* path);

	// First column of the first row. `probe_scalar` returns -1 when the query
	// fails or yields nothing (no expected value in any prover is -1);
	// `probe_text` returns empty.
	//
	// Here rather than in each prover because a check is only evidence if it
	// reads the database the same way the other provers do — a per-prover copy
	// that treated a NULL or a failed prepare differently would make two
	// commands disagree about the same file for a reason nobody would look for.
	int64_t probe_scalar(sqlite3* db, const char* sql);
	std::string probe_text(sqlite3* db, const char* sql);

	// One projection off one item_events row, keyed the way every Biography
	// question is: by Serial and event type. `expr` may be a column name, a
	// predicate ("map IS NULL") or an aggregate ("COUNT(*)") — the reader is the
	// same either way, and `-1` / empty still mean "no row" per probe_scalar.
	//
	// Here rather than in each prover because this SELECT names a table and two
	// columns that a later schema change will move, and three hand-typed copies
	// is three places to miss one. The provers keep their own thin wrappers over
	// these for the defaults each of them wants.
	int64_t event_scalar(sqlite3* db, const char* expr, int event_type, int64_t serial);
	std::string event_text(sqlite3* db, const char* column, int event_type, int64_t serial);

	// Redirects the game's ledger sink to a scratch database for the length of a
	// prover run and puts the live store back however the command returns —
	// including its early bail-outs. Leaving the scratch store installed would
	// point every later item transition in the process at a file this deletes on
	// the way out.
	//
	// The sink moves rather than the rows being deleted afterwards because an
	// append-only audit log cannot have test rows taken back out of it. The
	// scratch file (and the two WAL leaves beside it) are removed on the way in as
	// well as the way out, so a run that died half way through cannot leave its
	// rows for the next one to read as its own.
	//
	// Third prover to need this, which is why it is here rather than copied again.
	class ledger_sink_swap
	{
	public:
		ledger_sink_swap(CGame* game, const char* probe_path);
		~ledger_sink_swap();

		ledger_sink_swap(const ledger_sink_swap&) = delete;
		ledger_sink_swap& operator=(const ledger_sink_swap&) = delete;

		// False when the scratch store could not be created. The live sink is then
		// untouched and scratch() must not be called; the caller reports it.
		// Also false after restore() — the swap is over and there is no scratch.
		bool ok() const { return m_game != nullptr; }

		// The installed scratch store. Only valid while ok().
		item_ledger_store& scratch() const;

		// Put the live sink back now instead of at destruction, so that a prover
		// can assert the restoration happened. That assertion is worth making:
		// everything a prover writes goes into a file it is about to delete, and a
		// run that ended without moving the sink back would leave the world
		// logging into nothing — silently, because the ledger promises never to
		// fail a game action. Idempotent, and the destructor still removes the
		// scratch file afterwards.
		void restore();

	private:
		// m_game doubles as the "a swap is in effect" flag: it is set only
		// between a successful open and restore(), which is exactly when there is
		// a live store to put back and a scratch to read.
		CGame*      m_game = nullptr;
		std::string m_path;
		std::unique_ptr<item_ledger_store> m_live;
	};

	// A synthetic client parked in a free client handle, so a prover can test the
	// half of the actor columns that matters rather than only their NULL path.
	// The game loop cannot see it — a console command runs inside the tick, not
	// beside it — and it is removed when this goes out of scope.
	//
	// Its socket is never connected, which is safe rather than lucky:
	// ASIOSocket::send_msg refuses a socket whose type was never set, so every
	// send_notify_msg aimed at one of these is a no-op.
	//
	// It counts as logged in, so find_client_by_name resolves it. Escrow rows,
	// notices and ledger actors all carry a character name and nothing else, so a
	// probe the by-name paths cannot see would only ever test their absent case.
	class probe_client
	{
	public:
		probe_client(CGame* game, int handle, const char* account, const char* name,
			const char* map, int x, int y);
		~probe_client();

		probe_client(const probe_client&) = delete;
		probe_client& operator=(const probe_client&) = delete;

		int handle() const { return m_handle; }

	private:
		CGame* m_game;
		int    m_handle;
	};

	// The lowest free client handle at or above `from`, or -1 when the server is
	// full. Provers that need several take them one after another.
	int find_free_handle(CGame* game, int from = 1);
}
