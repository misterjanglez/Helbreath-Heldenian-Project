#include "GameDatabase.h"

#include "Log.h"
#include "sqlite3.h"

#include <format>

namespace
{
	bool exec_sql(sqlite3* db, const char* sql)
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
			hb::logger::error("[GAMEDB] SQLite exec failed ({}): {}", sql, err ? err : "unknown");
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

	bool game_database::open(const std::string& path)
	{
		if (m_db != nullptr) {
			return true;
		}

		if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
			hb::logger::error("[GAMEDB] sqlite3_open failed for {}: {}", path,
				m_db ? sqlite3_errmsg(m_db) : "unknown");
			if (m_db != nullptr) {
				sqlite3_close(m_db);
				m_db = nullptr;
			}
			return false;
		}

		m_path = path;

		if (!apply_pragmas()) {
			close();
			return false;
		}

		hb::logger::log("[GAMEDB] {} open (WAL, synchronous=NORMAL)", path);
		return true;
	}

	bool game_database::apply_pragmas()
	{
		sqlite3_busy_timeout(m_db, 1000);

		// foreign_keys is per-connection, not stored in the file, so it has to be
		// set every open. The ON DELETE CASCADE chains from `characters` down to
		// every per-character table are what make DeleteCharacterData one
		// statement instead of eight — without this pragma they are decoration.
		if (!exec_sql(m_db, "PRAGMA foreign_keys = ON;")) {
			return false;
		}

		// WAL for readers, not for write throughput: it is what lets the P4
		// reconciliation and analytics tools query the live world without
		// stopping it, and it is the mode ADR 0004 requires for cross-account
		// atomicity to mean anything. synchronous=NORMAL is the matching half —
		// snapshot saves already tolerate losing the window since the last save,
		// so an fsync per commit buys durability the design does not claim.
		if (!exec_sql(m_db, "PRAGMA journal_mode = WAL;")
			|| !exec_sql(m_db, "PRAGMA synchronous = NORMAL;")) {
			return false;
		}

		const std::string mode = read_pragma(m_db, "PRAGMA journal_mode;");
		if (mode != "wal") {
			hb::logger::error("[GAMEDB] journal_mode is '{}', not WAL - cross-account "
				"transactions and live external readers both depend on it",
				mode.empty() ? "unknown" : mode);
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
			hb::logger::warn("[GAMEDB] closing with {} transaction scope(s) still open - "
				"rolling back", m_txn_depth);
			m_txn_depth = 0;
			exec_sql(m_db, "ROLLBACK;");
		}

		// Cached statements hold a reference on the connection; sqlite3_close
		// returns SQLITE_BUSY while any of them live.
		for (auto& entry : m_statements) {
			sqlite3_finalize(entry.second);
		}
		m_statements.clear();

		if (sqlite3_close(m_db) != SQLITE_OK) {
			hb::logger::error("[GAMEDB] sqlite3_close failed: {}", sqlite3_errmsg(m_db));
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
			hb::logger::error("[GAMEDB] prepare failed: {} | SQL: {}", sqlite3_errmsg(m_db), sql);
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
			if (!exec_sql(m_db, "BEGIN;")) {
				return false;
			}
			m_txn_depth = 1;
			return true;
		}

		// Inside an existing transaction: a SAVEPOINT, so that failing here can
		// be undone without taking the enclosing scope down with it.
		const std::string sql = std::format("SAVEPOINT sp_{};", ++m_savepoint_seq);
		if (!exec_sql(m_db, sql.c_str())) {
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
			return exec_sql(m_db, "COMMIT;");
		}

		// RELEASE folds the savepoint's work into the enclosing scope; nothing
		// is durable until the outermost COMMIT.
		const std::string sql = std::format("RELEASE sp_{};", m_savepoint_seq--);
		--m_txn_depth;
		return exec_sql(m_db, sql.c_str());
	}

	bool game_database::rollback()
	{
		if (m_db == nullptr || m_txn_depth == 0) {
			return false;
		}

		if (m_txn_depth == 1) {
			m_txn_depth = 0;
			return exec_sql(m_db, "ROLLBACK;");
		}

		// ROLLBACK TO rewinds the savepoint but leaves it on the stack, so the
		// RELEASE is what actually pops it. Both, or the next commit at this
		// depth names a savepoint that is still open.
		const int id = m_savepoint_seq--;
		--m_txn_depth;
		const std::string undo = std::format("ROLLBACK TO sp_{};", id);
		const std::string pop = std::format("RELEASE sp_{};", id);
		const bool ok = exec_sql(m_db, undo.c_str());
		return exec_sql(m_db, pop.c_str()) && ok;
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

		if (db == game_db().handle()) {
			m_stmt = game_db().cached(sql);
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

		if (db == game_db().handle()) {
			m_nested = true;
			m_active = game_db().begin();
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
		return m_nested ? game_db().commit() : exec_sql(m_db, "COMMIT;");
	}

	void txn_scope::rollback()
	{
		if (!m_active) {
			return;
		}
		m_active = false;
		if (m_nested) {
			game_db().rollback();
		}
		else {
			exec_sql(m_db, "ROLLBACK;");
		}
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
