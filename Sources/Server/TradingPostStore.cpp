#include "TradingPostStore.h"

#include "Game.h"
#include "Client.h"
#include "Item.h"
#include "ItemManager.h"
#include "LoginServer.h"
#include "AccountSqliteStore.h"
#include "Packet/PacketTradingPost.h"
#include "sqlite3.h"
#include "Log.h"
#include "ServerLogChannels.h"

#include <chrono>
#include <cstring>
#include <cstdio>

using namespace hb::shared::net;   // Notify::*
using namespace hb::server::net;   // ItemLogAction::*
using hb::log_channel;

namespace
{
	bool exec_sql(sqlite3* db, const char* sql)
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
			hb::logger::error("[TP] SQLite exec failed: {}", err ? err : "unknown");
			sqlite3_free(err);
			return false;
		}
		return true;
	}

	bool bind_text(sqlite3_stmt* stmt, int idx, const char* value)
	{
		return sqlite3_bind_text(stmt, idx, value ? value : "", -1, SQLITE_TRANSIENT) == SQLITE_OK;
	}

	int64_t now_unix()
	{
		return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	}

	const char* tp_action_name(int action)
	{
		switch (action) {
		case ItemLogAction::TpList:     return "List";
		case ItemLogAction::TpDelist:   return "Delist";
		case ItemLogAction::TpOffer:    return "Offer";
		case ItemLogAction::TpRescind:  return "Rescind";
		case ItemLogAction::TpRefund:   return "Refund";
		case ItemLogAction::TpTradeOut: return "TradeOut";
		case ItemLogAction::TpTradeIn:  return "TradeIn";
		default:                        return "Tp?";
		}
	}
}

namespace hb::server
{
	trading_post_store::~trading_post_store()
	{
		close();
	}

	bool trading_post_store::open(const std::string& path)
	{
		if (m_db != nullptr) {
			return true;
		}

		if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
			hb::logger::error("[TP] sqlite3_open failed for {}: {}", path,
				m_db ? sqlite3_errmsg(m_db) : "unknown");
			if (m_db != nullptr) {
				sqlite3_close(m_db);
				m_db = nullptr;
			}
			return false;
		}

