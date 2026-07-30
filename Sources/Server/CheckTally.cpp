#include "CheckTally.h"

#include "Game.h"
#include "Item.h"
#include "sqlite3.h"

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
