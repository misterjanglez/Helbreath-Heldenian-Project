// GameDatabase.h: the one connection to game.db (ADR 0004, plan P1.2)
//
// The account store used to be one SQLite file per account. This class is what
// replaces the per-operation open/close of those files with a single connection
// held for the life of the process, and it exists for three reasons that the
// old layout could not deliver:
//
//   1. **Atomicity across accounts.** SQLite transactions spanning ATTACHed
//      files are not atomic in WAL mode, so an Exchange between two players was
//      permanently two commits with a dupe window between them. One file makes
//      one transaction possible; `transaction` below is that primitive, and #82
//      is what wraps the trade path in it.
//
//   2. **Nesting that does not lie.** `SaveCharacterSnapshot` runs its own
//      transaction, and the mass-save sweep wants to wrap the whole sweep in
//      one. A depth counter over plain BEGIN/COMMIT would make an inner failure
//      roll back everybody, so inner scopes are SAVEPOINTs: one character whose
//      save fails is undone alone and the sweep carries on. That is the
//      behaviour the per-file layout had by accident, kept here on purpose.
//
//   3. **A statement cache.** With one connection the prepared statements
//      outlive the call, so the save path stops re-compiling the same dozen
//      INSERTs for every character on every sweep.
//
// The schema itself is NOT here — it lives with the queries that use it, in
// AccountSqliteStore.cpp, whose `EnsureGameDatabase()` opens this connection and
// then applies the v8 DDL. This class owns the connection and its hygiene;
// nothing about which tables exist.
//
// Design contract: PLANS/ItemLedger_Plan.md P1.2, docs/adr/0004-single-game-db.md.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

namespace hb::server
{
	// The Game DB connection. One per server, opened at boot and held until
	// shutdown; also instantiable standalone so `gamedbcheck` can drive a scratch
	// file without touching the live world.
	class game_database
	{
	public:
		game_database() = default;
		~game_database();

		game_database(const game_database&) = delete;
		game_database& operator=(const game_database&) = delete;

		// Open (creating if absent) the file and put it in the hygiene posture:
		// WAL, synchronous=NORMAL, foreign keys on, a busy timeout. Does not
		// touch the schema — see EnsureGameDatabase() in AccountSqliteStore.h,
		// which is what callers other than the prover should use.
		//
		// `label` names this connection in every log line the class emits, so a
		// second instance on another file (the guild store's guilds.db, #120)
		// reports as itself rather than as the Game DB.
		bool open(const std::string& path, const char* label = "GAMEDB");

		// Finalize every cached statement and close. Idempotent. An open
		// transaction is rolled back rather than silently committed: reaching
		// close with one still open means an error path skipped its scope, and
		// committing a half-written sweep is the worse of the two outcomes.
		void close();

		bool is_open() const { return m_db != nullptr; }
		sqlite3* handle() const { return m_db; }
		const std::string& path() const { return m_path; }

		// --- Statement cache -------------------------------------------------
		// The prepared form of `sql`, compiled on first request and reused
		// afterwards, already reset with its bindings cleared. Null if the SQL
		// does not compile (logged).
		//
		// The returned statement belongs to the cache — do NOT finalize it. It is
		// also NOT reentrant: a caller stepping through one of these must finish
		// before anything else asks for the same SQL, or the second caller's
		// reset will cut the first one's loop short. Every caller here runs its
		// loop to completion on one thread, which is what makes that safe.
		sqlite3_stmt* cached(const char* sql);

		// How many statements the cache is holding. Only the prover cares; it is
		// how "the same SQL twice compiles once" is observable at all.
		size_t cached_statement_count() const { return m_statements.size(); }

		// --- Transactions ----------------------------------------------------
		// Prefer the `transaction` RAII below; these are what it drives.
		//
		// The outermost scope is a real BEGIN/COMMIT; every scope inside it is a
		// SAVEPOINT, so a nested rollback undoes only its own work and leaves the
		// enclosing transaction alive and committable.
		bool begin();
		bool commit();
		bool rollback();

		// 0 when no transaction is open, 1 for the outermost, deeper inside.
		int transaction_depth() const { return m_txn_depth; }

		// A transaction scope. Commits on destruction unless rollback() was
		// called or commit() already ran — so an early return or a thrown
		// exception undoes the scope instead of leaking an open transaction.
		class transaction
		{
		public:
			explicit transaction(game_database& db)
				: m_db(db), m_active(db.begin())
			{
			}

			~transaction()
			{
				if (m_active) m_db.rollback();
			}

			transaction(const transaction&) = delete;
			transaction& operator=(const transaction&) = delete;

			// True when the BEGIN/SAVEPOINT actually took. A false here means the
			// connection is unusable, not that the caller should carry on.
			bool active() const { return m_active; }

			bool commit()
			{
				if (!m_active) return false;
				m_active = false;
				return m_db.commit();
			}

			void rollback()
			{
				if (!m_active) return;
				m_active = false;
				m_db.rollback();
			}

		private:
			game_database& m_db;
			bool m_active;
		};

	private:
		bool apply_pragmas();