		sqlite3_busy_timeout(m_db, 1000);
		if (!exec_sql(m_db, "PRAGMA foreign_keys = ON;")) {
			close();
			return false;
		}
		if (!ensure_schema()) {
			close();
			return false;
		}
		return true;
	}

	void trading_post_store::close()
	{
		if (m_db != nullptr) {
			sqlite3_close(m_db);
			m_db = nullptr;
		}
	}

	bool trading_post_store::ensure_schema()
	{
		// Item columns mirror character_bank_items (AccountSqliteStore.cpp:434).
		// ON DELETE CASCADE on the item/offer tables relies on foreign_keys=ON,
		// set in open(): deleting a listings row removes its listing_items,
		// offers, and (transitively) offer_items; deleting an offers row removes
		// its offer_items.
		const char* schema =
			"BEGIN;"
			"CREATE TABLE IF NOT EXISTS meta ("
			" key TEXT PRIMARY KEY,"
			" value TEXT NOT NULL"
			");"
			"INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version','1');"
			"CREATE TABLE IF NOT EXISTS listings ("
			" listing_id      INTEGER PRIMARY KEY AUTOINCREMENT,"
			" seller_name     TEXT NOT NULL,"
			" seller_account  TEXT NOT NULL,"
			" seller_nation   INTEGER NOT NULL,"
			" seeking_note    TEXT NOT NULL DEFAULT '',"
			" created_at      INTEGER NOT NULL,"
			" expires_at      INTEGER NOT NULL"
			");"
			"CREATE TABLE IF NOT EXISTS listing_items ("
			" listing_id  INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,"
			" slot        INTEGER NOT NULL,"
			" item_id INTEGER NOT NULL,"
			" count INTEGER NOT NULL,"
			" touch_effect_type INTEGER NOT NULL,"
			" touch_effect_value1 INTEGER NOT NULL,"
			" touch_effect_value2 INTEGER NOT NULL,"
			" touch_effect_value3 INTEGER NOT NULL,"
			" item_color INTEGER NOT NULL,"
			" spec_effect_value1 INTEGER NOT NULL,"
			" spec_effect_value2 INTEGER NOT NULL,"
			" spec_effect_value3 INTEGER NOT NULL,"
			" cur_lifespan INTEGER NOT NULL,"
			" attribute INTEGER NOT NULL,"
			" PRIMARY KEY (listing_id, slot)"
			");"
			"CREATE TABLE IF NOT EXISTS offers ("
			" offer_id        INTEGER PRIMARY KEY AUTOINCREMENT,"
			" listing_id      INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,"
			" offerer_name    TEXT NOT NULL,"
			" offerer_account TEXT NOT NULL,"
			" created_at      INTEGER NOT NULL,"
			" UNIQUE (listing_id, offerer_name)"
			");"
			"CREATE TABLE IF NOT EXISTS offer_items ("
			" offer_id  INTEGER NOT NULL REFERENCES offers(offer_id) ON DELETE CASCADE,"
			" slot      INTEGER NOT NULL,"
			" item_id INTEGER NOT NULL,"
			" count INTEGER NOT NULL,"
			" touch_effect_type INTEGER NOT NULL,"
			" touch_effect_value1 INTEGER NOT NULL,"
			" touch_effect_value2 INTEGER NOT NULL,"
			" touch_effect_value3 INTEGER NOT NULL,"
			" item_color INTEGER NOT NULL,"
			" spec_effect_value1 INTEGER NOT NULL,"
			" spec_effect_value2 INTEGER NOT NULL,"
			" spec_effect_value3 INTEGER NOT NULL,"
			" cur_lifespan INTEGER NOT NULL,"
			" attribute INTEGER NOT NULL,"
			" PRIMARY KEY (offer_id, slot)"
			");"
			"CREATE TABLE IF NOT EXISTS notices ("
			" notice_id       INTEGER PRIMARY KEY AUTOINCREMENT,"
			" character_name  TEXT NOT NULL,"
			" message         TEXT NOT NULL,"
			" created_at      INTEGER NOT NULL"
			");"
			"COMMIT;";

		return exec_sql(m_db, schema);
	}

	CItem* trading_post_store::build_item(const escrow_item& e) const
	{
		if (m_game == nullptr || m_game->m_item_manager == nullptr) {
			return nullptr;
		}

		// Mirror the bank-row -> CItem deserialization (Game.cpp:4283): config
		// template via init_item_attr, then overlay the stored instance columns.
		CItem* item = new CItem();
		if (!m_game->m_item_manager->init_item_attr(item, static_cast<int>(e.item_id))) {
			delete item;
			return nullptr;
		}
		item->m_instance.count = e.count;
		item->m_instance.touch_effect_type = e.touch_effect_type;
		item->m_instance.touch_effect_value1 = e.touch_effect_value1;
		item->m_instance.touch_effect_value2 = e.touch_effect_value2;
		item->m_instance.touch_effect_value3 = e.touch_effect_value3;
		item->m_instance.item_color = static_cast<int8_t>(e.item_color);
		item->m_instance.special_effect_value1 = e.spec_effect_value1;
		item->m_instance.special_effect_value2 = e.spec_effect_value2;
		item->m_instance.special_effect_value3 = e.spec_effect_value3;
		item->m_instance.cur_durability = static_cast<uint16_t>(e.cur_lifespan);
		// PHASE5-TP: escrow_item still stores the legacy packed `attribute`; the
		// item-instance fields (custom_made/prefix/secondary/enchant) are not
		// reconstructed until the Phase-5 schema rework. Escrowed test data is
		// expendable (tradingpost.db is recreated with the new schema).
		m_game->m_item_manager->adjust_rare_item_value(item);
		if (item->m_instance.cur_durability > item->m_durability) {
			item->m_instance.cur_durability = item->m_durability;
		}
		return item;
	}

	std::string trading_post_store::describe(const escrow_item& e) const
	{
		char name[64] = "?";
		if (m_game != nullptr && m_game->m_item_manager != nullptr) {
			CItem tmp;
			if (m_game->m_item_manager->init_item_attr(&tmp, static_cast<int>(e.item_id))) {
				std::snprintf(name, sizeof(name), "%s", tmp.m_name);
			}
		}
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%s x%llu (id=%d attr=0x%08X)",
			name, static_cast<unsigned long long>(e.count),
			static_cast<int>(e.item_id), e.attribute);
		return buf;
	}

	bool trading_post_store::pull_items_from_inventory(int client_h,
		const hb::net::TpEscrowSlot* slots, int count, int max_items,
		std::vector<escrow_item>& out_items, uint16_t& out_result)
	{
		out_items.clear();
		out_result = hb::net::TpResultCode::Failed;

		if (m_game == nullptr) {
			return false;
		}
		CClient* client = m_game->m_client_list[client_h];
		if (client == nullptr) {
			return false;
		}
		if (slots == nullptr || count < 1 || count > max_items) {
			out_result = hb::net::TpResultCode::InvalidBundle;
			return false;
		}

		// Pass 1: validate + snapshot. Nothing is mutated until every slot in the
		// bundle is proven valid, so a rejected request removes no items.
		struct pending { int slot; uint64_t amount; };
		std::vector<pending> pend;
		std::vector<escrow_item> snap;
		std::vector<int> seen;
		pend.reserve(count);
		snap.reserve(count);
		seen.reserve(count);

		for (int i = 0; i < count; i++) {
			const int slot = slots[i].inv_slot;
			const int32_t amount = slots[i].amount;

			if (slot < 0 || slot >= hb::shared::limits::MaxItems) {
				out_result = hb::net::TpResultCode::InventoryChanged;
				return false;
			}
			CItem* it = client->m_item_list[slot];
			if (it == nullptr) {
				out_result = hb::net::TpResultCode::InventoryChanged;
				return false;
			}
			if (amount <= 0 || static_cast<uint64_t>(amount) > it->m_instance.count) {
				out_result = hb::net::TpResultCode::InvalidBundle;
				return false;
			}
			bool dup = false;
			for (int s : seen) {
				if (s == slot) { dup = true; break; }
			}
			if (dup) {
				out_result = hb::net::TpResultCode::InvalidBundle;
				return false;
			}
			seen.push_back(slot);

			escrow_item e;
			e.item_id = it->m_id_num;
			e.count = static_cast<uint64_t>(amount);
			e.touch_effect_type = it->m_instance.touch_effect_type;
			e.touch_effect_value1 = it->m_instance.touch_effect_value1;
			e.touch_effect_value2 = it->m_instance.touch_effect_value2;
			e.touch_effect_value3 = it->m_instance.touch_effect_value3;
			e.item_color = static_cast<uint8_t>(it->m_instance.item_color);
			e.spec_effect_value1 = it->m_instance.special_effect_value1;
			e.spec_effect_value2 = it->m_instance.special_effect_value2;
			e.spec_effect_value3 = it->m_instance.special_effect_value3;
			e.cur_lifespan = it->m_instance.cur_durability;
			e.attribute = 0; // PHASE5-TP: packed attribute retired; instance fields land with the schema rework
			snap.push_back(e);
			pend.push_back({ slot, static_cast<uint64_t>(amount) });
		}

		// Pass 2: remove from the in-memory inventory. Slot indices stay valid
		// because we never pack here. amount < held is necessarily a stackable
		// partial (a non-stackable holds count 1), so it decrements; otherwise
		// the whole item leaves the slot (unequipping first, like the Exchange
		// giver path in ItemManager.cpp:4339).
		for (const auto& p : pend) {
			CItem* it = client->m_item_list[p.slot];
			if (it == nullptr) {
				continue;
			}
			const uint64_t held = it->m_instance.count;
			if (p.amount < held) {
				m_game->m_item_manager->set_item_count(client_h, p.slot, held - p.amount);
			}
			else {
				const bool was_equipped = client->m_is_item_equipped[p.slot];
				m_game->m_item_manager->release_item_handler(client_h, static_cast<short>(p.slot), true);
				if (was_equipped) {
					m_game->send_notify_msg(0, client_h, Notify::ItemReleased,
						client->m_item_list[p.slot]->m_equip_pos, p.slot, 0, nullptr);
				}
				delete client->m_item_list[p.slot];
				client->m_item_list[p.slot] = nullptr;
				client->m_is_item_equipped[p.slot] = false;
				// Standard inventory-slot erase (as used by the deplete path);
				// Phase 5's dialog relies on this to vanish escrowed items.
				m_game->send_notify_msg(0, client_h, Notify::ItemDepletedEraseItem, p.slot, 0, 0, nullptr);
			}
		}
		m_game->calc_total_weight(client_h);

		// Forced save so the reduced inventory is durable BEFORE any escrow row
		// exists. A crash from here to the escrow insert loses the items (they
		// are gone from the saved character); it can never dupe them.
		if (g_login != nullptr) {
			g_login->local_save_player_data(client_h);
		}

		out_items = std::move(snap);
		out_result = hb::net::TpResultCode::Ok;
		return true;
	}

	bool trading_post_store::insert_item_rows(const char* table, const char* id_column,
		int64_t owner_id, const std::vector<escrow_item>& items)
	{
		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO %s(%s, slot, item_id, count, touch_effect_type,"
			" touch_effect_value1, touch_effect_value2, touch_effect_value3, item_color,"
			" spec_effect_value1, spec_effect_value2, spec_effect_value3, cur_lifespan, attribute)"
			" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
			table, id_column);

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}

		bool ok = true;
		for (int slot = 0; slot < static_cast<int>(items.size()) && ok; slot++) {
			const escrow_item& e = items[slot];
			sqlite3_reset(stmt);
			sqlite3_clear_bindings(stmt);
			int c = 1;
			ok &= (sqlite3_bind_int64(stmt, c++, owner_id) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, slot) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.item_id) == SQLITE_OK);
			ok &= (sqlite3_bind_int64(stmt, c++, static_cast<sqlite3_int64>(e.count)) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_type) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value1) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value2) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value3) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.item_color) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value1) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value2) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value3) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.cur_lifespan) == SQLITE_OK);
			ok &= (sqlite3_bind_int64(stmt, c++, static_cast<sqlite3_int64>(e.attribute)) == SQLITE_OK);
			if (ok && sqlite3_step(stmt) != SQLITE_DONE) {
				ok = false;
			}
		}

		sqlite3_finalize(stmt);
		return ok;
	}

	bool trading_post_store::create_listing(int client_h, const char* seeking_note,
		const hb::net::TpEscrowSlot* slots, int count,
		int64_t& out_listing_id, uint16_t& out_result)
	{
		out_listing_id = 0;
		out_result = hb::net::TpResultCode::Failed;
		if (!is_open() || m_game == nullptr) {
			return false;
		}
		CClient* seller = m_game->m_client_list[client_h];
		if (seller == nullptr) {
			return false;
		}

		// Escrow-in: validate + remove from inventory + forced save.
		std::vector<escrow_item> items;
		if (!pull_items_from_inventory(client_h, slots, count,
			hb::shared::limits::TpMaxListingItems, items, out_result)) {
			return false;
		}

		// The items are now out of the character's saved inventory. From here a
		// failed escrow insert is a loss, logged for GM recovery.
		const int64_t created = now_unix();
		const int64_t expires = created +
			static_cast<int64_t>(hb::shared::limits::TpListingExpiryDays) * 86400;

		bool ok = exec_sql(m_db, "BEGIN;");
		int64_t listing_id = 0;
		if (ok) {
			sqlite3_stmt* stmt = nullptr;
			ok = sqlite3_prepare_v2(m_db,
				"INSERT INTO listings(seller_name, seller_account, seller_nation,"
				" seeking_note, created_at, expires_at) VALUES(?,?,?,?,?,?);",
				-1, &stmt, nullptr) == SQLITE_OK;
			if (ok) {
				int c = 1;
				ok &= bind_text(stmt, c++, seller->m_char_name);
				ok &= bind_text(stmt, c++, seller->m_account_name);
				ok &= (sqlite3_bind_int(stmt, c++, static_cast<int>(seller->m_side)) == SQLITE_OK);
				ok &= bind_text(stmt, c++, seeking_note ? seeking_note : "");
				ok &= (sqlite3_bind_int64(stmt, c++, created) == SQLITE_OK);
				ok &= (sqlite3_bind_int64(stmt, c++, expires) == SQLITE_OK);
				if (ok && sqlite3_step(stmt) != SQLITE_DONE) {
					ok = false;
				}
			}
			if (stmt != nullptr) {
				sqlite3_finalize(stmt);
			}
		}
		if (ok) {
			listing_id = sqlite3_last_insert_rowid(m_db);
			ok = insert_item_rows("listing_items", "listing_id", listing_id, items);
		}
		if (ok) {
			ok = exec_sql(m_db, "COMMIT;");
		}

		if (!ok) {
			exec_sql(m_db, "ROLLBACK;");
			hb::logger::error("[TP] LISTING INSERT FAILED for {} - {} item(s) lost:",
				seller->m_char_name, static_cast<int>(items.size()));
			for (const auto& e : items) {
				hb::logger::error("[TP]   lost {}", describe(e));
			}
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}

		out_listing_id = listing_id;
		out_result = hb::net::TpResultCode::Ok;
		for (const auto& e : items) {
			hb::logger::log<log_channel::trade>("[TP] {} {} -> listing {} <- {}",
				seller->m_char_name, tp_action_name(ItemLogAction::TpList), listing_id, describe(e));
		}
		return true;
	}

	bool trading_post_store::place_offer(int client_h, int32_t listing_id,
		const hb::net::TpEscrowSlot* slots, int count,
		int64_t& out_offer_id, uint16_t& out_result)
	{
		out_offer_id = 0;
		out_result = hb::net::TpResultCode::Failed;
		if (!is_open() || m_game == nullptr) {
			return false;
		}
		CClient* offerer = m_game->m_client_list[client_h];
		if (offerer == nullptr) {
			return false;
		}

		// Loss-safety pre-checks: verify the two conditions whose violation would
		// make the offers INSERT fail *after* items were already escrowed (FK to a
		// vanished Listing, or the UNIQUE(listing_id, offerer_name) duplicate).
		// Richer business rules (<10 offers, same-account, proximity) are the
		// handler's job, but these guard against item loss on a race.
		{
			sqlite3_stmt* stmt = nullptr;
			bool exists = false;
			if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM listings WHERE listing_id=?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, listing_id);
				exists = (sqlite3_step(stmt) == SQLITE_ROW);
				sqlite3_finalize(stmt);
			}
			if (!exists) {
				out_result = hb::net::TpResultCode::ListingGone;
				return false;
			}
		}
		{
			sqlite3_stmt* stmt = nullptr;
			bool already = false;
			if (sqlite3_prepare_v2(m_db,
				"SELECT 1 FROM offers WHERE listing_id=? AND offerer_name=?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, listing_id);
				bind_text(stmt, 2, offerer->m_char_name);
				already = (sqlite3_step(stmt) == SQLITE_ROW);
				sqlite3_finalize(stmt);
			}
			if (already) {
				out_result = hb::net::TpResultCode::AlreadyOffered;
				return false;
			}
		}

		// Escrow-in: validate + remove from inventory + forced save.
		std::vector<escrow_item> items;
		if (!pull_items_from_inventory(client_h, slots, count,
			hb::shared::limits::TpMaxOfferItems, items, out_result)) {
			return false;
		}

		const int64_t created = now_unix();

		bool ok = exec_sql(m_db, "BEGIN;");
		int64_t offer_id = 0;
		if (ok) {
			sqlite3_stmt* stmt = nullptr;
			ok = sqlite3_prepare_v2(m_db,
				"INSERT INTO offers(listing_id, offerer_name, offerer_account, created_at)"
				" VALUES(?,?,?,?);", -1, &stmt, nullptr) == SQLITE_OK;
			if (ok) {
				int c = 1;
				ok &= (sqlite3_bind_int64(stmt, c++, listing_id) == SQLITE_OK);
				ok &= bind_text(stmt, c++, offerer->m_char_name);
				ok &= bind_text(stmt, c++, offerer->m_account_name);
				ok &= (sqlite3_bind_int64(stmt, c++, created) == SQLITE_OK);
				if (ok && sqlite3_step(stmt) != SQLITE_DONE) {
					ok = false;
				}
			}
			if (stmt != nullptr) {
				sqlite3_finalize(stmt);
			}
		}
		if (ok) {
			offer_id = sqlite3_last_insert_rowid(m_db);
			ok = insert_item_rows("offer_items", "offer_id", offer_id, items);
		}
		if (ok) {
			ok = exec_sql(m_db, "COMMIT;");
		}

		if (!ok) {
			exec_sql(m_db, "ROLLBACK;");
			hb::logger::error("[TP] OFFER INSERT FAILED for {} on listing {} - {} item(s) lost:",
				offerer->m_char_name, listing_id, static_cast<int>(items.size()));
			for (const auto& e : items) {
				hb::logger::error("[TP]   lost {}", describe(e));
			}
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}

		out_offer_id = offer_id;
		out_result = hb::net::TpResultCode::Ok;
		for (const auto& e : items) {
			hb::logger::log<log_channel::trade>("[TP] {} {} -> offer {} on listing {} <- {}",
				offerer->m_char_name, tp_action_name(ItemLogAction::TpOffer), offer_id, listing_id, describe(e));
		}
		return true;
	}

	bool trading_post_store::load_offer(int64_t offer_id, std::string& out_offerer,
		std::vector<escrow_item>& out_items)
	{
		out_offerer.clear();
		out_items.clear();

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, "SELECT offerer_name FROM offers WHERE offer_id=?;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, offer_id);
		bool found = false;
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const unsigned char* name = sqlite3_column_text(stmt, 0);
			if (name != nullptr) {
				out_offerer = reinterpret_cast<const char*>(name);
			}
			found = true;
		}
		sqlite3_finalize(stmt);
		if (!found) {
			return false;
		}

		if (sqlite3_prepare_v2(m_db,
			"SELECT item_id, count, touch_effect_type, touch_effect_value1,"
			" touch_effect_value2, touch_effect_value3, item_color, spec_effect_value1,"
			" spec_effect_value2, spec_effect_value3, cur_lifespan, attribute"
			" FROM offer_items WHERE offer_id=? ORDER BY slot;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, offer_id);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			escrow_item e;
			int c = 0;
			e.item_id = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.count = static_cast<uint64_t>(sqlite3_column_int64(stmt, c++));
			e.touch_effect_type = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.item_color = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.cur_lifespan = static_cast<uint16_t>(sqlite3_column_int(stmt, c++));
			e.attribute = static_cast<uint32_t>(sqlite3_column_int64(stmt, c++));
			out_items.push_back(e);
		}
		sqlite3_finalize(stmt);
		return true;
	}

	bool trading_post_store::deliver_to_bank(const char* character_name,
		const std::vector<escrow_item>& items)
	{
		if (items.empty()) {
			return true;
		}
		if (m_game == nullptr || character_name == nullptr) {
			return false;
		}

		const int h = m_game->find_client_by_name(character_name);
		if (h != 0 && m_game->m_client_list[h] != nullptr) {
			return deliver_to_online(h, items);
		}
		return deliver_to_offline(character_name, items);
	}

	bool trading_post_store::deliver_to_online(int client_h, const std::vector<escrow_item>& items)
	{
		bool all_ok = true;
		for (const auto& e : items) {
			CItem* item = build_item(e);
			if (item == nullptr) {
				hb::logger::error("[TP] deliver(online): unknown item id {} - lost",
					static_cast<int>(e.item_id));
				all_ok = false;
				continue;
			}
			if (!m_game->m_item_manager->set_item_to_bank_item(client_h, item)) {
				hb::logger::error("[TP] deliver(online): Warehouse full for {} - lost {}",
					m_game->m_client_list[client_h]->m_char_name, describe(e));
				delete item;
				all_ok = false;
			}
			// On success the Warehouse now owns `item`.
		}

		// Persist the bank additions immediately.
		if (g_login != nullptr) {
			g_login->local_save_player_data(client_h);
		}
		return all_ok;
	}

	bool trading_post_store::deliver_to_offline(const char* character_name,
		const std::vector<escrow_item>& items)
	{
		char account_name[32];
		std::memset(account_name, 0, sizeof(account_name));
		if (!ResolveCharacterToAccount(character_name, account_name, sizeof(account_name))) {
			hb::logger::error("[TP] deliver(offline): cannot resolve account for {} - {} item(s) lost",
				character_name, static_cast<int>(items.size()));
			for (const auto& e : items) {
				hb::logger::error("[TP]   lost {}", describe(e));
			}
			return false;
		}

		sqlite3* db = nullptr;
		std::string path;
		if (!EnsureAccountDatabase(account_name, &db, path)) {
			hb::logger::error("[TP] deliver(offline): cannot open account DB {} - {} item(s) lost",
				account_name, static_cast<int>(items.size()));
			for (const auto& e : items) {
				hb::logger::error("[TP]   lost {}", describe(e));
			}
			return false;
		}

		// Next free Warehouse slot = max(slot)+1, bounded only by the hard cap.
		int next_slot = 0;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(db,
				"SELECT COALESCE(MAX(slot), -1) FROM character_bank_items WHERE character_name=?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				bind_text(stmt, 1, character_name);
				if (sqlite3_step(stmt) == SQLITE_ROW) {
					next_slot = sqlite3_column_int(stmt, 0) + 1;
				}
				sqlite3_finalize(stmt);
			}
		}

		std::vector<AccountDbBankItemRow> rows;
		bool all_ok = true;
		for (const auto& e : items) {
			if (next_slot >= hb::shared::limits::MaxBankItems) {
				hb::logger::error("[TP] deliver(offline): Warehouse full for {} - lost {}",
					character_name, describe(e));
				all_ok = false;
				continue;
			}
			AccountDbBankItemRow r{};
			r.slot = next_slot++;
			r.item_id = e.item_id;
			r.count = static_cast<int64_t>(e.count);
			r.touch_effect_type = e.touch_effect_type;
			r.touch_effect_value1 = e.touch_effect_value1;
			r.touch_effect_value2 = e.touch_effect_value2;
			r.touch_effect_value3 = e.touch_effect_value3;
			r.item_color = e.item_color;
			r.spec_effect_value1 = e.spec_effect_value1;
			r.spec_effect_value2 = e.spec_effect_value2;
			r.spec_effect_value3 = e.spec_effect_value3;
			// PHASE5-TP: only durability carries over; attribute fields are
			// reconstructed when escrow adopts the item-instance schema.
			r.cur_durability = e.cur_lifespan;
			r.custom_made = 0;
			r.prefix_type = 0;
			r.prefix_value = 0;
			r.secondary_type = 0;
			r.secondary_value = 0;
			r.enchant_bonus = 0;
			rows.push_back(r);
		}

		if (!rows.empty()) {
			bool inserted = false;
			if (exec_sql(db, "BEGIN;")) {
				if (InsertCharacterBankItems(db, character_name, rows) && exec_sql(db, "COMMIT;")) {
					inserted = true;
				}
				else {
					exec_sql(db, "ROLLBACK;");
				}
			}
			if (!inserted) {
				hb::logger::error("[TP] deliver(offline): bank insert failed for {} - {} item(s) lost",
					character_name, static_cast<int>(rows.size()));
				for (const auto& e : items) {
					hb::logger::error("[TP]   lost {}", describe(e));
				}
				all_ok = false;
			}
		}

		CloseAccountDatabase(db);
		return all_ok;
	}

	bool trading_post_store::refund_offer(int64_t offer_id, int log_action)
	{
		if (!is_open()) {
			return false;
		}

		std::string offerer;
		std::vector<escrow_item> items;
		if (!load_offer(offer_id, offerer, items)) {
			return false; // already gone (rescinded, finalized, or a race loser)
		}

		// Delete the escrow rows and commit BEFORE delivering (offer_items cascade
		// off the offers row). A crash between the commit and delivery loses the
		// items; it can never dupe them.
		bool deleted = false;
		if (exec_sql(m_db, "BEGIN;")) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db, "DELETE FROM offers WHERE offer_id=?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, offer_id);
				deleted = (sqlite3_step(stmt) == SQLITE_DONE);
				sqlite3_finalize(stmt);
			}
			if (deleted && exec_sql(m_db, "COMMIT;")) {
				// committed
			}
			else {
				exec_sql(m_db, "ROLLBACK;");
				deleted = false;
			}
		}
		if (!deleted) {
			hb::logger::error("[TP] refund: failed to delete offer {}", offer_id);
			return false;
		}

		const bool delivered = deliver_to_bank(offerer.c_str(), items);
		for (const auto& e : items) {
			hb::logger::log<log_channel::trade>("[TP] {} offer {} -> {} <- {}",
				tp_action_name(log_action), offer_id, offerer, describe(e));
		}
		return delivered;
	}

	int trading_post_store::refund_all_offers_on_listing(int64_t listing_id)
	{
		if (!is_open()) {
			return 0;
		}

		std::vector<int64_t> ids;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, "SELECT offer_id FROM offers WHERE listing_id=? ORDER BY offer_id;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, listing_id);
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				ids.push_back(sqlite3_column_int64(stmt, 0));
			}
			sqlite3_finalize(stmt);
		}

		int refunded = 0;
		for (int64_t id : ids) {
			if (refund_offer(id, ItemLogAction::TpRefund)) {
				refunded++;
			}
		}
		return refunded;
	}

	// ---- Reads ---------------------------------------------------------------

	int trading_post_store::count_active_listings(const char* seller_name)
	{
		if (!is_open() || seller_name == nullptr) {
			return 0;
		}
		int n = 0;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"SELECT COUNT(*) FROM listings WHERE seller_name = ? COLLATE NOCASE;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			bind_text(stmt, 1, seller_name);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				n = sqlite3_column_int(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
		return n;
	}

	int trading_post_store::count_offers(int64_t listing_id)
	{
		if (!is_open()) {
			return 0;
		}
		int n = 0;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"SELECT COUNT(*) FROM offers WHERE listing_id = ?;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, listing_id);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				n = sqlite3_column_int(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
		return n;
	}

	bool trading_post_store::get_listing_owner(int64_t listing_id,
		std::string& out_seller_name, std::string& out_seller_account,
		int64_t& out_expires_at)
	{
		out_seller_name.clear();
		out_seller_account.clear();
		out_expires_at = 0;
		if (!is_open()) {
			return false;
		}
		sqlite3_stmt* stmt = nullptr;
		bool found = false;
		if (sqlite3_prepare_v2(m_db,
			"SELECT seller_name, seller_account, expires_at FROM listings WHERE listing_id = ?;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, listing_id);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				const unsigned char* s = sqlite3_column_text(stmt, 0);
				const unsigned char* a = sqlite3_column_text(stmt, 1);
				if (s != nullptr) out_seller_name = reinterpret_cast<const char*>(s);
				if (a != nullptr) out_seller_account = reinterpret_cast<const char*>(a);
				out_expires_at = sqlite3_column_int64(stmt, 2);
				found = true;
			}
			sqlite3_finalize(stmt);
		}
		return found;
	}

	bool trading_post_store::get_offer_owner(int64_t offer_id,
		std::string& out_offerer_name, std::string& out_offerer_account,
		int64_t& out_listing_id)
	{
		out_offerer_name.clear();
		out_offerer_account.clear();
		out_listing_id = 0;
		if (!is_open()) {
			return false;
		}
		sqlite3_stmt* stmt = nullptr;
		bool found = false;
		if (sqlite3_prepare_v2(m_db,
			"SELECT offerer_name, offerer_account, listing_id FROM offers WHERE offer_id = ?;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, offer_id);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				const unsigned char* s = sqlite3_column_text(stmt, 0);
				const unsigned char* a = sqlite3_column_text(stmt, 1);
				if (s != nullptr) out_offerer_name = reinterpret_cast<const char*>(s);
				if (a != nullptr) out_offerer_account = reinterpret_cast<const char*>(a);
				out_listing_id = sqlite3_column_int64(stmt, 2);
				found = true;
			}
			sqlite3_finalize(stmt);
		}
		return found;
	}

	bool trading_post_store::load_listing_items(int64_t listing_id,
		std::vector<escrow_item>& out)
	{
		out.clear();
		if (!is_open()) {
			return false;
		}
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"SELECT item_id, count, touch_effect_type, touch_effect_value1,"
			" touch_effect_value2, touch_effect_value3, item_color, spec_effect_value1,"
			" spec_effect_value2, spec_effect_value3, cur_lifespan, attribute"
			" FROM listing_items WHERE listing_id = ? ORDER BY slot;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, listing_id);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			escrow_item e;
			int c = 0;
			e.item_id = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.count = static_cast<uint64_t>(sqlite3_column_int64(stmt, c++));
			e.touch_effect_type = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.item_color = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.cur_lifespan = static_cast<uint16_t>(sqlite3_column_int(stmt, c++));
			e.attribute = static_cast<uint32_t>(sqlite3_column_int64(stmt, c++));
			out.push_back(e);
		}
		sqlite3_finalize(stmt);
		return !out.empty();
	}

	void trading_post_store::query_board(const char* count_sql, const char* rows_sql,
		const char* name_filter, int page, int page_size,
		std::vector<listing_brief>& out_rows, int& out_total_listings)
	{
		out_rows.clear();
		out_total_listings = 0;
		if (!is_open() || page_size < 1) {
			return;
		}
		if (page < 0) {
			page = 0;
		}

		// Unpaged total, for page-count math on the client.
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db, count_sql, -1, &stmt, nullptr) == SQLITE_OK) {
				if (name_filter != nullptr) {
					bind_text(stmt, 1, name_filter);
				}
				if (sqlite3_step(stmt) == SQLITE_ROW) {
					out_total_listings = sqlite3_column_int(stmt, 0);
				}
				sqlite3_finalize(stmt);
			}
		}

		// Newest Listings first, with the active-Offer count folded in. The optional
		// name filter is bound first; LIMIT/OFFSET are always the last two binds.
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, rows_sql, -1, &stmt, nullptr) != SQLITE_OK) {
			return;
		}
		int bind = 1;
		if (name_filter != nullptr) {
			bind_text(stmt, bind++, name_filter);
		}
		sqlite3_bind_int(stmt, bind++, page_size);
		sqlite3_bind_int(stmt, bind++, page * page_size);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			listing_brief b;
			int c = 0;
			b.listing_id = sqlite3_column_int64(stmt, c++);
			const unsigned char* s = sqlite3_column_text(stmt, c++);
			if (s != nullptr) b.seller_name = reinterpret_cast<const char*>(s);
			b.seller_nation = sqlite3_column_int(stmt, c++);
			const unsigned char* note = sqlite3_column_text(stmt, c++);
			if (note != nullptr) b.seeking_note = reinterpret_cast<const char*>(note);
			b.expires_at = sqlite3_column_int64(stmt, c++);
			b.offer_count = sqlite3_column_int(stmt, c++);
			out_rows.push_back(std::move(b));
		}
		sqlite3_finalize(stmt);

		// Fill each row's bundle preview (the page is small — TpBoardPageRows rows).
		for (auto& b : out_rows) {
			load_listing_items(b.listing_id, b.items);
		}
	}

	void trading_post_store::get_board_page(int page, int page_size,
		std::vector<listing_brief>& out_rows, int& out_total_listings)
	{
		query_board(
			"SELECT COUNT(*) FROM listings;",
			"SELECT l.listing_id, l.seller_name, l.seller_nation, l.seeking_note, l.expires_at,"
			" (SELECT COUNT(*) FROM offers o WHERE o.listing_id = l.listing_id)"
			" FROM listings l ORDER BY l.listing_id DESC LIMIT ? OFFSET ?;",
			nullptr, page, page_size, out_rows, out_total_listings);
	}

	void trading_post_store::get_my_listings_page(const char* seller_name, int page,
		int page_size, std::vector<listing_brief>& out_rows, int& out_total_listings)
	{
		if (seller_name == nullptr) {
			out_rows.clear();
			out_total_listings = 0;
			return;
		}
		query_board(
			"SELECT COUNT(*) FROM listings WHERE seller_name = ? COLLATE NOCASE;",
			"SELECT l.listing_id, l.seller_name, l.seller_nation, l.seeking_note, l.expires_at,"
			" (SELECT COUNT(*) FROM offers o WHERE o.listing_id = l.listing_id)"
			" FROM listings l WHERE l.seller_name = ? COLLATE NOCASE"
			" ORDER BY l.listing_id DESC LIMIT ? OFFSET ?;",
			seller_name, page, page_size, out_rows, out_total_listings);
	}

	void trading_post_store::get_my_offers_page(const char* offerer_name, int page,
		int page_size, std::vector<listing_brief>& out_rows, int& out_total_listings)
	{
		if (offerer_name == nullptr) {
			out_rows.clear();
			out_total_listings = 0;
			return;
		}
		// Listings this character has an active Offer on (their own Offer's items are
		// public too, but the row describes the Listing — the Seller stays the Seller).
		query_board(
			"SELECT COUNT(*) FROM listings WHERE listing_id IN"
			" (SELECT listing_id FROM offers WHERE offerer_name = ? COLLATE NOCASE);",
			"SELECT l.listing_id, l.seller_name, l.seller_nation, l.seeking_note, l.expires_at,"
			" (SELECT COUNT(*) FROM offers o WHERE o.listing_id = l.listing_id)"
			" FROM listings l WHERE l.listing_id IN"
			" (SELECT listing_id FROM offers WHERE offerer_name = ? COLLATE NOCASE)"
			" ORDER BY l.listing_id DESC LIMIT ? OFFSET ?;",
			offerer_name, page, page_size, out_rows, out_total_listings);
	}

	bool trading_post_store::get_listing_detail(int64_t listing_id, listing_detail& out)
	{
		out = listing_detail{};
		if (!is_open()) {
			return false;
		}

		sqlite3_stmt* stmt = nullptr;
		bool found = false;
		if (sqlite3_prepare_v2(m_db,
			"SELECT seller_name, seller_nation, seeking_note, expires_at"
			" FROM listings WHERE listing_id = ?;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, listing_id);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				int c = 0;
				const unsigned char* s = sqlite3_column_text(stmt, c++);
				if (s != nullptr) out.seller_name = reinterpret_cast<const char*>(s);
				out.seller_nation = sqlite3_column_int(stmt, c++);
				const unsigned char* note = sqlite3_column_text(stmt, c++);
				if (note != nullptr) out.seeking_note = reinterpret_cast<const char*>(note);
				out.expires_at = sqlite3_column_int64(stmt, c++);
				found = true;
			}
			sqlite3_finalize(stmt);
		}
		if (!found) {
			return false;
		}
		out.listing_id = listing_id;
		load_listing_items(listing_id, out.items);

		// Offers oldest-first, each with its escrowed bundle.
		std::vector<int64_t> offer_ids;
		if (sqlite3_prepare_v2(m_db,
			"SELECT offer_id FROM offers WHERE listing_id = ? ORDER BY offer_id;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, listing_id);
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				offer_ids.push_back(sqlite3_column_int64(stmt, 0));
			}
			sqlite3_finalize(stmt);
		}
		for (int64_t oid : offer_ids) {
			offer_view ov;
			ov.offer_id = oid;
			if (load_offer(oid, ov.offerer_name, ov.items)) {
				out.offers.push_back(std::move(ov));
			}
		}
		return true;
	}

	// ---- Row deletion (each its own committed txn) ---------------------------

	bool trading_post_store::delete_listing_row(int64_t listing_id)
	{
		bool ok = false;
		if (exec_sql(m_db, "BEGIN;")) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db, "DELETE FROM listings WHERE listing_id = ?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, listing_id);
				ok = (sqlite3_step(stmt) == SQLITE_DONE);
				sqlite3_finalize(stmt);
			}
			if (ok && exec_sql(m_db, "COMMIT;")) {
				// committed
			}
			else {
				exec_sql(m_db, "ROLLBACK;");
				ok = false;
			}
		}
		return ok;
	}

	bool trading_post_store::delete_offer_row(int64_t offer_id)
	{
		bool ok = false;
		if (exec_sql(m_db, "BEGIN;")) {
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db, "DELETE FROM offers WHERE offer_id = ?;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, offer_id);
				ok = (sqlite3_step(stmt) == SQLITE_DONE);
				sqlite3_finalize(stmt);
			}
			if (ok && exec_sql(m_db, "COMMIT;")) {
				// committed
			}
			else {
				exec_sql(m_db, "ROLLBACK;");
				ok = false;
			}
		}
		return ok;
	}

	// ---- Composed operations -------------------------------------------------

	bool trading_post_store::finalize(int64_t listing_id, int64_t offer_id, uint16_t& out_result)
	{
		out_result = hb::net::TpResultCode::Failed;
		if (!is_open()) {
			return false;
		}

		// Re-validate at request time (never trust the client's stale view): the
		// Listing and the winning Offer must both exist, and the Offer must belong
		// to this Listing.
		std::string seller_name, seller_account;
		int64_t expires_at = 0;
		if (!get_listing_owner(listing_id, seller_name, seller_account, expires_at)) {
			out_result = hb::net::TpResultCode::ListingGone;
			return false;
		}
		std::string winner_name, winner_account;
		int64_t offer_listing = 0;
		if (!get_offer_owner(offer_id, winner_name, winner_account, offer_listing)
			|| offer_listing != listing_id) {
			out_result = hb::net::TpResultCode::OfferGone;
			return false;
		}

		// Snapshot the two bundles that change hands, and the losing offerers (for
		// notices), before anything is deleted.
		std::vector<escrow_item> listing_items;   // Listing bundle -> winner
		std::vector<escrow_item> winning_items;   // winning Offer  -> Seller
		load_listing_items(listing_id, listing_items);
		{
			std::string ignore;
			load_offer(offer_id, ignore, winning_items);
		}
		std::vector<std::string> losers;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"SELECT offerer_name FROM offers WHERE listing_id = ? AND offer_id <> ? ORDER BY offer_id;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, listing_id);
				sqlite3_bind_int64(stmt, 2, offer_id);
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					const unsigned char* nm = sqlite3_column_text(stmt, 0);
					losers.emplace_back(nm ? reinterpret_cast<const char*>(nm) : "");
				}
				sqlite3_finalize(stmt);
			}
		}

		// ADR ordering: every escrow row is deleted + committed BEFORE its items are
		// delivered, so a crash in any window resolves to loss, never duplication.
		// 1) Remove the winning Offer so refund_all leaves it alone.
		if (!delete_offer_row(offer_id)) {
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}
		// 2) Refund the losing Offers to their offerers (delete+commit+deliver each).
		refund_all_offers_on_listing(listing_id);
		// 3) Remove the Listing (cascades its items; all Offers are gone by now).
		delete_listing_row(listing_id);

		// 4) Deliver from the in-memory snapshots.
		const bool to_seller = deliver_to_bank(seller_name.c_str(), winning_items);
		const bool to_winner = deliver_to_bank(winner_name.c_str(), listing_items);

		for (const auto& e : winning_items) {
			hb::logger::log<log_channel::trade>("[TP] {} listing {} offer {} -> {} <- {}",
				tp_action_name(ItemLogAction::TpTradeOut), listing_id, offer_id, seller_name, describe(e));
		}
		for (const auto& e : listing_items) {
			hb::logger::log<log_channel::trade>("[TP] {} listing {} offer {} -> {} <- {}",
				tp_action_name(ItemLogAction::TpTradeIn), listing_id, offer_id, winner_name, describe(e));
		}

		// 5) Notices. The Seller is the online actor and gets the action result; the
		// winner and losers are told out-of-band (chat now, or queued for login).
		notify_or_queue(winner_name.c_str(),
			"Your Trading Post Offer was accepted. The items are in your Warehouse.");
		for (const auto& nm : losers) {
			notify_or_queue(nm.c_str(),
				"Your Trading Post Offer was declined. Your items were returned to your Warehouse.");
		}

		out_result = (to_seller && to_winner) ? hb::net::TpResultCode::Ok
											  : hb::net::TpResultCode::WarehouseFull;
		return true;
	}

	bool trading_post_store::do_delist(int64_t listing_id, bool expired, uint16_t& out_result)
	{
		out_result = hb::net::TpResultCode::Failed;
		if (!is_open()) {
			return false;
		}

		std::string seller_name, seller_account;
		int64_t expires_at = 0;
		if (!get_listing_owner(listing_id, seller_name, seller_account, expires_at)) {
			out_result = hb::net::TpResultCode::ListingGone;
			return false;
		}

		std::vector<escrow_item> listing_items;
		load_listing_items(listing_id, listing_items);

		// Capture offerers before refunding (for notices).
		std::vector<std::string> offerers;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"SELECT offerer_name FROM offers WHERE listing_id = ? ORDER BY offer_id;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				sqlite3_bind_int64(stmt, 1, listing_id);
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					const unsigned char* nm = sqlite3_column_text(stmt, 0);
					offerers.emplace_back(nm ? reinterpret_cast<const char*>(nm) : "");
				}
				sqlite3_finalize(stmt);
			}
		}

		// ADR ordering: refund the Offers (each delete+commit before deliver), then
		// remove the Listing and return its bundle to the Seller.
		refund_all_offers_on_listing(listing_id);
		delete_listing_row(listing_id);
		const bool returned = deliver_to_bank(seller_name.c_str(), listing_items);

		for (const auto& e : listing_items) {
			hb::logger::log<log_channel::trade>("[TP] {} listing {} -> {} <- {}",
				tp_action_name(ItemLogAction::TpDelist), listing_id, seller_name, describe(e));
		}

		// Notices. On expiry the Seller is told too (they didn't initiate it); on a
		// manual delist the Seller is the online actor and gets the action result.
		if (expired) {
			notify_or_queue(seller_name.c_str(),
				"Your Trading Post Listing expired. Your items were returned to your Warehouse.");
		}
		for (const auto& nm : offerers) {
			notify_or_queue(nm.c_str(), expired
				? "A Listing you had an Offer on expired. Your items were returned to your Warehouse."
				: "A Listing you had an Offer on was delisted. Your items were returned to your Warehouse.");
		}

		out_result = returned ? hb::net::TpResultCode::Ok : hb::net::TpResultCode::WarehouseFull;
		return true;
	}

	bool trading_post_store::delist(int64_t listing_id, uint16_t& out_result)
	{
		return do_delist(listing_id, false, out_result);
	}

	void trading_post_store::sweep_expired()
	{
		if (!is_open()) {
			return;
		}
		const int64_t now = now_unix();
		std::vector<int64_t> ids;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"SELECT listing_id FROM listings WHERE expires_at < ? ORDER BY listing_id;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, now);
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				ids.push_back(sqlite3_column_int64(stmt, 0));
			}
			sqlite3_finalize(stmt);
		}
		if (ids.empty()) {
			return;
		}
		for (int64_t id : ids) {
			uint16_t result = 0;
			do_delist(id, true, result);
		}
		hb::logger::log<log_channel::trade>("[TP] expiry sweep delisted {} listing(s)",
			static_cast<int>(ids.size()));
	}

	void trading_post_store::void_character(const char* character_name)
	{
		if (!is_open() || character_name == nullptr) {
			return;
		}

		// 1) The character's own Listings: refund counterparties' Offers to them,
		// then destroy the character's escrowed bundle (never delivered — it shares
		// the fate of the deleted character's inventory) and remove the Listing.
		std::vector<int64_t> my_listings;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"SELECT listing_id FROM listings WHERE seller_name = ? COLLATE NOCASE ORDER BY listing_id;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				bind_text(stmt, 1, character_name);
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					my_listings.push_back(sqlite3_column_int64(stmt, 0));
				}
				sqlite3_finalize(stmt);
			}
		}
		for (int64_t lid : my_listings) {
			std::vector<std::string> offerers;
			{
				sqlite3_stmt* stmt = nullptr;
				if (sqlite3_prepare_v2(m_db,
					"SELECT offerer_name FROM offers WHERE listing_id = ? ORDER BY offer_id;",
					-1, &stmt, nullptr) == SQLITE_OK) {
					sqlite3_bind_int64(stmt, 1, lid);
					while (sqlite3_step(stmt) == SQLITE_ROW) {
						const unsigned char* nm = sqlite3_column_text(stmt, 0);
						offerers.emplace_back(nm ? reinterpret_cast<const char*>(nm) : "");
					}
					sqlite3_finalize(stmt);
				}
			}
			refund_all_offers_on_listing(lid);

			std::vector<escrow_item> listing_items;
			load_listing_items(lid, listing_items);
			delete_listing_row(lid);
			for (const auto& e : listing_items) {
				hb::logger::log<log_channel::trade>("[TP] void: destroyed {}'s listing {} item <- {}",
					character_name, lid, describe(e));
			}
			for (const auto& nm : offerers) {
				notify_or_queue(nm.c_str(),
					"A Listing you had an Offer on was removed. Your items were returned to your Warehouse.");
			}
		}

		// 2) The character's own Offers on other Sellers' Listings: destroy the
		// escrowed items (the character's own) and remove the Offers.
		std::vector<int64_t> my_offers;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"SELECT offer_id FROM offers WHERE offerer_name = ? COLLATE NOCASE ORDER BY offer_id;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				bind_text(stmt, 1, character_name);
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					my_offers.push_back(sqlite3_column_int64(stmt, 0));
				}
				sqlite3_finalize(stmt);
			}
		}
		for (int64_t oid : my_offers) {
			std::string ignore;
			std::vector<escrow_item> offer_items;
			load_offer(oid, ignore, offer_items);
			delete_offer_row(oid);
			for (const auto& e : offer_items) {
				hb::logger::log<log_channel::trade>("[TP] void: destroyed {}'s offer {} item <- {}",
					character_name, oid, describe(e));
			}
		}

		// 3) The character's queued notices.
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"DELETE FROM notices WHERE character_name = ? COLLATE NOCASE;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				bind_text(stmt, 1, character_name);
				sqlite3_step(stmt);
				sqlite3_finalize(stmt);
			}
		}

		if (!my_listings.empty() || !my_offers.empty()) {
			hb::logger::log<log_channel::trade>("[TP] void_character('{}'): {} listing(s), {} offer(s)",
				character_name, static_cast<int>(my_listings.size()), static_cast<int>(my_offers.size()));
		}
	}

	// ---- Notices -------------------------------------------------------------

	void trading_post_store::queue_notice(const char* character_name, const std::string& message)
	{
		if (!is_open() || character_name == nullptr) {
			return;
		}
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"INSERT INTO notices(character_name, message, created_at) VALUES(?,?,?);",
			-1, &stmt, nullptr) == SQLITE_OK) {
			bind_text(stmt, 1, character_name);
			bind_text(stmt, 2, message.c_str());
			sqlite3_bind_int64(stmt, 3, now_unix());
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
	}

	void trading_post_store::notify_or_queue(const char* character_name, const std::string& message)
	{
		if (m_game == nullptr || character_name == nullptr) {
			return;
		}
		const int h = m_game->find_client_by_name(character_name);
		if (h != 0 && m_game->m_client_list[h] != nullptr
			&& m_game->m_client_list[h]->m_is_init_complete) {
			m_game->send_notify_msg(0, h, Notify::NoticeMsg, 0, 0, 0, message.c_str());
		}
		else {
			queue_notice(character_name, message);
		}
	}

	void trading_post_store::flush_notices(int client_h)
	{
		if (!is_open() || m_game == nullptr) {
			return;
		}
		CClient* client = m_game->m_client_list[client_h];
		if (client == nullptr) {
			return;
		}

		std::vector<std::string> messages;
		{
			sqlite3_stmt* stmt = nullptr;
			if (sqlite3_prepare_v2(m_db,
				"SELECT message FROM notices WHERE character_name = ? COLLATE NOCASE ORDER BY notice_id;",
				-1, &stmt, nullptr) == SQLITE_OK) {
				bind_text(stmt, 1, client->m_char_name);
				while (sqlite3_step(stmt) == SQLITE_ROW) {
					const unsigned char* m = sqlite3_column_text(stmt, 0);
					messages.emplace_back(m ? reinterpret_cast<const char*>(m) : "");
				}
				sqlite3_finalize(stmt);
			}
		}
		if (messages.empty()) {
			return;
		}

		for (const auto& m : messages) {
			m_game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, m.c_str());
		}

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"DELETE FROM notices WHERE character_name = ? COLLATE NOCASE;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			bind_text(stmt, 1, client->m_char_name);
			sqlite3_step(stmt);
			sqlite3_finalize(stmt);
		}
	}
}
