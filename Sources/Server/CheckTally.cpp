#include "CheckTally.h"

#include "Client.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "sqlite3.h"

#include <cstdio>
#include <format>
#include <filesystem>

namespace hb::server
{
	int find_probe_item(CGame* game, bool want_stackable, int from)
	{
		for (int i = from; i < hb::server::config::MaxItemTypes; i++)
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

	namespace
	{
		std::string event_sql(const char* expr, int event_type, int64_t serial)
		{
			return std::format("SELECT {} FROM item_events WHERE serial={} AND event_type={};",
				expr, serial, event_type);
		}
	}

	int64_t event_scalar(sqlite3* db, const char* expr, int event_type, int64_t serial)
	{
		return probe_scalar(db, event_sql(expr, event_type, serial).c_str());
	}

	int64_t flow_scalar(sqlite3* db, const char* expr, int32_t day, int item_id, int flow_type)
	{
		return probe_scalar(db, std::format(
			"SELECT {} FROM item_flows WHERE day={} AND item_id={} AND flow_type={};",
			expr, day, item_id, flow_type).c_str());
	}

	std::string event_text(sqlite3* db, const char* column, int event_type, int64_t serial)
	{
		return probe_text(db, event_sql(column, event_type, serial).c_str());
	}

	ledger_sink_swap::ledger_sink_swap(CGame* game, const char* probe_path)
		: m_path(probe_path != nullptr ? probe_path : "")
	{
		if (game == nullptr || m_path.empty()) return;

		remove_probe_db(m_path.c_str());

		auto scratch = std::make_unique<item_ledger_store>();
		if (!scratch->open(m_path)) {
			// The live sink stays installed and the caller reports the failure.
			// m_game is left null, so ok() is false and the destructor has
			// nothing to put back.
			return;
		}

		m_live = std::move(game->m_item_ledger_store);
		game->m_item_ledger_store = std::move(scratch);
		m_game = game;
	}

	ledger_sink_swap::~ledger_sink_swap()
	{
		restore();
		if (!m_path.empty()) remove_probe_db(m_path.c_str());
	}

	void ledger_sink_swap::restore()
	{
		if (m_game == nullptr) return;
		m_game->m_item_ledger_store = std::move(m_live);
		m_game = nullptr;
	}

	item_ledger_store& ledger_sink_swap::scratch() const
	{
		return *m_game->m_item_ledger_store;
	}

	probe_client::probe_client(CGame* game, int handle, const char* account, const char* name,
		const char* map, int x, int y)
		: m_game(game), m_handle(handle)
	{
		CClient* client = new CClient(G_pIOPool->get_context());
		std::snprintf(client->m_account_name, sizeof(client->m_account_name), "%s", account);
		std::snprintf(client->m_char_name, sizeof(client->m_char_name), "%s", name);
		std::snprintf(client->m_map_name, sizeof(client->m_map_name), "%s", map);
		client->m_x = static_cast<short>(x);
		client->m_y = static_cast<short>(y);

		// find_client_by_name skips a client that has not finished logging in, so
		// without this a synthetic client is invisible to every by-name path in
		// the server — which is most of the ones worth testing, since a name is
		// all an escrow row or a notice ever carries.
		client->m_is_init_complete = true;

		m_game->m_client_list[m_handle] = client;
	}

	probe_client::~probe_client()
	{
		delete m_game->m_client_list[m_handle];
		m_game->m_client_list[m_handle] = nullptr;
	}

	int find_free_handle(CGame* game, int from)
	{
		for (int i = from; i < hb::server::config::MaxClients; i++)
			if (game->m_client_list[i] == nullptr) return i;
		return -1;
	}

	bool seed_probe_account(sqlite3* db, const char* account_name)
	{
		if (db == nullptr || account_name == nullptr) return false;

		const std::string sql = std::string(
			"INSERT OR IGNORE INTO accounts(account_name, password_hash, password_salt,"
			" email, created_at, password_changed_at, last_ip)"
			" VALUES('") + account_name + "','h','s','e','t','t','ip');";
		return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
	}
}
