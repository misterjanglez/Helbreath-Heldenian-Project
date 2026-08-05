#include "TradingPostStore.h"

#include "Game.h"
#include "Client.h"
#include "Item.h"
#include "ItemLedgerStore.h"   // detail_json, for the escrow events' `detail`
#include "ItemManager.h"
#include "LoginServer.h"
#include "AccountSqliteStore.h"
#include "GameDatabase.h"
#include "Packet/PacketTradingPost.h"
#include "sqlite3.h"
#include "Log.h"

#include "ServerLogChannels.h"

#include <chrono>
#include <cstring>
#include <cstdio>
#include <format>

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
	bool custody_scope::finish()
	{
		if (m_finished) {
			return false;
		}
		m_finished = true;

		if (m_ok && m_txn.commit()) {
			// Durable. Only now can the ledger be told, because only now is the
			// move a fact.
			for (auto& emit : m_emit) {
				emit();
			}
			return true;
		}

		m_txn.rollback();

		// Reverse order, for the same reason any undo stack is: a later step may
		// depend on an earlier one, and unwinding forwards would undo the
		// foundation before the thing standing on it.
		for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it) {
			(*it)();
		}
		return false;
	}

	trading_post_store::~trading_post_store()
	{
		close();
	}

	bool trading_post_store::open(sqlite3* db)
	{
		if (db == nullptr) {
			hb::logger::error("[TP] cannot open the escrow store: no Game DB connection");
			return false;
		}
		// The schema is already on the connection — EnsureGameSchema lays the
		// escrow tables down with the character tables, because they share one
		// version stamp now (see AccountSqliteStore.cpp).
		m_db = db;
		return true;
	}

	void trading_post_store::close()
	{
		// Borrowed, never owned: the Game DB connection outlives this store and
		// is closed by CloseGameDatabase().
		m_db = nullptr;
	}

	CItem* trading_post_store::build_item(const escrow_item& e) const
	{
		if (m_game == nullptr || m_game->m_item_manager == nullptr) {
			return nullptr;
		}

		// Mirror the bank-row -> CItem deserialization in CGame's character
		// load: config template via init_item_attr, then overlay the stored
		// instance columns.
		// A listing in escrow is an item that already exists, so this restores
		// its identity rather than minting a new one: the Serial stored on the
		// escrow row is the one the item goes back into the world with.
		CItem* item = m_game->m_item_manager->restore_item(static_cast<int>(e.item_id), e.serial);
		if (item == nullptr) return nullptr;
		item->m_instance.count = e.count;
		item->m_instance.touch_effect_type = e.touch_effect_type;
		item->m_instance.touch_effect_value1 = e.touch_effect_value1;
		item->m_instance.touch_effect_value2 = e.touch_effect_value2;
		item->m_instance.touch_effect_value3 = e.touch_effect_value3;
		item->m_instance.item_color = static_cast<int8_t>(e.item_color);
		item->m_instance.special_effect_value1 = e.spec_effect_value1;
		item->m_instance.special_effect_value2 = e.spec_effect_value2;
		item->m_instance.special_effect_value3 = e.spec_effect_value3;
		item->m_instance.cur_durability = e.cur_durability;
		item->set_attributes(e.attributes);
		if (item->is_custom_made()) {
			item->m_durability = item->m_instance.special_effect_value1;
		}
		m_game->m_item_manager->apply_modifier_derived_stats(item);
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
		std::snprintf(buf, sizeof(buf), "%s x%llu (id=%d pfx=%d/%d sec=%d/%d ench=%d%s)",
			name, static_cast<unsigned long long>(e.count),
			static_cast<int>(e.item_id), e.attributes.modifiers[0].type, e.attributes.modifiers[0].value,
			e.attributes.modifiers[1].type, e.attributes.modifiers[1].value, e.attributes.enchant_bonus,
			e.attributes.custom_made ? " custom" : "");
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
			// A partial stack split leaves its Serial behind with the remainder,
			// which for a stackable is 0 either way — merge and split dissolve
			// identity, so there is nothing to divide (D2). A non-stackable
			// always moves whole and carries its Serial into escrow.
			e.serial = it->m_serial;
			e.count = static_cast<uint64_t>(amount);
			e.touch_effect_type = it->m_instance.touch_effect_type;
			e.touch_effect_value1 = it->m_instance.touch_effect_value1;
			e.touch_effect_value2 = it->m_instance.touch_effect_value2;
			e.touch_effect_value3 = it->m_instance.touch_effect_value3;
			e.item_color = static_cast<uint8_t>(it->m_instance.item_color);
			e.spec_effect_value1 = it->m_instance.special_effect_value1;
			e.spec_effect_value2 = it->m_instance.special_effect_value2;
			e.spec_effect_value3 = it->m_instance.special_effect_value3;
			e.cur_durability = it->m_instance.cur_durability;
			e.attributes = it->get_attributes();
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
				// flow_none: escrow is booked by record_escrow_event inside
				// scope.on_commit — tied to the DB commit, a stronger guarantee
				// than a flow here would be.
				m_game->m_item_manager->set_item_count(client_h, p.slot, held - p.amount,
					ItemManager::flow_none);
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

		// No save here. Until v9 this function forced one, so the reduced
		// inventory was durable BEFORE any escrow row existed and a crash in
		// between lost the items rather than duplicating them. The escrow tables
		// are in game.db now, so the caller puts that save and the escrow insert
		// in ONE transaction (#82) — and a failure rewinds to before the items
		// left, instead of resolving to the lesser of two bad outcomes.
		out_items = std::move(snap);
		out_result = hb::net::TpResultCode::Ok;
		return true;
	}

	void trading_post_store::return_items_to_inventory(int client_h,
		const std::vector<escrow_item>& items)
	{
		// The undo half of pull_items_from_inventory: the transaction that was
		// meant to make the escrow durable rolled back, so the items never left
		// as far as the database is concerned and memory has to agree.
		//
		// Rebuilt through build_item rather than kept aside as live objects,
		// because the pull destroys the CItem (a partial stack never had a
		// separate one at all) — and rebuilding from the snapshot is what
		// carries the Serial back, so the item that returns is the same instance
		// that was about to be listed.
		//
		// The original slot and equipped state are not restored: add_item finds a
		// slot and merges stackables back into the stack the pull decremented,
		// which is what the player's client is told. A returned item being
		// unequipped is a visible difference from nothing having happened, and it
		// is the honest one — the alternative is re-equipping something the
		// player watched come off.
		if (m_game == nullptr || m_game->m_item_manager == nullptr) {
			return;
		}
		if (m_game->m_client_list[client_h] == nullptr) {
			// Logged out inside the failed operation. Nothing to hand back to,
			// and the rollback already restored the saved inventory that still
			// holds them, so the character logs back in with the items.
			return;
		}

		for (const auto& e : items) {
			CItem* item = build_item(e);
			if (item == nullptr) {
				hb::logger::error("[TP] rollback: cannot rebuild item id {} - lost",
					static_cast<int>(e.item_id));
				continue;
			}
			m_game->m_item_manager->add_item(client_h, item, 0);
		}
		m_game->calc_total_weight(client_h);
	}

	bool trading_post_store::save_character(int client_h) const
	{
		// Deliberately NOT g_login->local_save_player_data: that would take the
		// live game.db handle, while this store may be pointed at a scratch file
		// (the provers), and it is also where the block-list write lives, which
		// has nothing to do with a custody move. SaveCharacterSnapshot on this
		// store's own connection is what keeps the character half of the move
		// inside the same transaction as the escrow half.
		if (m_game == nullptr || m_db == nullptr) {
			return false;
		}
		const CClient* client = m_game->m_client_list[client_h];
		if (client == nullptr) {
			return false;
		}
		return SaveCharacterSnapshot(m_db, client);
	}

	// ---- Provenance Ledger (#80) ---------------------------------------------

	void trading_post_store::record_escrow_event(const escrow_move& move,
		const std::vector<escrow_item>& items,
		const char* actor_char, const CClient* actor)
	{
		if (m_game == nullptr || m_game->m_item_manager == nullptr) {
			return;
		}
		ItemManager& manager = *m_game->m_item_manager;

		// The actor and the ids are properties of the transition, not of the
		// item, so the shared half of the row is assembled once and the loop
		// below only adds a Serial. Four items in a Listing all moved for the
		// same reason, at the same moment, between the same two parties.
		hb::server::ledger_event_record base = manager.ledger_actor(actor_char, actor);
		if (move.counterparty_char != nullptr) {
			base.counterparty_char = move.counterparty_char;
		}
		base.detail = move.offer_id != 0
			? hb::server::detail_json({ { "listing", move.listing_id }, { "offer", move.offer_id } })
			: hb::server::detail_json("listing", move.listing_id);

		for (const auto& e : items) {
			// Counted items are skipped inside record_ledger_event (serial 0), so
			// a bundle of gold costs a comparison and writes nothing. Their
			// aggregate flow counters are #81's — flow_type numbers are permanent
			// world fact and naming them while closing a different gap is how a
			// numbering gets picked badly.
			manager.record_ledger_event(move.action, e.serial, base);
		}
	}

	void trading_post_store::record_escrow_exit(const std::vector<int64_t>& serials,
		hb::server::destroy_reason::destroy_reason reason,
		const char* owner_char, const CClient* owner)
	{
		if (serials.empty() || m_game == nullptr || m_game->m_item_manager == nullptr) {
			return;
		}
		ItemManager& manager = *m_game->m_item_manager;

		hb::server::ledger_event_record base = manager.ledger_actor(owner_char, owner);
		base.detail = ItemManager::destroyed_detail(reason);

		for (int64_t serial : serials) {
			manager.record_ledger_event(hb::server::ledger_event::destroyed, serial, base);
		}
	}

	void trading_post_store::record_escrow_exit(const std::vector<escrow_item>& items,
		hb::server::destroy_reason::destroy_reason reason,
		const char* owner_char, const CClient* owner)
	{
		std::vector<int64_t> serials;
		serials.reserve(items.size());
		for (const auto& e : items) {
			serials.push_back(e.serial);
		}
		record_escrow_exit(serials, reason, owner_char, owner);
	}

	bool trading_post_store::insert_item_rows(const char* table, const char* id_column,
		int64_t owner_id, const std::vector<escrow_item>& items)
	{
		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO %s(%s, slot, item_id, serial, count, touch_effect_type,"
			" touch_effect_value1, touch_effect_value2, touch_effect_value3, item_color,"
			" spec_effect_value1, spec_effect_value2, spec_effect_value3, cur_durability,"
			HB_ITEM_ATTR_COLUMNS_SQL ")"
			" VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?," HB_ITEM_ATTR_PLACEHOLDERS_SQL ");",
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
			ok &= (sqlite3_bind_int64(stmt, c++, e.serial) == SQLITE_OK);
			ok &= (sqlite3_bind_int64(stmt, c++, static_cast<sqlite3_int64>(e.count)) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_type) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value1) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value2) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.touch_effect_value3) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.item_color) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value1) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value2) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.spec_effect_value3) == SQLITE_OK);
			ok &= (sqlite3_bind_int(stmt, c++, e.cur_durability) == SQLITE_OK);
			ok &= BindItemAttributeColumns(stmt, c, e.attributes);
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

		// Escrow-in: validate + remove from the in-memory inventory. Nothing is
		// durable yet — the transaction below is what makes the reduced inventory
		// and the escrow rows true at the same instant.
		std::vector<escrow_item> items;
		if (!pull_items_from_inventory(client_h, slots, count,
			hb::shared::limits::TpMaxListingItems, items, out_result)) {
			return false;
		}

		const int64_t created = now_unix();
		const int64_t expires = created +
			static_cast<int64_t>(hb::shared::limits::TpListingExpiryDays) * 86400;

		custody_scope scope(m_db);

		// Registered before anything can fail, so every exit below — including a
		// transaction that could not even begin — hands the bundle back.
		scope.on_rollback([this, client_h, items]()
		{
			return_items_to_inventory(client_h, items);
		});

		bool ok = scope.ok();
		// The seller's save comes FIRST inside the transaction, so a failure to
		// write the character costs nothing else. Order does not affect
		// atomicity — that is the point of one transaction — but it does decide
		// how much work a doomed operation does before it finds out.
		if (ok) {
			ok = save_character(client_h);
		}
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
		if (!ok) {
			scope.fail();
		}

		const std::string seller_name(seller->m_char_name);
		scope.on_commit([this, client_h, seller_name, listing_id, items]()
		{
			record_escrow_event({ ItemLogAction::TpList, nullptr, listing_id, 0 }, items,
				seller_name.c_str(), m_game->m_client_list[client_h]);
			for (const auto& e : items) {
				hb::logger::log<log_channel::trade>("[TP] {} {} -> listing {} <- {}",
					seller_name, tp_action_name(ItemLogAction::TpList), listing_id, describe(e));
			}
		});

		if (!scope.finish()) {
			// Nothing happened. The rollback puts the database back to before the
			// listing, and the registered undo puts the items back in the seller's
			// hands to match — so there is no exit event to write, because nothing
			// left. Until v9 this branch was a designed LOSS: the reduced inventory
			// was already committed by then and only the escrow half could be undone.
			hb::logger::error("[TP] LISTING FAILED for {} - rolled back, {} item(s) returned",
				seller_name, static_cast<int>(items.size()));
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}

		out_listing_id = listing_id;
		out_result = hb::net::TpResultCode::Ok;
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

		// Escrow-in: validate + remove from the in-memory inventory. See
		// create_listing — the transaction below is what makes the reduced
		// inventory and the escrow rows true at the same instant.
		std::vector<escrow_item> items;
		if (!pull_items_from_inventory(client_h, slots, count,
			hb::shared::limits::TpMaxOfferItems, items, out_result)) {
			return false;
		}

		const int64_t created = now_unix();

		custody_scope scope(m_db);
		scope.on_rollback([this, client_h, items]()
		{
			return_items_to_inventory(client_h, items);
		});

		bool ok = scope.ok();
		if (ok) {
			ok = save_character(client_h);
		}
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
		if (!ok) {
			scope.fail();
		}

		const std::string offerer_name(offerer->m_char_name);
		const int64_t parent_listing = listing_id;
		scope.on_commit([this, client_h, offerer_name, parent_listing, offer_id, items]()
		{
			record_escrow_event({ ItemLogAction::TpOffer, nullptr, parent_listing, offer_id }, items,
				offerer_name.c_str(), m_game->m_client_list[client_h]);
			for (const auto& e : items) {
				hb::logger::log<log_channel::trade>("[TP] {} {} -> offer {} on listing {} <- {}",
					offerer_name, tp_action_name(ItemLogAction::TpOffer), offer_id, parent_listing, describe(e));
			}
		});

		if (!scope.finish()) {
			// Nothing happened — see create_listing. The two pre-checks above
			// (vanished Listing, duplicate Offer) exist because they are the two
			// ways this INSERT fails for a reason the player can be told about;
			// reaching here means the database itself refused.
			hb::logger::error("[TP] OFFER FAILED for {} on listing {} - rolled back, {} item(s) returned",
				offerer_name, listing_id, static_cast<int>(items.size()));
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}

		out_offer_id = offer_id;
		out_result = hb::net::TpResultCode::Ok;
		return true;
	}

	bool trading_post_store::load_offer(int64_t offer_id, std::string& out_offerer,
		std::vector<escrow_item>& out_items, int64_t* out_listing_id)
	{
		out_offerer.clear();
		out_items.clear();
		if (out_listing_id != nullptr) *out_listing_id = 0;

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, "SELECT offerer_name, listing_id FROM offers WHERE offer_id=?;",
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
			if (out_listing_id != nullptr) *out_listing_id = sqlite3_column_int64(stmt, 1);
			found = true;
		}
		sqlite3_finalize(stmt);
		if (!found) {
			return false;
		}

		if (sqlite3_prepare_v2(m_db,
			"SELECT item_id, serial, count, touch_effect_type, touch_effect_value1,"
			" touch_effect_value2, touch_effect_value3, item_color, spec_effect_value1,"
			" spec_effect_value2, spec_effect_value3, cur_durability,"
			HB_ITEM_ATTR_COLUMNS_SQL
			" FROM offer_items WHERE offer_id=? ORDER BY slot;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, offer_id);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			escrow_item e;
			int c = 0;
			e.item_id = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.serial = sqlite3_column_int64(stmt, c++);
			e.count = static_cast<uint64_t>(sqlite3_column_int64(stmt, c++));
			e.touch_effect_type = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.item_color = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.cur_durability = static_cast<uint16_t>(sqlite3_column_int(stmt, c++));
			e.attributes = ReadItemAttributeColumns(stmt, c);

			if (!ItemAttributesLoadOk(e.attributes,
				std::format("offer_items offer {} item {}", offer_id, e.item_id))) {
				sqlite3_finalize(stmt);
				return false;
			}
			out_items.push_back(e);
		}
		sqlite3_finalize(stmt);
		return true;
	}

	bool trading_post_store::deliver_to_bank(const char* character_name,
		const std::vector<escrow_item>& items, const escrow_move& move,
		custody_scope& scope)
	{
		if (items.empty()) {
			return true;
		}
		if (m_game == nullptr || character_name == nullptr || !scope.ok()) {
			scope.fail();
			return false;
		}

		const int h = m_game->find_client_by_name(character_name);
		CClient* recipient = (h != 0) ? m_game->m_client_list[h] : nullptr;

		// What did not arrive. Neither mechanic records anything itself, so a new
		// failure branch in either one cannot forget to — it just does not report
		// the Serial as landed — and two branches cannot both claim the same one.
		std::vector<int64_t> lost;
		const bool all_ok = (recipient != nullptr)
			? deliver_to_online(h, items, lost, scope)
			: deliver_to_offline(character_name, items, lost, scope);

		// Both recordings are deferred to the commit. The custody move is true
		// whatever the delivery does — the escrow row is deleted in this same
		// transaction, so the item has left the board — but it is only true if
		// the transaction commits, and the ledger cannot take a row back. Until
		// v9 the escrow row was already committed by the time anyone reached
		// here, which is why this used to be recorded up front.
		//
		// Everything the emission needs is copied, not referenced: it runs after
		// this function has returned, and `move.counterparty_char` in particular
		// points at a caller's local string.
		const std::string recipient_name(character_name);
		const int action = move.action;
		const std::string counterparty(move.counterparty_char ? move.counterparty_char : "");
		const bool has_counterparty = (move.counterparty_char != nullptr);
		const int64_t listing_id = move.listing_id;
		const int64_t offer_id = move.offer_id;
		scope.on_commit([this, recipient_name, action, counterparty, has_counterparty,
			listing_id, offer_id, moved = items, lost]()
		{
			// Re-resolved rather than captured: the recipient may have logged out
			// between the delivery and the commit, and an event carrying a stale
			// CClient* would read a freed object for its location.
			const int now_h = m_game->find_client_by_name(recipient_name.c_str());
			CClient* now = (now_h != 0) ? m_game->m_client_list[now_h] : nullptr;
			const escrow_move recorded{ action,
				has_counterparty ? counterparty.c_str() : nullptr, listing_id, offer_id };
			record_escrow_event(recorded, moved, recipient_name.c_str(), now);
			record_escrow_exit(lost, hb::server::destroy_reason::delivery_lost,
				recipient_name.c_str(), now);
		});
		return all_ok;
	}

	bool trading_post_store::deliver_to_online(int client_h, const std::vector<escrow_item>& items,
		std::vector<int64_t>& out_lost, custody_scope& scope)
	{
		// Which objects this delivery put in the Warehouse, so a rollback can
		// take them back out. Pointer identity, not slot indices: a later step in
		// the same operation can deposit into a slot this one skipped, and the
		// undo has to remove exactly what it added.
		std::vector<CItem*> deposited;

		bool all_ok = true;
		for (const auto& e : items) {
			CItem* item = build_item(e);
			if (item == nullptr) {
				// No object was built, so there is nothing for destroy_item to end
				// — but the escrow row is deleted in this transaction and the item
				// is just as gone as one that failed to fit in a Warehouse.
				out_lost.push_back(e.serial);
				hb::logger::error("[TP] deliver(online): unknown item id {} - lost",
					static_cast<int>(e.item_id));
				all_ok = false;
				continue;
			}
			// already_recorded: deliver_to_bank emits the custody move for this
			// bundle (TpTradeIn / TpRefund / TpDelist), so a Deposit as well would
			// give one move two events (#81).
			if (!m_game->m_item_manager->set_item_to_bank_item(client_h, item,
				ItemManager::bank_deposit::already_recorded)) {
				hb::logger::error("[TP] deliver(online): Warehouse full for {} - lost {}",
					m_game->m_client_list[client_h]->m_char_name, describe(e));
				// Deliberately NOT reported in out_lost: this is the one failure
				// holding a live CItem, and destroy_item is the funnel every
				// object exit goes through, so the event is already written.
				// Reporting it as well would give one Serial two exits.
				m_game->m_item_manager->destroy_item(item,
					hb::server::destroy_reason::delivery_lost, client_h);
				all_ok = false;
			}
			else {
				// On success the Warehouse now owns `item`.
				deposited.push_back(item);
			}
		}

		// The bank additions are part of the same transaction as the escrow row
		// they came from. Not through g_login: this store may be pointed at a
		// scratch database, and a second connection's save would be a second
		// commit — the exact thing this ticket removes.
		if (!save_character(client_h)) {
			scope.fail();
			all_ok = false;
		}

		if (!deposited.empty()) {
			scope.on_rollback([this, client_h, deposited]()
			{
				remove_from_bank(client_h, deposited);
			});
		}
		return all_ok;
	}

	void trading_post_store::remove_from_bank(int client_h, const std::vector<CItem*>& items)
	{
		// The undo half of an online delivery. Without it a rewound escrow-out
		// leaves the item in the recipient's Warehouse in memory while the
		// database has it back on the board — one item in two places, which is
		// the duplication ADR 0001's ordering rules exist to prevent.
		CClient* client = (m_game != nullptr) ? m_game->m_client_list[client_h] : nullptr;
		if (client == nullptr) {
			// Logged out inside the failed operation. Their in-memory Warehouse
			// went with them and the rollback restored the saved one, which never
			// held these.
			return;
		}
		for (CItem* item : items) {
			for (int i = 0; i < hb::shared::limits::MaxBankItems; i++) {
				if (client->m_item_in_bank_list[i] == item) {
					client->m_item_in_bank_list[i] = nullptr;
					break;
				}
			}
			// Deleted, not destroy_item'd: destruction is an economic event and
			// this item is not leaving the world — it is going back into escrow,
			// where the restored row still describes it. An exit event here would
			// declare a Serial dead that the board still holds.
			delete item;
		}
	}

	bool trading_post_store::deliver_to_offline(const char* character_name,
		const std::vector<escrow_item>& items, std::vector<int64_t>& out_lost,
		custody_scope& scope)
	{
		// Nothing reached the account store, so the whole bundle is gone.
		auto lose_everything = [&]()
		{
			for (const auto& e : items) {
				out_lost.push_back(e.serial);
				hb::logger::error("[TP]   lost {}", describe(e));
			}
		};

		char account_name[32];
		std::memset(account_name, 0, sizeof(account_name));
		if (!ResolveCharacterToAccount(m_db, character_name, account_name, sizeof(account_name))) {
			hb::logger::error("[TP] deliver(offline): cannot resolve account for {} - {} item(s) lost",
				character_name, static_cast<int>(items.size()));
			lose_everything();
			return false;
		}

		// This store's own connection, not game_db_handle(): the escrow rows and
		// the Warehouse rows are in the same database now, and using the handle
		// the operation's transaction was opened on is what makes them one
		// commit. On the live server the two are the same pointer; on a prover's
		// scratch world they are not, and the difference used to leak probe
		// characters into the real game.db.
		sqlite3* db = m_db;
		if (db == nullptr) {
			hb::logger::error("[TP] deliver(offline): Game DB not open (account {}) - {} item(s) lost",
				account_name, static_cast<int>(items.size()));
			lose_everything();
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
				out_lost.push_back(e.serial);
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
			r.cur_durability = e.cur_durability;
			r.serial = e.serial;
			r.attributes = e.attributes;
			rows.push_back(r);
		}

		if (!rows.empty()) {
			// No transaction of its own any more: this insert belongs to the
			// operation's transaction, which is what pairs it with the deletion
			// of the escrow row it came from. A scope here would commit the
			// Warehouse half on its own and put the two-commit window back.
			if (!InsertCharacterBankItems(db, character_name, rows)) {
				// Over `rows`, not `items`: anything already dropped above for a
				// full Warehouse is in out_lost already, and a second entry for
				// the same Serial is precisely the double-exit Reconciliation
				// would report as an anomaly. The text lines follow the same set,
				// which they previously did not - the count said `rows` and the
				// lines listed `items`.
				hb::logger::error("[TP] deliver(offline): bank insert failed for {} - {} item(s) lost",
					character_name, static_cast<int>(rows.size()));
				for (const auto& r : rows) {
					out_lost.push_back(r.serial);
					hb::logger::error("[TP]   lost item id {}", static_cast<int>(r.item_id));
				}
				scope.fail();
				all_ok = false;
			}
		}

		return all_ok;
	}

	bool trading_post_store::refund_offer(int64_t offer_id, int log_action)
	{
		if (!is_open()) {
			return false;
		}

		custody_scope scope(m_db);
		const bool refunded = refund_offer(offer_id, log_action, scope);
		return scope.finish() && refunded;
	}

	bool trading_post_store::refund_offer(int64_t offer_id, int log_action, custody_scope& scope)
	{
		if (!scope.ok()) {
			return false;
		}

		std::string offerer;
		int64_t listing_id = 0;
		std::vector<escrow_item> items;
		if (!load_offer(offer_id, offerer, items, &listing_id)) {
			return false; // already gone (rescinded, finalized, or a race loser)
		}

		// The escrow rows go and the items arrive in ONE transaction (offer_items
		// cascade off the offers row). Until v9 the delete had to be committed
		// BEFORE the delivery, on its own, so that a crash in between resolved to
		// loss rather than duplication — the two halves lived in different files
		// and no ordering could make them one commit.
		if (!delete_offer_row(offer_id)) {
			hb::logger::error("[TP] refund: failed to delete offer {}", offer_id);
			scope.fail();
			return false;
		}

		const bool delivered = deliver_to_bank(offerer.c_str(), items,
			{ log_action, nullptr, listing_id, offer_id }, scope);

		// Deferred with the ledger events, and for the same reason: a trade-log
		// line describing a refund that rolled back is a line an operator will
		// chase for an hour.
		const std::vector<escrow_item> refunded = items;
		scope.on_commit([this, log_action, offer_id, offerer, refunded]()
		{
			for (const auto& e : refunded) {
				hb::logger::log<log_channel::trade>("[TP] {} offer {} -> {} <- {}",
					tp_action_name(log_action), offer_id, offerer, describe(e));
			}
		});
		return delivered;
	}

	int trading_post_store::refund_all_offers_on_listing(int64_t listing_id, custody_scope& scope)
	{
		if (!is_open() || !scope.ok()) {
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
			if (refund_offer(id, ItemLogAction::TpRefund, scope)) {
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
			"SELECT item_id, serial, count, touch_effect_type, touch_effect_value1,"
			" touch_effect_value2, touch_effect_value3, item_color, spec_effect_value1,"
			" spec_effect_value2, spec_effect_value3, cur_durability,"
			HB_ITEM_ATTR_COLUMNS_SQL
			" FROM listing_items WHERE listing_id = ? ORDER BY slot;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, listing_id);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			escrow_item e;
			int c = 0;
			e.item_id = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.serial = sqlite3_column_int64(stmt, c++);
			e.count = static_cast<uint64_t>(sqlite3_column_int64(stmt, c++));
			e.touch_effect_type = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.touch_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.item_color = static_cast<uint8_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value1 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value2 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.spec_effect_value3 = static_cast<int16_t>(sqlite3_column_int(stmt, c++));
			e.cur_durability = static_cast<uint16_t>(sqlite3_column_int(stmt, c++));
			e.attributes = ReadItemAttributeColumns(stmt, c);

			if (!ItemAttributesLoadOk(e.attributes,
				std::format("listing_items listing {} item {}", listing_id, e.item_id))) {
				sqlite3_finalize(stmt);
				out.clear();
				return false;
			}
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

	bool trading_post_store::delete_escrow_row(const char* sql, int64_t id)
	{
		// No transaction of its own: these run inside the operation's scope, so
		// the row leaving the board and the items arriving somewhere else are one
		// commit. Before v9 each was its own BEGIN/COMMIT, which is what made an
		// escrow-out two commits no matter how the caller was written.
		bool ok = false;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_int64(stmt, 1, id);
			ok = (sqlite3_step(stmt) == SQLITE_DONE);
			sqlite3_finalize(stmt);
		}
		return ok;
	}

	bool trading_post_store::delete_listing_row(int64_t listing_id)
	{
		return delete_escrow_row("DELETE FROM listings WHERE listing_id = ?;", listing_id);
	}

	bool trading_post_store::delete_offer_row(int64_t offer_id)
	{
		return delete_escrow_row("DELETE FROM offers WHERE offer_id = ?;", offer_id);
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

		// ONE transaction for the whole Trade (#82). A finalized Trade moves four
		// bundles between three or more characters, and before v9 that was five
		// or more commits across two database files: a crash between any pair
		// left the Listing gone and one side of the trade undelivered. ADR 0001's
		// ordering — delete and commit the escrow row BEFORE delivering — was the
		// best a two-file layout allowed, and it bought loss instead of
		// duplication. One file buys neither.
		custody_scope scope(m_db);

		// 1) Remove the winning Offer so refund_all leaves it alone.
		if (!scope.ok() || !delete_offer_row(offer_id)) {
			scope.fail();
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}
		// 2) Refund the losing Offers to their offerers.
		refund_all_offers_on_listing(listing_id, scope);
		// 3) Remove the Listing (cascades its items; all Offers are gone by now).
		if (!delete_listing_row(listing_id)) {
			scope.fail();
		}

		// 4) Deliver from the in-memory snapshots. These are the two events that
		// carry a counterparty, and the only two: a finalized Trade is the one
		// escrow transition where an item genuinely passed from one character to
		// another, so this is where "who did I get this from" becomes answerable
		// months later.
		const bool to_seller = deliver_to_bank(seller_name.c_str(), winning_items,
			{ ItemLogAction::TpTradeOut, winner_name.c_str(), listing_id, offer_id }, scope);
		const bool to_winner = deliver_to_bank(winner_name.c_str(), listing_items,
			{ ItemLogAction::TpTradeIn, seller_name.c_str(), listing_id, offer_id }, scope);

		scope.on_commit([this, listing_id, offer_id, seller_name, winner_name,
			winning_items, listing_items]()
		{
			for (const auto& e : winning_items) {
				hb::logger::log<log_channel::trade>("[TP] {} listing {} offer {} -> {} <- {}",
					tp_action_name(ItemLogAction::TpTradeOut), listing_id, offer_id, seller_name, describe(e));
			}
			for (const auto& e : listing_items) {
				hb::logger::log<log_channel::trade>("[TP] {} listing {} offer {} -> {} <- {}",
					tp_action_name(ItemLogAction::TpTradeIn), listing_id, offer_id, winner_name, describe(e));
			}
		});

		if (!scope.finish()) {
			// Nothing moved and the board is as it was, so the Listing is still
			// there to try again — which is why this reports Failed rather than
			// one of the "it is gone" codes.
			hb::logger::error("[TP] FINALIZE FAILED for listing {} offer {} - rolled back",
				listing_id, offer_id);
			out_result = hb::net::TpResultCode::Failed;
			return false;
		}

		// 5) Notices, after the commit: telling a winner their items are waiting
		// is only true once the rows say so, and notify_or_queue writes a row of
		// its own for an offline recipient.
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

		// ONE transaction: every Offer returns to its offerer and the bundle
		// returns to the Seller, or none of it does and the Listing stands.
		custody_scope scope(m_db);
		refund_all_offers_on_listing(listing_id, scope);
		if (!delete_listing_row(listing_id)) {
			scope.fail();
		}
		const bool returned = deliver_to_bank(seller_name.c_str(), listing_items,
			{ ItemLogAction::TpDelist, nullptr, listing_id, 0 }, scope);

		scope.on_commit([this, listing_id, seller_name, listing_items]()
		{
			for (const auto& e : listing_items) {
				hb::logger::log<log_channel::trade>("[TP] {} listing {} -> {} <- {}",
					tp_action_name(ItemLogAction::TpDelist), listing_id, seller_name, describe(e));
			}
		});

		if (!scope.finish()) {
			hb::logger::error("[TP] DELIST FAILED for listing {} - rolled back", listing_id);
			out_result = hb::net::TpResultCode::Failed;
			return false;
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

		// ONE transaction for the whole void (#82). This touches every character
		// who had an Offer on one of the deleted character's Listings, so it is
		// the widest multi-account operation the Trading Post has — and half of
		// it committing would refund some offerers and strand the rest against a
		// Listing whose owner no longer exists.
		custody_scope scope(m_db);
		std::vector<std::string> to_notify;

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
			refund_all_offers_on_listing(lid, scope);

			std::vector<escrow_item> listing_items;
			load_listing_items(lid, listing_items);
			if (!delete_listing_row(lid)) {
				scope.fail();
			}
			// Never delivered and never returned — the escrowed bundle shares the
			// fate of the inventory it was pulled from, so this is an exit, not a
			// custody move, and it is the last thing the ledger will ever say
			// about these Serials. Deferred with everything else: a Serial
			// declared dead by a rolled-back void is still on the board.
			const std::string owner(character_name);
			scope.on_commit([this, owner, lid, listing_items]()
			{
				record_escrow_exit(listing_items,
					hb::server::destroy_reason::character_deleted, owner.c_str(), nullptr);
				for (const auto& e : listing_items) {
					hb::logger::log<log_channel::trade>("[TP] void: destroyed {}'s listing {} item <- {}",
						owner, lid, describe(e));
				}
			});
			to_notify.insert(to_notify.end(), offerers.begin(), offerers.end());
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
			if (!delete_offer_row(oid)) {
				scope.fail();
			}
			const std::string owner(character_name);
			scope.on_commit([this, owner, oid, offer_items]()
			{
				record_escrow_exit(offer_items,
					hb::server::destroy_reason::character_deleted, owner.c_str(), nullptr);
				for (const auto& e : offer_items) {
					hb::logger::log<log_channel::trade>("[TP] void: destroyed {}'s offer {} item <- {}",
						owner, oid, describe(e));
				}
			});
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

		if (!scope.finish()) {
			hb::logger::error("[TP] void_character('{}') FAILED - rolled back; the character's "
				"escrowed items are still on the board", character_name);
			return;
		}

		// After the commit: an offline offerer's notice is a row of its own, and
		// queueing it inside the transaction would have it rolled back with
		// everything else while the refund it describes had not happened either.
		for (const auto& nm : to_notify) {
			notify_or_queue(nm.c_str(),
				"A Listing you had an Offer on was removed. Your items were returned to your Warehouse.");
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
