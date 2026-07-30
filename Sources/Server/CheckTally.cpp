#include "CheckTally.h"

#include "Game.h"
#include "Item.h"
#include "ItemManager.h"
#include "sqlite3.h"

#include <filesystem>

namespace hb::server
{
	int find_probe_item(CGame* game, bool want_stackable)
	{
		for (int i = 0; i < hb::server::config::MaxItemTypes; i++)
		{
			CItem* config = game->m_item_config_list[i];
			if (config == nullptr) continue;
			if (config->is_stackable() == want_stackable) return i;
		}
		return -1;
	}

	int find_unlogged_probe_item(CGame* game)
	{
		for (int i = 0; i < hb::server::config::MaxItemTypes; i++)
		{
			CItem* config = game->m_item_config_list[i];
			if (config == nullptr || config->is_stackable()) continue;
			if (game->m_item_manager->check_good_item(config) == false) return i;
		}
		return -1;
	}

	void remove_probe_db(const char* path)
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
		std::filesystem::remove(std::string(path) + "-wal", ec);
		std::filesystem::remove(std::string(path) + "-shm", ec);
	}

	int64_t probe_scalar(sqlite3* db, const char* sql)
	{
		if (db == nullptr) return -1;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

		int64_t value = -1;
		if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
			value = sqlite3_column_int64(stmt, 0);
		}
		sqlite3_finalize(stmt);
		return value;
	}

	std::string probe_text(sqlite3* db, const char* sql)
	{
		if (db == nullptr) return {};

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

		std::string value;
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const unsigned char* raw = sqlite3_column_text(stmt, 0);
			if (raw != nullptr) value = reinterpret_cast<const char*>(raw);
		}
		sqlite3_finalize(stmt);
		return value;
	}
}
