#pragma once

#include "IDialogBox.h"
#include "Packet/PacketTradingPost.h"
#include "NetConstants.h"
#include <cstdint>
#include <string>

// DialogBox_TradingPost — the Auctioneer (Vince) dialog.
//
// A single view-switched dialog covering the whole Trading Post: the Browse /
// My Listings / My Offers board slices, a Listing Detail view with contextual
// Finalize / Delist / Rescind / Place-Offer actions, and the Create-Listing /
// Create-Offer bundle builders. The board is pull-only — the client re-requests
// the current page or the open detail after each of its own successful actions.
//
// Vocabulary follows CONTEXT.md: Listing, Offer, Finalize, Rescind, Delist,
// seeking note, Warehouse (never "bank" in player-facing text). Item data is
// never trusted from here: create/offer requests carry inventory slot indices +
// amounts only, and the server re-reads the character's item list.
class DialogBox_TradingPost : public IDialogBox
{
public:
	DialogBox_TradingPost(CGame* game);
	~DialogBox_TradingPost() override = default;

	void on_draw() override;
	bool on_click() override;
	bool on_item_drop() override;
	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
	bool on_disable() override;

	// --- Network feed (called from NetworkMessages_TradingPost.cpp) ---
	void receive_board_page(const hb::net::PacketResponseTpBoardPage* pkt);
	void receive_listing_detail(const hb::net::PacketResponseTpListingDetail* pkt);
	void receive_action_result(const hb::net::PacketResponseTpActionResult* pkt);

	// Stage an inventory slot into the open Create bundle. Called directly for
	// single items (from on_item_drop) and from the amount-picker finalize path
	// (Screen_OnGame.cpp drop-target 1003) for stackables. Returns false if the
	// slot could not be staged (wrong view, duplicate, or the bundle is full).
	bool stage_item(int inv_slot, int amount);

private:
	enum class view : uint8_t
	{
		browse         = 0,
		my_listings    = 1,
		my_offers      = 2,
		detail         = 3,
		create_listing = 4,
		create_offer   = 5,
	};

	// One staged bundle entry: an inventory slot index + the amount to escrow.
	struct stage_slot
	{
		int inv_slot = -1;
		int amount = 0;
	};

	// --- Per-view draw ---
	void draw_frame();
	void draw_tabs(int mx, int my);
	void draw_board(int mx, int my);
	void draw_detail(int mx, int my);
	void draw_create(int mx, int my);
	void draw_close_button(int mx, int my);

	// --- Per-view click ---
	bool click_tabs(int mx, int my);
	bool click_board(int mx, int my);
	bool click_detail(int mx, int my);
	bool click_create(int mx, int my);

	// --- Requests (pull-only board) ---
	void request_board(uint16_t page);       // browse / my_listings / my_offers by m_view
	void request_detail(int32_t listing_id);
	void send_create_listing();
	void send_place_offer(int32_t listing_id);
	void send_rescind(int32_t offer_id);
	void send_finalize(int32_t listing_id, int32_t offer_id);
	void send_delist(int32_t listing_id);

	// --- Helpers ---
	void switch_view(view v);                 // ends note input when leaving Create-Listing
	void open_board_view(view v);             // switch_view + request page 0
	void clear_bundle();                      // unlock + drop every staged slot
	void prune_stale_bundle();                // drop staged slots whose item vanished
	int  staged_count() const;
	bool has_own_offer() const;               // an Offer by me on the open Listing detail
	bool is_mine(const char* name) const;     // name == my character name (case-insensitive)
	void draw_item_icon(int px, int py, short item_id, char item_color);
	bool is_board_view(view v) const
	{
		return v == view::browse || v == view::my_listings || v == view::my_offers;
	}

	// --- State ---
	view m_view = view::browse;

	// Board buffer (shared by browse / my_listings / my_offers). m_board_kind is
	// the slice m_board actually holds, set when the request is sent, so a fast
	// tab switch shows "loading" rather than the wrong slice's rows.
	hb::net::PacketResponseTpBoardPage m_board{};
	view m_board_kind = view::browse;
	uint16_t m_page = 0;
	int m_hover_row = -1;

	// Detail buffer.
	hb::net::PacketResponseTpListingDetail m_detail{};
	bool m_has_detail = false;
	int32_t m_detail_listing_id = -1;
	view m_detail_return = view::browse;      // board view [Back] returns to
	int m_offer_scroll = 0;

	// Create staging (Listing + Offer share the grid; both cap at 4 items).
	stage_slot m_bundle[hb::shared::limits::TpMaxListingItems];
	std::string m_note_text;

	// Note text-input drag tracking (restart the field when the box is dragged).
	short m_last_sx = 0;
	short m_last_sy = 0;
};