		sqlite3* m_db = nullptr;
		std::string m_path;
		std::string m_label = "GAMEDB";

		// Keyed by the SQL text rather than the pointer: some call sites build
		// their statement with std::format, and a pointer key would compile a
		// fresh copy of the same query on every call while looking cached.
		std::unordered_map<std::string, sqlite3_stmt*> m_statements;

		int m_txn_depth = 0;

		// Names the savepoints. Monotonic rather than reusing the depth, so a
		// stale RELEASE can never name a live scope.
		int m_savepoint_seq = 0;
	};

	// The live Game DB. Opened by EnsureGameDatabase() at boot, closed at
	// shutdown. A free accessor rather than a CGame member because the account
	// store is a set of free functions that must not have to know about CGame,
	// and the prover needs the class without the singleton.
	game_database& game_db();

	// The live handle, or null before boot has opened it. Sugar for the many
	// call sites that only want to pass `sqlite3*` to an existing store function.
	sqlite3* game_db_handle();

	// A borrowed statement, for the store functions that still speak `sqlite3*`.
	//
	// On any connection a game_database owns — the live one, or a prover's
	// scratch file — this comes from that connection's cache, so the save sweep
	// stops re-compiling the same fifteen statements for every character. On a
	// handle nothing owns (another store's database) it is prepared and
	// finalized normally. The cache belongs to one connection by definition, so
	// "which object owns this handle?" is the whole rule — which is what lets
	// every call site opt in without a signature change.
	//
	// It resets on destruction either way, and that is the part worth having even
	// where nothing is cached: a SELECT abandoned after its first row keeps a read
	// transaction open, and under WAL an unreset statement of that kind is enough
	// to make a later COMMIT fail. Scope exit is a place the old manual
	// sqlite3_finalize could be — and on several error paths was — skipped.
	class stmt_scope
	{
	public:
		stmt_scope(sqlite3* db, const char* sql);
		~stmt_scope();

		stmt_scope(const stmt_scope&) = delete;
		stmt_scope& operator=(const stmt_scope&) = delete;

		sqlite3_stmt* get() const { return m_stmt; }
		operator sqlite3_stmt* () const { return m_stmt; }
		explicit operator bool() const { return m_stmt != nullptr; }

	private:
		sqlite3_stmt* m_stmt = nullptr;
		bool m_owned = false;   // prepared here, so this scope finalizes it
	};

	// A stepped read with typed columns, over stmt_scope — which already answers
	// the awkward half: on a handle a game_database owns this borrows from that
	// connection's statement cache, and on a handle nothing owns (a read-only
	// snapshot, which the audit jobs exist to read) it prepares and finalizes
	// normally. Only the column accessors are new.
	//
	// Shared rather than copied into each audit job because those jobs have to
	// read a database the same way. A per-file copy that treated a NULL column or
	// a failed prepare differently would make two commands disagree about one
	// file, for a reason nobody would think to look for.
	//
	// `tag` prefixes the error log when a statement will not compile, so a broken
	// query names the job that owns it rather than only the SQL.
	class sql_query
	{
	public:
		sql_query(sqlite3* db, const std::string& sql, const char* tag);

		// False when the statement would not compile. Stepping one is safe and
		// simply ends immediately, so a caller may check this or not.
		explicit operator bool() const { return m_stmt.get() != nullptr; }

		bool step();

		int64_t     as_int(int column) const;
		std::string as_text(int column) const;   // a NULL column reads as empty

	private:
		stmt_scope m_stmt;
	};

	// First column of the first row; `fallback` when the query fails or returns
	// nothing.
	int64_t sql_scalar(sqlite3* db, const char* sql, int64_t fallback, const char* tag);

	// Whether a table is present. This is what tells an audit job it was handed
	// the wrong file — the mistake an operator makes at a command line where two
	// database paths sit next to each other.
	bool sql_table_exists(sqlite3* db, const char* name, const char* tag);

	// A transaction, for the store functions that still speak `sqlite3*`. Same
	// rule as stmt_scope: on a connection a game_database owns it is that
	// object's nested transaction — so an enclosing scope turns this into a
	// SAVEPOINT rather than a second BEGIN, which is what lets save_all_players
	// wrap every character's own transaction in one commit, and what lets a
	// Trading Post custody move wrap a character save (#82). On a handle nothing
	// owns it is a plain BEGIN/COMMIT/ROLLBACK.
	//
	// Rolls back on destruction unless commit() ran, so every early return in a
	// long save path undoes its own work without needing to remember to.
	class txn_scope
	{
	public:
		explicit txn_scope(sqlite3* db);
		~txn_scope();

		txn_scope(const txn_scope&) = delete;
		txn_scope& operator=(const txn_scope&) = delete;

		bool active() const { return m_active; }
		bool commit();
		void rollback();

	private:
		sqlite3* m_db = nullptr;
		bool m_active = false;
		// The game_database that owns m_db, when one does: nesting and the
		// savepoint names live on it. Null means a raw BEGIN/COMMIT.
		game_database* m_owner = nullptr;
	};
}
