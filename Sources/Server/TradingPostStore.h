#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
class CGame;
class CItem;

namespace hb::net { struct TpEscrowSlot; }

namespace hb::server
{
	// One escrowed item's full instance column set. Mirrors the
	// character_bank_items schema / item_instance_data (and the TpItemFull wire
	// struct), so the same record moves losslessly between a live CItem,
	// tradingpost.db, and an account DB. This is the currency of every escrow
	// transition: pulled out of inventory on escrow-in, stored as a
	// listing/offer item, and handed back to deliver_to_bank on escrow-out.
	struct escrow_item
	{
		int16_t  item_id             = 0;
		uint64_t count               = 0;
		int16_t  touch_effect_type   = 0;
		int16_t  touch_effect_value1 = 0;
		int16_t  touch_effect_value2 = 0;
		int16_t  touch_effect_value3 = 0;
		uint8_t  item_color          = 0;
		int16_t  spec_effect_value1  = 0;
		int16_t  spec_effect_value2  = 0;
		int16_t  spec_effect_value3  = 0;
		uint16_t cur_durability      = 0;
		uint8_t  custom_made         = 0;
		uint8_t  prefix_type         = 0;
		uint8_t  prefix_value        = 0;
		uint8_t  secondary_type      = 0;
		uint8_t  secondary_value     = 0;
		uint8_t  enchant_bonus       = 0;
	};

	// One Listing summarized for a board browse row: identity + a compact preview
	// of the escrowed bundle. The handler renders names from the item's
	// name-affecting instance fields (prefix/secondary/enchant/custom/dye).
	struct listing_brief
	{
		int64_t                  listing_id    = 0;
		std::string              seller_name;
		int                      seller_nation = 0;
		int                      offer_count   = 0;
		int64_t                  expires_at    = 0;   // unix seconds
		std::string              seeking_note;
		std::vector<escrow_item> items;
	};

	// One Offer as shown in a Listing's detail view.
	struct offer_view
	{
		int64_t                  offer_id      = 0;
		std::string              offerer_name;
		std::vector<escrow_item> items;
	};

	// Full detail for one Listing: the escrowed bundle plus every active Offer.
	struct listing_detail
	{
		int64_t                  listing_id    = 0;
		std::string              seller_name;
		int                      seller_nation = 0;
		std::string              seeking_note;
		int64_t                  expires_at    = 0;   // unix seconds
		std::vector<escrow_item> items;
		std::vector<offer_view>  offers;
	};

	// Server-owned escrow store for the Trading Post.
	//
	// Owns a single persistent SQLite connection to Binaries/Server/tradingpost.db,
	// opened once at startup and held for the server's lifetime. Setup follows the
	// GameConfigSqliteStore pattern (busy_timeout, PRAGMA foreign_keys=ON so the
	// ON DELETE CASCADE relationships fire, an idempotent CREATE TABLE IF NOT
	// EXISTS block, and a meta(key,value) schema_version row).
	//
	// The Trading Post is the owner of record for escrowed items; character DBs
	// never contain them (see docs/adr/0001-trading-post-physical-escrow.md).
	// Because a transition crosses stores (inventory <-> tradingpost.db <->
	// account DBs) it cannot be one SQLite transaction, so every operation is
	// ordered to fail as loss, never duplication:
	//   escrow-in : remove from inventory + forced save BEFORE the escrow insert
	//   escrow-out: delete the escrow row + commit BEFORE delivery
	// Every transition is logged to the trade channel for manual GM recovery.
	class trading_post_store
	{
	public:
		trading_post_store() = default;
		~trading_post_store();

		trading_post_store(const trading_post_store&) = delete;
		trading_post_store& operator=(const trading_post_store&) = delete;

		void set_game(CGame* game) { m_game = game; }

		// Open (creating if absent) tradingpost.db and ensure the schema. Call
		// once at startup. Returns false on connection or schema failure.
		bool open(const std::string& path);
		void close();
		bool is_open() const { return m_db != nullptr; }
		sqlite3* handle() const { return m_db; }

