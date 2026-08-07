#include "GameDatabase.h"

#include "Log.h"
#include "sqlite3.h"

#include <format>
#include <unordered_map>

namespace
{
	// Every open game_database, keyed by its connection.
	//
	// stmt_scope and txn_scope take a raw `sqlite3*` — that is what let ~35 call
	// sites opt into the statement cache and the nested transactions without a
	// signature change — so they need a way to get from a handle back to the
	// object that owns it, because the cache and the SAVEPOINT depth both live
	// on that object.
	//
	// This used to be a single comparison against the live singleton, and
	// anything else fell back to a plain BEGIN. That made a prover's scratch
	// database behave differently from the real one in exactly the way a prover
	// exists to rule out: a nested scope on the live world became a SAVEPOINT
	// and on the scratch world became a second BEGIN, which SQLite rejects. It
	// went unnoticed while nothing nested on a scratch file; #82 puts a
	// transaction around a character save, so the scratch world now has to
	// exercise the same code the live world runs.
	//
	// Single-threaded by the same rule as the statement cache: every caller runs
	// on the game thread.
	// Deliberately leaked rather than a static object. game_db() is a
	// function-local static too, and its destructor runs at process exit — after
	// this map's would, since this one is constructed later (the first open()
	// happens after game_db() has been reached). close() then erases from a map
	// that no longer exists, which is undefined behaviour and in practice a
	// segfault on every shutdown. Never destroying a handful of pointers at exit
	// costs nothing; reaching a destroyed static costs the process.
	std::unordered_map<sqlite3*, hb::server::game_database*>& database_registry()
	{
		static auto* instances = new std::unordered_map<sqlite3*, hb::server::game_database*>();
		return *instances;
	}

	hb::server::game_database* owner_of(sqlite3* db)
	{
		if (db == nullptr) return nullptr;
		auto& instances = database_registry();
		auto found = instances.find(db);
		return (found != instances.end()) ? found->second : nullptr;
	}

	bool exec_sql(sqlite3* db, const char* sql, const char* label = "GAMEDB")
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
			hb::logger::error("[{}] SQLite exec failed ({}): {}", label, sql, err ? err : "unknown");
			sqlite3_free(err);
			return false;
		}
		return true;
	}

	// The value a PRAGMA reports back, as text. Setting journal_mode returns a
	// row that sqlite3_exec discards, and a file that quietly stayed in
	// rollback-journal mode looks fine right up until a reader blocks the server
	// — so the mode is read back rather than assumed.
	std::string read_pragma(sqlite3* db, const char* pragma)
	{
		sqlite3_stmt* stmt = nullptr;
		std::string value;
		if (sqlite3_prepare_v2(db, pragma, -1, &stmt, nullptr) == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				const unsigned char* text = sqlite3_column_text(stmt, 0);
				if (text != nullptr) value = reinterpret_cast<const char*>(text);
			}
			sqlite3_finalize(stmt);
		}
		return value;
	}
}

namespace hb::server
{
	game_database::~game_database()
	{
		close();
	}

	bool game_database::open(const std::string& path, const char* label)
	{
		if (m_db != nullptr) {
			return true;
		}
		m_label = label;

		if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
			hb::logger::error("[{}] sqlite3_open failed for {}: {}", m_label, path,
				m_db ? sqlite3_errmsg(m_db) : "unknown");
			if (m_db != nullptr) {
				sqlite3_close(m_db);
				m_db = nullptr;
			}
			return false;
		}

		m_path = path;
		database_registry()[m_db] = this;

		if (!apply_pragmas()) {
			close();
			return false;
		}

