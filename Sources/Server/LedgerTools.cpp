#include "LedgerTools.h"

#include "Game.h"
#include "ItemLedgerStore.h"
#include "sqlite3.h"

#include <filesystem>

namespace hb::server
{
	read_only_db::read_only_db(const std::string& path)
	{
		if (sqlite3_open_v2(path.c_str(), &m_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
			m_error = m_db != nullptr ? sqlite3_errmsg(m_db) : "out of memory";
			sqlite3_close(m_db);
			m_db = nullptr;
			return;
		}

		// A live WAL database can be busy behind a checkpoint. Waiting is right
		// for a job an operator started and is expecting an answer from.
		sqlite3_busy_timeout(m_db, 5000);
	}

	read_only_db::~read_only_db()
	{
		if (m_db != nullptr) sqlite3_close(m_db);
	}

	bool same_file(const std::string& a, const std::string& b)
	{
		if (a.empty() || b.empty()) return false;

		std::error_code ec;
		const auto left = std::filesystem::weakly_canonical(a, ec);
		if (ec) return a == b;
		const auto right = std::filesystem::weakly_canonical(b, ec);
		if (ec) return a == b;
		return left == right;
	}

	size_t flush_if_live_ledger(CGame* game, const std::string& path)
	{
		if (game == nullptr || game->m_item_ledger_store == nullptr
			|| !game->m_item_ledger_store->is_open()) {
			return 0;
		}
		if (!same_file(path, sqlite3_db_filename(game->m_item_ledger_store->handle(), "main"))) {
			return 0;
		}

		const size_t pending = game->m_item_ledger_store->pending_count();
		game->flush_item_ledger();
		return pending;
	}
}