		// --- Escrow-in ------------------------------------------------------
		// Create a Listing from an online character's inventory. Validates the
		// requested slots/amounts, removes the items from the in-memory
		// inventory (unequipping and decrementing stacks as needed), forces a
		// character save so the reduced inventory is durable, then inserts the
		// listings + listing_items rows in one transaction and logs TpList.
		// On validation failure nothing is removed and out_result carries a
		// hb::net::TpResultCode; on success out_result is Ok and out_listing_id
		// receives the new id. Business-rule checks (<=5 active Listings,
		// proximity, ...) belong to the request handler that calls this.
		bool create_listing(int client_h,
			const char* seeking_note,
			const hb::net::TpEscrowSlot* slots, int count,
			int64_t& out_listing_id, uint16_t& out_result);

		// Place an Offer on a Listing from an online character's inventory. Same
		// escrow-in discipline as create_listing; inserts offers + offer_items
		// and logs TpOffer. Does not itself enforce the <10-offers, one-per-
		// character, or same-account rules (the handler does).
		bool place_offer(int client_h, int32_t listing_id,
			const hb::net::TpEscrowSlot* slots, int count,
			int64_t& out_offer_id, uint16_t& out_result);

		// --- Escrow-out -----------------------------------------------------
		// Deliver items to a character's Warehouse (bank). Online recipients get
		// an in-memory bank add plus a forced save; offline recipients get a
		// direct insert into their account DB at max(slot)+1, bounded only by
		// the hard MaxBankItems cap (the soft 200 cap is intentionally ignored).
		// Callers MUST have already deleted the backing escrow rows and
		// committed before calling this. Returns false if any item could not be
		// placed; every such loss is logged for GM recovery.
		bool deliver_to_bank(const char* character_name,
			const std::vector<escrow_item>& items);

		// --- Refunds (built on deliver_to_bank) -----------------------------
		// Return a single Offer's items to its offerer: read the items, delete
		// the offer row and commit, then deliver. log_action selects the trade
		// log verb (TpRefund for an involuntary return, TpRescind when the
		// offerer withdrew). Returns false if the Offer is gone or delivery
		// failed.
		bool refund_offer(int64_t offer_id, int log_action);

		// Return every Offer on a Listing to its offerers (used by delist and
		// the character-delete void). Returns the number of Offers refunded.
		int refund_all_offers_on_listing(int64_t listing_id);

		// --- Reads (for the request handlers) -------------------------------
		// Number of active Listings owned by a character (the <=5 rule).
		int count_active_listings(const char* seller_name);

		// Number of active Offers on a Listing (the <10 rule).
		int count_offers(int64_t listing_id);

		// Load a Listing's owner identity + expiry. False if the Listing is gone.
		bool get_listing_owner(int64_t listing_id, std::string& out_seller_name,
			std::string& out_seller_account, int64_t& out_expires_at);

		// Load an Offer's owner identity + parent Listing. False if it is gone.
		bool get_offer_owner(int64_t offer_id, std::string& out_offerer_name,
			std::string& out_offerer_account, int64_t& out_listing_id);

		// One page of the board, newest Listings first. out_total_listings is the
		// unpaged Listing count (for page_count). page is 0-based, page_size > 0.
		void get_board_page(int page, int page_size,
			std::vector<listing_brief>& out_rows, int& out_total_listings);

		// One page of the actor's own slice of the board: the Listings they sell
		// (get_my_listings_page) or the Listings they have an Offer on
		// (get_my_offers_page). Same row shape and paging as get_board_page — the
		// board response is reused verbatim. Name match is case-insensitive, like
		// the rest of the Trading Post's identity checks.
		void get_my_listings_page(const char* seller_name, int page, int page_size,
			std::vector<listing_brief>& out_rows, int& out_total_listings);
		void get_my_offers_page(const char* offerer_name, int page, int page_size,
			std::vector<listing_brief>& out_rows, int& out_total_listings);

		// Full detail (bundle + Offers) for one Listing. False if it is gone.
		bool get_listing_detail(int64_t listing_id, listing_detail& out);

