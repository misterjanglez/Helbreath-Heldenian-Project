#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "GameDatabase.h"     // txn_scope, the boundary a custody move commits in
#include "Item/ItemAttributeData.h"
#include "ItemProvenance.h"   // destroy_reason, for the escrow-exit recorders

struct sqlite3;
class CClient;
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
	// The attribute portion is the shared POD, stored as the 15 flat
	// attribute columns.
	struct escrow_item
	{
		int16_t  item_id             = 0;
		// The Serial the escrowed item carries (ADR 0003). Escrow is a custody
		// transfer, not a destruction and re-creation, so the item that comes
		// back out of a Listing has to be the same instance that went in — a
		// Serial dropped here would read to Reconciliation as one item vanishing
		// and an unrelated one appearing. 0 for Counted (stackable) items.
		int64_t  serial              = 0;
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
		hb::shared::item::item_attribute_data attributes;
	};

	// Which escrow transition a delivery is, for the Provenance Ledger (#80).
	//
	// A parameter of deliver_to_bank rather than something emitted beside it:
	// that function is this store's single escrow-out door, so making the
	// transition mandatory to name is what stops the next one ever being
	// delivered without being recorded. There is no "unspecified" — an item
	// leaving escrow is always one of the seven Tp* transitions, and a default
	// would just be somewhere for a future caller to hide.
	struct escrow_move
	{
		int         action            = 0;        // ItemLogAction::Tp*
		const char* counterparty_char = nullptr;  // only a finalized Trade has one
		int64_t     listing_id        = 0;
		int64_t     offer_id          = 0;        // 0 when the move is not about an Offer
	};

	// One Listing summarized for a board browse row: identity + a compact preview
	// of the escrowed bundle. The handler renders names from the item's
	// name-affecting instance fields (modifiers/enchant/custom/dye).
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

	// One custody operation's atomic boundary (#82, plan P3.4).
	//
	// A Trading Post move is three things that have to agree: rows in game.db,
	// objects in memory, and events in the Provenance Ledger. Only the first can
	// be inside a SQLite transaction — memory has no rollback, and the ledger is
	// a separate append-only database on purpose (ADR 0003) — so this scope is
	// what keeps the other two honest about what the first one actually did:
	//
	//   * `on_rollback` collects the memory changes made along the way, unwound
	//     in reverse if the transaction does not commit. Without it a rewound
	//     escrow-out would leave the item in a Warehouse AND back on the board,
	//     which is the duplication every ordering rule in ADR 0001 exists to
	//     avoid.
	//   * `on_commit` holds the ledger events and trade-log lines, written only
	//     once the rows are durable. An append-only log cannot take a row back,
	//     so a custody move recorded and then rolled back would name the wrong
	//     holder for as long as the ledger exists.
	//
	// One scope per player-visible operation. A finalized Trade moves four
	// bundles between three characters and is one scope, so it is one commit;
	// the expiry sweep delists many Listings and gives each its own, because
	// they are unrelated operations that happen to run in the same tick.
	class custody_scope
	{
	public:
		explicit custody_scope(sqlite3* db) : m_txn(db), m_ok(db != nullptr && m_txn.active()) {}

		// Rolls back and unwinds unless finish() already ran, so an early return
		// anywhere inside an operation cannot leave half of it standing.
		~custody_scope() { finish(); }

		custody_scope(const custody_scope&) = delete;
		custody_scope& operator=(const custody_scope&) = delete;

		// False when the transaction could not even begin, or once something has
		// failed. Every step checks it, so a doomed operation stops doing work
		// rather than piling more onto a rollback.
		bool ok() const { return m_ok; }

		// Give up on the operation. Idempotent, and deliberately one-way: an
		// operation that failed halfway cannot be talked back into committing.
		void fail() { m_ok = false; }

		void on_rollback(std::function<void()> undo) { m_undo.push_back(std::move(undo)); }
		void on_commit(std::function<void()> emit) { m_emit.push_back(std::move(emit)); }

		// Commit and emit, or roll back and unwind. Returns whether it committed.
		bool finish();

	private:
		hb::server::txn_scope m_txn;
		bool m_ok;
		bool m_finished = false;
		std::vector<std::function<void()>> m_undo;
		std::vector<std::function<void()>> m_emit;
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
	// Since v9 (#82, plan P3.4) this store does NOT own a database. Its five
	// tables live in `game.db` beside the character tables, and it borrows that
	// connection — because the thing an escrow move has to be atomic WITH is a
	// character save, and two files cannot share a transaction under WAL.
	//
	// The Trading Post is the owner of record for escrowed items; characters
	// never hold them while listed (see docs/adr/0001-trading-post-physical-escrow.md).
	// Until v9 a transition crossed two stores, so it could not be one SQLite
	// transaction and every operation was instead ORDERED to fail as loss rather
	// than duplication:
	//   escrow-in : remove from inventory + forced save BEFORE the escrow insert
	//   escrow-out: delete the escrow row + commit BEFORE delivery
	// That ordering is now a fallback rather than the guarantee: each transition
	// is one transaction, so a failure rewinds to before it started instead of
	// resolving to a designed loss. The loss paths remain and remain recorded —
	// they are what a transaction that cannot even begin still resolves to.
	// Every transition is logged to the trade channel for manual GM recovery.
	//
	// Every transition ALSO appends a Provenance Ledger event (#80, plan P3.2).
	// The two sinks are the pair #78 established everywhere else — the channel is
	// for an operator tailing a file, the ledger is for a query run months later
	// — and escrow is the transition that needs the second one most: it is the
	// only place in the economy where an item changes hands between two accounts
	// without either of them ever holding it at the same time. See
	// record_escrow_event for what an escrowed item's Serial does in the meantime.
	class trading_post_store
	{
	public:
		trading_post_store() = default;
		~trading_post_store();

		trading_post_store(const trading_post_store&) = delete;
		trading_post_store& operator=(const trading_post_store&) = delete;

		void set_game(CGame* game) { m_game = game; }

		// Borrow the connection the escrow tables live on. `db` is the live Game
		// DB for the running server and a scratch file for the provers; either
		// way the caller has already put the v9 schema on it. Call once at
		// startup, after EnsureGameDatabase().
		bool open(sqlite3* db);

		// Drops the borrowed pointer. Does NOT close the connection — this store
		// does not own it.
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

		// --- Refunds --------------------------------------------------------
		// Return a single Offer's items to its offerer: read the items, delete
		// the offer row, and deliver — one transaction (#82). log_action selects
		// the trade log verb (TpRefund for an involuntary return, TpRescind when
		// the offerer withdrew). Returns false if the Offer is gone, the
		// delivery failed, or the transaction rolled back.
		bool refund_offer(int64_t offer_id, int log_action);

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
		// --- Escrow-out -----------------------------------------------------
		// The single door out of escrow. Deliver items to a character's
		// Warehouse (bank): online recipients get an in-memory bank add plus a
		// save; offline recipients get a direct insert at max(slot)+1, bounded
		// only by the hard MaxBankItems cap (the soft 200 cap is intentionally
		// ignored). Returns false if any item could not be placed; every such
		// loss is logged for GM recovery.
		//
		// The caller deletes the backing escrow rows inside `scope`, and this
		// delivers inside the same one, so the row leaving the board and the
		// items arriving are one commit. Until v9 the caller had to have
		// COMMITTED the deletion first, because the two lived in different files
		// and a crash between them had to resolve to loss rather than a dupe.
		//
		// This is also where escrow-out reaches the Provenance Ledger (#80).
		// `move` says which transition the delivery is, and the recording is
		// this function's job rather than each caller's for the reason #78
		// turned item_log() into a dual sink: coverage that depends on every
		// call site remembering is coverage with a hole in it the day someone
		// adds the eighth one. Recorded here, a transition cannot be delivered
		// without being recorded, and an item that fails to land gets its exit
		// event in the same place — so the pair can never come apart. Both go
		// through `scope.on_commit`, because the ledger is append-only and a
		// custody move it recorded for a transaction that rewound is a wrong
		// holder it can never take back.
		bool deliver_to_bank(const char* character_name,
			const std::vector<escrow_item>& items, const escrow_move& move,
			custody_scope& scope);

		// The two composed refunds, inside a caller's operation: one Offer, or
		// every Offer on a Listing (used by delist, finalize and the
		// character-delete void). refund_all returns how many it refunded.
		bool refund_offer(int64_t offer_id, int log_action, custody_scope& scope);
		int refund_all_offers_on_listing(int64_t listing_id, custody_scope& scope);

		// Validate + remove the requested inventory slots from an online
		// character and snapshot them. On failure nothing is removed. max_items
		// is the per-bundle cap (Listing/Offer). The caller saves the character
		// inside its transaction — see the note in the definition for why this
		// no longer forces a save of its own.
		bool pull_items_from_inventory(int client_h,
			const hb::net::TpEscrowSlot* slots, int count, int max_items,
			std::vector<escrow_item>& out_items, uint16_t& out_result);

		// The undo half of pull_items_from_inventory: rebuild the bundle from its
		// snapshots and hand it back. Registered on a custody_scope's rollback,
		// so an escrow-in that could not commit leaves the character holding what
		// they held before it — which is what turns ADR 0001's designed loss into
		// nothing having happened.
		void return_items_to_inventory(int client_h, const std::vector<escrow_item>& items);

		// The undo half of an online delivery: take back out of the Warehouse
		// exactly the objects this operation put in, by pointer identity.
		void remove_from_bank(int client_h, const std::vector<CItem*>& items);

		// Persist one online character on THIS store's connection, inside the
		// enclosing transaction. Not g_login->local_save_player_data, which would
		// use the live handle and open a scope of its own.
		bool save_character(int client_h) const;

		// Insert a bundle's item rows into listing_items or offer_items within
		// an already-open transaction. id_column is "listing_id" or "offer_id".
		bool insert_item_rows(const char* table, const char* id_column,
			int64_t owner_id, const std::vector<escrow_item>& items);

		// Load an Offer's offerer name and items. Returns false if it is gone.
		//
		// `out_listing_id` is optional because only the refund path needs it: the
		// ledger event an Offer produces carries its Listing — the thread that
		// ties a whole trade, both bundles and every offerer, into one query —
		// and it is already in the row this reads, so it costs no second query.
		// The other callers either know it or do not care.
		bool load_offer(int64_t offer_id, std::string& out_offerer,
			std::vector<escrow_item>& out_items, int64_t* out_listing_id = nullptr);

		// --- Provenance Ledger (#80, plan P3.2) -----------------------------
		// One event per Instanced item in a bundle that just changed custody.
		//
		// Escrow is the one custody transfer with no CItem to hang an event off:
		// the object is destroyed when the item is pulled from inventory and
		// rebuilt only when it is delivered, so between those moments the Serial
		// carried on the escrow row (added by #77 for exactly this) is the item's
		// entire identity. That is why these go through ItemManager's by-Serial
		// recorders rather than through item_log().
		//
		// `move.counterparty_char` is set only where the item genuinely passed
		// between two characters — the two halves of a finalized Trade. Listing,
		// delisting, offering and rescinding are one character and the board, and
		// naming a counterparty there would invent a transfer that did not
		// happen.
		//
		// `actor` is the recorded character's client when they are online and
		// null when they are not; the caller has already resolved it, and an
		// offline actor is an ordinary case here rather than an error.
		void record_escrow_event(const escrow_move& move,
			const std::vector<escrow_item>& items,
			const char* actor_char, const CClient* actor);

		// An escrowed item that is not coming back out. Two reasons reach this:
		// `delivery_lost`, where a transfer failed with the item already out of
		// its owner's hands (ADR 0001 orders every escrow step to fail as loss
		// rather than duplication, so these are the deliberate cost of that
		// choice), and `character_deleted`, where nothing failed at all. Spelling
		// the reason at the call site rather than burying it behind two function
		// names keeps the most-read column of a destruction visible where it is
		// decided.
		//
		// An audit trail that recorded the custody move and then stayed silent
		// about the exit would name the wrong holder forever, which is the whole
		// reason these exist. The owner is recorded as the actor: it was their
		// item, whether they were sending it or receiving it.
		void record_escrow_exit(const std::vector<int64_t>& serials,
			hb::server::destroy_reason::destroy_reason reason,
			const char* owner_char, const CClient* owner);
		void record_escrow_exit(const std::vector<escrow_item>& items,
			hb::server::destroy_reason::destroy_reason reason,
			const char* owner_char, const CClient* owner);

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
		// single Offer (cascades offer_items). Each runs inside the caller's
		// custody_scope — they had a committed transaction each before v9, which
		// is what made every escrow-out at least two commits.
		bool delete_escrow_row(const char* sql, int64_t id);
		bool delete_listing_row(int64_t listing_id);
		bool delete_offer_row(int64_t offer_id);

		// Shared delist body. Seller-initiated (expired=false) and expiry-initiated
		// (expired=true) differ only in the notice wording.
		bool do_delist(int64_t listing_id, bool expired, uint16_t& out_result);

		// Insert one notices row (offline notice).
		void queue_notice(const char* character_name, const std::string& message);

		// The two delivery mechanics. Neither records anything: each reports the
		// Serials that did NOT land in `out_lost` and lets deliver_to_bank emit
		// the exits, so a branch added to either of them cannot forget to — and
		// cannot double-record one the other already covered.
		//
		// The one exception is a full Warehouse on the online path, which is the
		// only failure holding a live CItem. That goes through destroy_item, the
		// destruction funnel every object exit in the server goes through
		// (Scripts/check_item_destroy.py), so it is already recorded and is
		// deliberately absent from `out_lost`.
		//
		// A per-item failure does NOT fail the scope: the item is gone and said
		// so, and rewinding the operation around it would restore an escrow row
		// for an object destroy_item has already ended. What fails the scope is
		// the database refusing — a character that will not save, a bank insert
		// that will not go in — because that is the case where rewinding leaves
		// the items safely back on the board to be tried again.
		bool deliver_to_online(int client_h, const std::vector<escrow_item>& items,
			std::vector<int64_t>& out_lost, custody_scope& scope);
		bool deliver_to_offline(const char* character_name,
			const std::vector<escrow_item>& items, std::vector<int64_t>& out_lost,
			custody_scope& scope);

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