		hb::logger::log("[{}] {} open (WAL, synchronous=NORMAL)", m_label, path);
		return true;
	}

	bool game_database::apply_pragmas()
	{
		sqlite3_busy_timeout(m_db, 1000);

		// foreign_keys is per-connection, not stored in the file, so it has to be
		// set every open. The ON DELETE CASCADE chains from `characters` down to
		// every per-character table are what make DeleteCharacterData one
		// statement instead of eight — without this pragma they are decoration.
		if (!exec_sql(m_db, "PRAGMA foreign_keys = ON;", m_label.c_str())) {
			return false;
		}

		// WAL for readers, not for write throughput: it is what lets the P4
		// reconciliation and analytics tools query the live world without
		// stopping it, and it is the mode ADR 0004 requires for cross-account
		// atomicity to mean anything. synchronous=NORMAL is the matching half —
		// snapshot saves already tolerate losing the window since the last save,
		// so an fsync per commit buys durability the design does not claim.
		if (!exec_sql(m_db, "PRAGMA journal_mode = WAL;", m_label.c_str())
			|| !exec_sql(m_db, "PRAGMA synchronous = NORMAL;", m_label.c_str())) {
			return false;
		}

		const std::string mode = read_pragma(m_db, "PRAGMA journal_mode;");
		if (mode != "wal") {
			hb::logger::error("[{}] journal_mode is '{}', not WAL - cross-account "
				"transactions and live external readers both depend on it",
				m_label, mode.empty() ? "unknown" : mode);
			return false;
		}

		return true;
	}

	void game_database::close()
	{
		if (m_db == nullptr) {
			return;
		}

		// An open transaction at close means an error path skipped its scope.
		// Rolling back is the honest outcome: committing a sweep that never
		// reached its own commit would persist whatever half of it had run.
		if (m_txn_depth > 0) {
			hb::logger::warn("[{}] closing with {} transaction scope(s) still open - "
				"rolling back", m_label, m_txn_depth);
			m_txn_depth = 0;
			exec_sql(m_db, "ROLLBACK;", m_label.c_str());
		}

		// Cached statements hold a reference on the connection; sqlite3_close
		// returns SQLITE_BUSY while any of them live.
		for (auto& entry : m_statements) {
			sqlite3_finalize(entry.second);
		}
		m_statements.clear();

		// Deregistered before the close, because SQLite is free to hand the same
		// address out again for the next connection — and a stale entry would
		// route a fresh database's scopes at a destroyed object.
		database_registry().erase(m_db);

		if (sqlite3_close(m_db) != SQLITE_OK) {
			hb::logger::error("[{}] sqlite3_close failed: {}", m_label, sqlite3_errmsg(m_db));
		}
		m_db = nullptr;
	}

	sqlite3_stmt* game_database::cached(const char* sql)
	{
		if (m_db == nullptr || sql == nullptr) {
			return nullptr;
		}

		auto found = m_statements.find(sql);
		if (found != m_statements.end()) {
			// Handed back ready to bind. Clearing as well as resetting means a
			// caller that binds fewer parameters than the last one cannot
			// silently inherit the previous call's value in the gap.
			sqlite3_reset(found->second);
			sqlite3_clear_bindings(found->second);
			return found->second;
		}

		sqlite3_stmt* stmt = nullptr;
		// PREPARE_PERSISTENT: these statements are cached for the life of the
		// process, which is exactly the case the flag exists to tell SQLite about.
		if (sqlite3_prepare_v3(m_db, sql, -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr) != SQLITE_OK) {
			hb::logger::error("[{}] prepare failed: {} | SQL: {}", m_label, sqlite3_errmsg(m_db), sql);
			return nullptr;
		}

		m_statements.emplace(sql, stmt);
		return stmt;
	}

	bool game_database::begin()
	{
		if (m_db == nullptr) {
			return false;
		}

		if (m_txn_depth == 0) {
			if (!exec_sql(m_db, "BEGIN;", m_label.c_str())) {
				return false;
			}
			m_txn_depth = 1;
			return true;
		}

		// Inside an existing transaction: a SAVEPOINT, so that failing here can
		// be undone without taking the enclosing scope down with it.
		const std::string sql = std::format("SAVEPOINT sp_{};", ++m_savepoint_seq);
		if (!exec_sql(m_db, sql.c_str(), m_label.c_str())) {
			--m_savepoint_seq;
			return false;
		}
		++m_txn_depth;
		return true;
	}

	bool game_database::commit()
	{
		if (m_db == nullptr || m_txn_depth == 0) {
			return false;
		}

		if (m_txn_depth == 1) {
			m_txn_depth = 0;
			return exec_sql(m_db, "COMMIT;", m_label.c_str());
		}

		// RELEASE folds the savepoint's work into the enclosing scope; nothing
		// is durable until the outermost COMMIT.
		const std::string sql = std::format("RELEASE sp_{};", m_savepoint_seq--);
		--m_txn_depth;
		return exec_sql(m_db, sql.c_str(), m_label.c_str());
	}

	bool game_database::rollback()
	{
		if (m_db == nullptr || m_txn_depth == 0) {
			return false;
		}

		if (m_txn_depth == 1) {
			m_txn_depth = 0;
			return exec_sql(m_db, "ROLLBACK;", m_label.c_str());
		}

		// ROLLBACK TO rewinds the savepoint but leaves it on the stack, so the
		// RELEASE is what actually pops it. Both, or the next commit at this
		// depth names a savepoint that is still open.
		const int id = m_savepoint_seq--;
		--m_txn_depth;
		const std::string undo = std::format("ROLLBACK TO sp_{};", id);
		const std::string pop = std::format("RELEASE sp_{};", id);
		const bool ok = exec_sql(m_db, undo.c_str(), m_label.c_str());
		return exec_sql(m_db, pop.c_str(), m_label.c_str()) && ok;
	}

	game_database& game_db()
	{
		static game_database instance;
		return instance;
	}

	sqlite3* game_db_handle()
	{
		return game_db().handle();
	}

	stmt_scope::stmt_scope(sqlite3* db, const char* sql)
	{
		if (db == nullptr || sql == nullptr) {
			return;
		}

		if (game_database* owner = owner_of(db)) {
			m_stmt = owner->cached(sql);
			return;
		}

		if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
			hb::logger::error("[GAMEDB] prepare failed: {} | SQL: {}", sqlite3_errmsg(db), sql);
			m_stmt = nullptr;
			return;
		}
		m_owned = true;
	}

	txn_scope::txn_scope(sqlite3* db)
		: m_db(db)
	{
		if (db == nullptr) {
			return;
		}

		if (game_database* owner = owner_of(db)) {
			m_owner = owner;
			m_active = owner->begin();
			return;
		}

		m_active = exec_sql(db, "BEGIN;");
	}

	txn_scope::~txn_scope()
	{
		rollback();
	}

	bool txn_scope::commit()
	{
		if (!m_active) {
			return false;
		}
		m_active = false;
		return (m_owner != nullptr) ? m_owner->commit() : exec_sql(m_db, "COMMIT;");
	}

	void txn_scope::rollback()
	{
		if (!m_active) {
			return;
		}
		m_active = false;
		if (m_owner != nullptr) {
			m_owner->rollback();
		}
		else {
			exec_sql(m_db, "ROLLBACK;");
		}
	}

	sql_query::sql_query(sqlite3* db, const std::string& sql, const char* tag)
		: m_stmt(db, sql.c_str())
	{
		if (!m_stmt) {
			hb::logger::error("[{}] query failed: {}", tag,
				db != nullptr ? sqlite3_errmsg(db) : "no connection");
		}
	}

	bool sql_query::step()
	{
		return m_stmt && sqlite3_step(m_stmt.get()) == SQLITE_ROW;
	}

	int64_t sql_query::as_int(int column) const
	{
		return sqlite3_column_int64(m_stmt.get(), column);
	}

	std::string sql_query::as_text(int column) const
	{
		const unsigned char* text = sqlite3_column_text(m_stmt.get(), column);
		return text != nullptr ? reinterpret_cast<const char*>(text) : std::string();
	}

	int64_t sql_scalar(sqlite3* db, const char* sql, int64_t fallback, const char* tag)
	{
		sql_query query(db, sql, tag);
		return query.step() ? query.as_int(0) : fallback;
	}

	bool sql_table_exists(sqlite3* db, const char* name, const char* tag)
	{
		sql_query query(db, std::format(
			"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='{}';", name), tag);
		return query.step() && query.as_int(0) > 0;
	}

	stmt_scope::~stmt_scope()
	{
		if (m_stmt == nullptr) {
			return;
		}
		if (m_owned) {
			sqlite3_finalize(m_stmt);
		}
		else {
			// Cached: reset releases whatever read transaction the statement is
			// still holding. cached() resets on handout too, but doing it here as
			// well is what keeps a partially-stepped SELECT from blocking a
			// COMMIT that happens before the next borrow.
			sqlite3_reset(m_stmt);
		}
	}
}