		// --- Composed operations (built on the escrow-out primitives) -------
		// Finalize a Trade: the winning Offer's items go to the Seller's Warehouse,
		// the Listing's bundle to the winner's Warehouse, every losing Offer is
		// auto-refunded, and the Listing is removed — one logical operation. State
		// is re-validated at call time (ListingGone / OfferGone on a race). Winner
		// and losers receive notices. out_result carries a hb::net::TpResultCode.
		bool finalize(int64_t listing_id, int64_t offer_id, uint16_t& out_result);

		// Delist a Listing: refund every Offer to its offerer, return the bundle to
		// the Seller, remove the Listing. Offerers receive notices. out_result
		// carries a TpResultCode.
		bool delist(int64_t listing_id, uint16_t& out_result);

		// --- Hooks ----------------------------------------------------------
		// Delist every Listing whose expires_at has passed (returns + refunds +
		// notices). Cheap when the board is empty; call on a periodic tick.
		void sweep_expired();

		// Void a deleted character's Trading Post presence: refund every Offer on
		// the character's Listings to its offerers, destroy the character's own
		// escrowed items (logged, never delivered — same fate as its inventory),
		// then delete its Listings, Offers, and queued notices.
		void void_character(const char* character_name);

		// Deliver a one-line notice: as a system chat line if the recipient is
		// online, else persisted for delivery on their next login.
		void notify_or_queue(const char* character_name, const std::string& message);

		// Flush an online character's queued notices as system chat lines, then
		// delete them. Called from the login hook.
		void flush_notices(int client_h);

	private:
		bool ensure_schema();

		// Validate + remove the requested inventory slots from an online
		// character and snapshot them, then force a character save. On failure
		// nothing is removed. max_items is the per-bundle cap (Listing/Offer).
		bool pull_items_from_inventory(int client_h,
			const hb::net::TpEscrowSlot* slots, int count, int max_items,
			std::vector<escrow_item>& out_items, uint16_t& out_result);

		// Insert a bundle's item rows into listing_items or offer_items within
		// an already-open transaction. id_column is "listing_id" or "offer_id".
		bool insert_item_rows(const char* table, const char* id_column,
			int64_t owner_id, const std::vector<escrow_item>& items);

		// Load an Offer's offerer name and items. Returns false if it is gone.
		bool load_offer(int64_t offer_id, std::string& out_offerer,
			std::vector<escrow_item>& out_items);

		// Load a Listing's escrowed bundle, ordered by slot.
		bool load_listing_items(int64_t listing_id, std::vector<escrow_item>& out);

		// Shared body for the three board queries (public board, my-Listings,
		// my-Offers). count_sql yields the unpaged total; rows_sql yields the paged
		// rows in get_board_page's column order. When name_filter is non-null it is
		// bound to the first parameter of both statements; rows_sql always ends with
		// LIMIT ? OFFSET ? as its last two binds.
		void query_board(const char* count_sql, const char* rows_sql,
			const char* name_filter, int page, int page_size,
			std::vector<listing_brief>& out_rows, int& out_total_listings);

		// Delete a Listing (cascades listing_items + offers + offer_items) or a
		// single Offer (cascades offer_items), each in its own committed txn.
		bool delete_listing_row(int64_t listing_id);
		bool delete_offer_row(int64_t offer_id);

		// Shared delist body. Seller-initiated (expired=false) and expiry-initiated
		// (expired=true) differ only in the notice wording.
		bool do_delist(int64_t listing_id, bool expired, uint16_t& out_result);

		// Insert one notices row (offline notice).
		void queue_notice(const char* character_name, const std::string& message);

		bool deliver_to_online(int client_h, const std::vector<escrow_item>& items);
		bool deliver_to_offline(const char* character_name,
			const std::vector<escrow_item>& items);

		// Rebuild a heap CItem from an escrow_item (config attrs via
		// init_item_attr, then the stored instance columns overlaid). Caller
		// owns the returned pointer; nullptr if the item id is unknown.
		CItem* build_item(const escrow_item& e) const;

		// Human-readable one-item description for trade-log lines.
		std::string describe(const escrow_item& e) const;

		sqlite3* m_db = nullptr;
		CGame* m_game = nullptr;
	};
}
