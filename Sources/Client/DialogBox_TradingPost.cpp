#include "DialogBox_TradingPost.h"
#include "DialogBox_ItemDropAmount.h"
#include "Game.h"
#include "Player.h"
#include "CursorTarget.h"
#include "InventoryManager.h"
#include "ItemNameFormatter.h"
#include "ItemSpriteMetadata.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "TextInputManager.h"
#include "SpriteID.h"
#include "GlobalDef.h"
#include "NetMessages.h"
#include "lan_eng.h"
#include "IInput.h"
#include "AudioManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <string>

using namespace hb::shared::net;        // MsgId
using namespace hb::net;                // Tp* packets, TpAction, TpResultCode, TpMyBoardFilter
using namespace hb::client::sprite_id;  // item_atlas
using hb::shared::item::EquipPos;
using hb::shared::item::to_int;
using render_color = hb::shared::render::Color;

// ============================================================================
// Layout + palette (dark parchment panel drawn from primitives — no fixed
// background sprite, so the dialog can be sized to the board).
// ============================================================================
namespace tp
{
	constexpr int dlg_w = 472;
	constexpr int dlg_h = 360;
	constexpr int margin = 10;
	constexpr int inner_x = margin;                              // relative to m_x
	constexpr int inner_w = dlg_w - margin * 2;                  // 452

	constexpr int title_y = 8;
	constexpr int tab_y = 30;
	constexpr int tab_h = 20;
	constexpr int tab_gap = 4;
	constexpr int tab_count = 4;
	constexpr int tab_w = (inner_w - tab_gap * (tab_count - 1)) / tab_count;   // 110

	constexpr int content_y = 58;

	// Board rows
	constexpr int row_h = 33;
	constexpr int row_count = hb::shared::limits::TpBoardPageRows;             // 8
	constexpr int page_y = content_y + row_h * row_count + 2;                  // 324
	constexpr int page_btn_w = 72;

	// Item slot (detail bundle + create grid)
	constexpr int slot_size = 46;
	constexpr int slot_gap = 6;
	constexpr int slot_pitch = slot_size + slot_gap;                          // 52

	// Detail view (offsets relative to m_y)
	constexpr int det_header_y = content_y;                                   // 58
	constexpr int det_note_y = content_y + 18;                                // 76
	constexpr int det_bundle_label_y = content_y + 38;                        // 96
	constexpr int det_bundle_y = content_y + 54;                              // 112
	constexpr int det_info_y = content_y + 104;                               // 162
	constexpr int det_offers_label_y = content_y + 134;                       // 192
	constexpr int det_offers_y = content_y + 150;                             // 208
	constexpr int det_offer_row_h = 22;
	constexpr int det_offers_visible = 5;
	constexpr int det_actions_y = det_offers_y + det_offer_row_h * det_offers_visible + 4;  // 322
	constexpr int det_btn_h = 18;

	// Create view (offsets relative to m_y)
	constexpr int cr_hint_y = content_y + 20;                                 // 78
	constexpr int cr_grid_y = content_y + 44;                                 // 102
	constexpr int cr_info_y = content_y + 94;                                 // 152
	constexpr int cr_note_label_y = content_y + 120;                          // 178
	constexpr int cr_note_y = content_y + 138;                                // 196
	constexpr int cr_note_h = 18;
	constexpr int cr_buttons_y = content_y + 176;                             // 234
	constexpr int cr_btn_h = 20;
}

namespace tp_col
{
	const auto panel      = render_color(38, 32, 24, 244);
	const auto panel_edge = render_color(120, 100, 70);
	const auto header     = render_color(214, 192, 132);
	const auto row_bg     = render_color(56, 48, 36, 165);
	const auto row_hover  = render_color(96, 80, 48, 205);
	const auto tab_bg     = render_color(48, 42, 32, 220);
	const auto tab_active = render_color(96, 80, 48, 235);
	const auto tab_edge   = render_color(112, 96, 66);
	const auto text       = GameColors::UINearWhite;
	const auto text_dim   = render_color(176, 166, 142);
	const auto text_hi    = GameColors::UIWhite;
	const auto btn        = render_color(70, 60, 44, 232);
	const auto btn_hover  = render_color(112, 92, 55, 242);
	const auto btn_edge   = render_color(140, 120, 82);
	const auto slot_bg    = render_color(24, 20, 15, 236);
	const auto slot_edge  = render_color(96, 82, 55);
	const auto good       = GameColors::ChatEventGreen;
	const auto mine       = GameColors::UIPaleYellow;
	const auto aresden    = render_color(228, 96, 84);    // Aresden — red
	const auto elvine     = render_color(116, 152, 255);  // Elvine — blue
}

namespace
{
	bool hit(int mx, int my, int x, int y, int w, int h)
	{
		return mx >= x && mx <= x + w && my >= y && my <= y + h;
	}

	// Case-insensitive full-string compare (character-name identity, matching the
	// server's COLLATE NOCASE / hb_strnicmp checks). StringCompat.h is server-only.
	bool iequals(const char* a, const char* b)
	{
		if (a == nullptr || b == nullptr) return false;
		while (*a != '\0' && *b != '\0')
		{
			if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
				return false;
			++a; ++b;
		}
		return *a == *b;
	}

	// Clip a string to n characters, appending "..." when truncated.
	std::string fit(const std::string& s, size_t n)
	{
		if (s.size() <= n) return s;
		if (n <= 3) return s.substr(0, n);
		return s.substr(0, n - 3) + "...";
	}

	std::string fmt_expiry(uint16_t hours)
	{
		if (hours == 0) return "expiring";
		int d = hours / 24;
		int h = hours % 24;
		if (d > 0) return std::format("{}d {}h", d, h);
		return std::format("{}h", h);
	}

	// Seller nation is the character's side (0 = Neutral, 1 = Aresden, 2 = Elvine).
	const char* nation_name(uint8_t n)
	{
		switch (n)
		{
		case 1:  return "Aresden";
		case 2:  return "Elvine";
		default: return "Neutral";
		}
	}

	render_color nation_color(uint8_t n)
	{
		switch (n)
		{
		case 1:  return tp_col::aresden;
		case 2:  return tp_col::elvine;
		default: return tp_col::text_dim;
		}
	}

	// A parchment-toned button; returns true when hovered (only if enabled).
	bool draw_button(CGame* game, int x, int y, int w, int h, const char* label,
		int mx, int my, bool enabled = true)
	{
		bool hover = enabled && hit(mx, my, x, y, w, h);
		game->m_Renderer->draw_rect_filled(x, y, w, h, hover ? tp_col::btn_hover : tp_col::btn);
		game->m_Renderer->draw_rect_outline(x, y, w, h, tp_col::btn_edge, 1);
		auto color = !enabled ? tp_col::text_dim : (hover ? tp_col::text_hi : tp_col::text);
		hb::shared::text::draw_text_aligned(GameFont::Default, x, y, w, h, label,
			hb::shared::text::TextStyle::from_color(color), hb::shared::text::Align::Center);
		return hover;
	}

	// Toast string for every action/result-code (see lan_eng.h TP_TOAST_*).
	const char* tp_result_toast(uint16_t action, uint16_t code)
	{
		if (code == TpResultCode::Ok)
		{
			switch (action)
			{
			case TpAction::CreateListing: return TP_TOAST_LISTED;
			case TpAction::PlaceOffer:    return TP_TOAST_OFFERED;
			case TpAction::RescindOffer:  return TP_TOAST_RESCINDED;
			case TpAction::Finalize:      return TP_TOAST_FINALIZED;
			case TpAction::Delist:        return TP_TOAST_DELISTED;
			default:                      return TP_TOAST_OK;
			}
		}
		switch (code)
		{
		case TpResultCode::TooManyListings:   return TP_TOAST_TOO_MANY_LISTINGS;
		case TpResultCode::ListingGone:       return TP_TOAST_LISTING_GONE;
		case TpResultCode::OfferGone:         return TP_TOAST_OFFER_GONE;
		case TpResultCode::AccountSelfTrade:  return TP_TOAST_SELF_TRADE;
		case TpResultCode::TooManyOffers:     return TP_TOAST_TOO_MANY_OFFERS;
		case TpResultCode::AlreadyOffered:    return TP_TOAST_ALREADY_OFFERED;
		case TpResultCode::InventoryChanged:  return TP_TOAST_INV_CHANGED;
		case TpResultCode::InvalidBundle:     return TP_TOAST_INVALID_BUNDLE;
		case TpResultCode::InvalidNote:       return TP_TOAST_INVALID_NOTE;
		case TpResultCode::NotSeller:         return TP_TOAST_NOT_SELLER;
		case TpResultCode::NotNearAuctioneer: return TP_TOAST_NOT_NEAR;
		case TpResultCode::WarehouseFull:     return TP_TOAST_WAREHOUSE_FULL;
		case TpResultCode::Busy:              return TP_TOAST_BUSY;
		default:                              return TP_TOAST_FAILED;
		}
	}
}

// ============================================================================
// Construction / lifecycle
// ============================================================================
DialogBox_TradingPost::DialogBox_TradingPost(CGame* game)
	: IDialogBox(DialogBoxId::TradingPost, game)
{
	set_default_rect(0, 0, tp::dlg_w, tp::dlg_h);
	m_can_close_on_right_click = true;
}

bool DialogBox_TradingPost::on_enable(int type, int64_t v1, int v2, const char* string)
{
	if (is_enabled()) return true;
	m_x = (LOGICAL_WIDTH() - tp::dlg_w) / 2;
	m_y = (LOGICAL_HEIGHT() - tp::dlg_h) / 2 - 20;
	if (m_x < 0) m_x = 0;
	if (m_y < 0) m_y = 0;

	m_view = view::browse;
	m_board_kind = view::browse;
	m_page = 0;
	m_hover_row = -1;
	m_has_detail = false;
	m_detail_listing_id = -1;
	m_detail_return = view::browse;
	m_offer_scroll = 0;
	for (auto& b : m_bundle) { b.inv_slot = -1; b.amount = 0; }
	m_note_text.clear();

	request_board(0);
	return true;
}

bool DialogBox_TradingPost::on_disable()
{
	text_input_manager::get().end_input();
	clear_bundle();
	m_has_detail = false;
	return true;
}

// ============================================================================
// Network feed
// ============================================================================
void DialogBox_TradingPost::receive_board_page(const hb::net::PacketResponseTpBoardPage* pkt)
{
	if (pkt == nullptr) return;
	m_board = *pkt;
	m_page = pkt->page;
	if (m_hover_row >= static_cast<int>(m_board.row_count)) m_hover_row = -1;
}

void DialogBox_TradingPost::receive_listing_detail(const hb::net::PacketResponseTpListingDetail* pkt)
{
	if (pkt == nullptr) return;
	m_detail = *pkt;
	m_has_detail = true;
	m_detail_listing_id = pkt->listing_id;
	int max_scroll = std::max(0, static_cast<int>(m_detail.offer_count) - tp::det_offers_visible);
	m_offer_scroll = std::clamp(m_offer_scroll, 0, max_scroll);
}

void DialogBox_TradingPost::receive_action_result(const hb::net::PacketResponseTpActionResult* pkt)
{
	if (pkt == nullptr) return;
	add_event_list(tp_result_toast(pkt->action, pkt->result_code), 10);

	if (pkt->result_code == TpResultCode::Ok)
	{
		switch (pkt->action)
		{
		case TpAction::CreateListing:
			clear_bundle();
			open_board_view(view::my_listings);   // show the new Listing
			break;
		case TpAction::PlaceOffer:
			clear_bundle();
			if (m_detail_listing_id >= 0)
			{
				switch_view(view::detail);
				request_detail(m_detail_listing_id);  // now shows my Offer
			}
			else
			{
				open_board_view(view::browse);
			}
			break;
		case TpAction::Finalize:
		case TpAction::Delist:
			m_has_detail = false;                    // the Listing is gone
			open_board_view(m_detail_return);
			break;
		case TpAction::RescindOffer:
			if (m_has_detail && m_detail_listing_id >= 0)
				request_detail(m_detail_listing_id);  // the Offer left the list
			else
				open_board_view(m_detail_return);
			break;
		default:
			break;
		}
	}
	else
	{
		// Failures on detail actions may mean the board moved under us — resync the
		// open detail. Create failures keep the staged bundle so the user can retry.
		if ((pkt->action == TpAction::Finalize || pkt->action == TpAction::Delist
			|| pkt->action == TpAction::RescindOffer)
			&& m_has_detail && m_detail_listing_id >= 0)
		{
			request_detail(m_detail_listing_id);
		}
	}
}

// ============================================================================
// Requests (pull-only board)
// ============================================================================
void DialogBox_TradingPost::request_board(uint16_t page)
{
	m_page = page;
	m_board_kind = is_board_view(m_view) ? m_view : view::browse;

	if (m_view == view::my_listings || m_view == view::my_offers)
	{
		hb::net::PacketRequestTpMyBoard req{};
		req.header.msg_id = MsgId::RequestTpMyBoard;
		req.page = page;
		req.which = (m_view == view::my_offers)
			? static_cast<uint8_t>(TpMyBoardFilter::MyOffers)
			: static_cast<uint8_t>(TpMyBoardFilter::MyListings);
		send_game_packet(req);
	}
	else
	{
		hb::net::PacketRequestTpBoardPage req{};
		req.header.msg_id = MsgId::RequestTpBoardPage;
		req.page = page;
		send_game_packet(req);
	}
}

void DialogBox_TradingPost::request_detail(int32_t listing_id)
{
	m_detail_listing_id = listing_id;
	hb::net::PacketRequestTpListingDetail req{};
	req.header.msg_id = MsgId::RequestTpListingDetail;
	req.listing_id = listing_id;
	send_game_packet(req);
}

void DialogBox_TradingPost::send_create_listing()
{
	hb::net::PacketRequestTpCreateListing req{};
	req.header.msg_id = MsgId::RequestTpCreateListing;
	std::snprintf(req.note, sizeof(req.note), "%s", m_note_text.c_str());
	int n = 0;
	for (auto& b : m_bundle)
	{
		if (b.inv_slot < 0) continue;
		req.items[n].inv_slot = static_cast<uint8_t>(b.inv_slot);
		req.items[n].amount = b.amount;
		n++;
	}
	req.item_count = static_cast<uint8_t>(n);
	if (n < 1) { add_event_list(TP_TOAST_NEED_ITEMS, 10); return; }
	send_game_packet(req);
}

void DialogBox_TradingPost::send_place_offer(int32_t listing_id)
{
	// Cheap client-side mirror: no Offer on my own Listing (server also blocks the
	// whole account). The server stays authoritative.
	if (m_has_detail && is_mine(m_detail.seller)) { add_event_list(TP_TOAST_SELF_TRADE, 10); return; }

	hb::net::PacketRequestTpPlaceOffer req{};
	req.header.msg_id = MsgId::RequestTpPlaceOffer;
	req.listing_id = listing_id;
	int n = 0;
	for (auto& b : m_bundle)
	{
		if (b.inv_slot < 0) continue;
		req.items[n].inv_slot = static_cast<uint8_t>(b.inv_slot);
		req.items[n].amount = b.amount;
		n++;
	}
	req.item_count = static_cast<uint8_t>(n);
	if (n < 1) { add_event_list(TP_TOAST_NEED_ITEMS, 10); return; }
	send_game_packet(req);
}

void DialogBox_TradingPost::send_rescind(int32_t offer_id)
{
	hb::net::PacketRequestTpRescindOffer req{};
	req.header.msg_id = MsgId::RequestTpRescindOffer;
	req.offer_id = offer_id;
	send_game_packet(req);
}

void DialogBox_TradingPost::send_finalize(int32_t listing_id, int32_t offer_id)
{
	hb::net::PacketRequestTpFinalize req{};
	req.header.msg_id = MsgId::RequestTpFinalize;
	req.listing_id = listing_id;
	req.offer_id = offer_id;
	send_game_packet(req);
}

void DialogBox_TradingPost::send_delist(int32_t listing_id)
{
	hb::net::PacketRequestTpDelist req{};
	req.header.msg_id = MsgId::RequestTpDelist;
	req.listing_id = listing_id;
	send_game_packet(req);
}

// ============================================================================
// Helpers
// ============================================================================
void DialogBox_TradingPost::switch_view(view v)
{
	if (m_view == view::create_listing && v != view::create_listing)
		text_input_manager::get().end_input();
	m_view = v;
	m_hover_row = -1;
}

void DialogBox_TradingPost::open_board_view(view v)
{
	switch_view(v);
	m_offer_scroll = 0;
	request_board(0);
}

void DialogBox_TradingPost::clear_bundle()
{
	for (auto& b : m_bundle)
	{
		if (b.inv_slot >= 0 && inventory_manager::get().is_locked(b.inv_slot))
			inventory_manager::get().unlock_item(b.inv_slot);
		b.inv_slot = -1;
		b.amount = 0;
	}
	m_note_text.clear();
}

void DialogBox_TradingPost::prune_stale_bundle()
{
	for (auto& b : m_bundle)
	{
		if (b.inv_slot < 0) continue;
		if (player().m_item_list[b.inv_slot] == nullptr)
		{
			if (inventory_manager::get().is_locked(b.inv_slot))
				inventory_manager::get().unlock_item(b.inv_slot);
			b.inv_slot = -1;
			b.amount = 0;
		}
	}
}

int DialogBox_TradingPost::staged_count() const
{
	int n = 0;
	for (auto& b : m_bundle) if (b.inv_slot >= 0) n++;
	return n;
}

bool DialogBox_TradingPost::has_own_offer() const
{
	if (!m_has_detail) return false;
	int oc = std::min<int>(m_detail.offer_count, hb::shared::limits::TpMaxOffersPerListing);
	for (int i = 0; i < oc; i++)
		if (is_mine(m_detail.offers[i].offerer)) return true;
	return false;
}

bool DialogBox_TradingPost::is_mine(const char* name) const
{
	return iequals(name, player().m_player_name.c_str());
}

bool DialogBox_TradingPost::stage_item(int inv_slot, int amount)
{
	if (m_view != view::create_listing && m_view != view::create_offer) return false;
	if (inv_slot < 0 || inv_slot >= hb::shared::limits::MaxItems) return false;
	if (amount < 1) return false;
	for (auto& b : m_bundle) if (b.inv_slot == inv_slot) return false;   // no duplicate slots
	for (auto& b : m_bundle)
	{
		if (b.inv_slot < 0) { b.inv_slot = inv_slot; b.amount = amount; return true; }
	}
	return false;   // bundle full
}

// slot_x/slot_y are the slot's top-left. Pack-atlas item sprites pivot on their
// own center (the same convention the Exchange/Bank rely on), so the draw point
// must be the slot's center, not its corner — otherwise the item hangs off the
// top-left.
void DialogBox_TradingPost::draw_item_icon(int slot_x, int slot_y, short item_id, char item_color)
{
	CItem* cfg = m_game->get_item_config(item_id);
	if (cfg == nullptr) return;
	auto d = m_game->get_item_draw(cfg->m_display_id, item_atlas::pack, cfg->sprite_is_female());
	if (d.sprite == nullptr) return;
	int cx = slot_x + tp::slot_size / 2;
	int cy = slot_y + tp::slot_size / 2;
	if (item_color == 0)
	{
		d.sprite->draw(cx, cy, d.frame);
	}
	else
	{
		const auto& tint = m_game->m_color_palette[static_cast<uint8_t>(item_color)];
		d.sprite->draw(cx, cy, d.frame, hb::shared::sprite::DrawParams::tint(tint.r, tint.g, tint.b));
	}
}

// ============================================================================
// Draw
// ============================================================================
void DialogBox_TradingPost::on_draw()
{
	if (!m_game->ensure_item_configs_loaded()) return;
	int mx = static_cast<int>(hb::shared::input::get_mouse_x());
	int my = static_cast<int>(hb::shared::input::get_mouse_y());

	draw_frame();
	draw_tabs(mx, my);
	switch (m_view)
	{
	case view::browse:
	case view::my_listings:
	case view::my_offers:
		draw_board(mx, my);
		break;
	case view::detail:
		draw_detail(mx, my);
		break;
	case view::create_listing:
	case view::create_offer:
		draw_create(mx, my);
		break;
	}
	draw_close_button(mx, my);
}

void DialogBox_TradingPost::draw_frame()
{
	m_game->m_Renderer->draw_rect_filled(m_x, m_y, tp::dlg_w, tp::dlg_h, tp_col::panel);
	m_game->m_Renderer->draw_rect_outline(m_x, m_y, tp::dlg_w, tp::dlg_h, tp_col::panel_edge, 1);
	hb::shared::text::draw_text_aligned(GameFont::Bitmap1, m_x, m_y + tp::title_y, tp::dlg_w, 16,
		"Trading Post",
		hb::shared::text::TextStyle::with_integrated_shadow(tp_col::header),
		hb::shared::text::Align::TopCenter);
}

void DialogBox_TradingPost::draw_close_button(int mx, int my)
{
	int x = m_x + tp::dlg_w - 22;
	int y = m_y + 6;
	bool hover = hit(mx, my, x, y, 16, 16);
	m_game->m_Renderer->draw_rect_filled(x, y, 16, 16, hover ? tp_col::btn_hover : tp_col::btn);
	m_game->m_Renderer->draw_rect_outline(x, y, 16, 16, tp_col::btn_edge, 1);
	hb::shared::text::draw_text_aligned(GameFont::Default, x, y, 16, 16, "X",
		hb::shared::text::TextStyle::from_color(hover ? tp_col::text_hi : tp_col::text),
		hb::shared::text::Align::Center);
}

void DialogBox_TradingPost::draw_tabs(int mx, int my)
{
	static const char* labels[tp::tab_count] = { "Browse", "My Listings", "My Offers", "+ List" };
	int active = -1;
	if (m_view == view::browse) active = 0;
	else if (m_view == view::my_listings) active = 1;
	else if (m_view == view::my_offers) active = 2;
	else if (m_view == view::create_listing) active = 3;

	for (int i = 0; i < tp::tab_count; i++)
	{
		int x = m_x + tp::inner_x + i * (tp::tab_w + tp::tab_gap);
		int y = m_y + tp::tab_y;
		bool hover = hit(mx, my, x, y, tp::tab_w, tp::tab_h);
		const auto& bg = (i == active) ? tp_col::tab_active : (hover ? tp_col::row_hover : tp_col::tab_bg);
		m_game->m_Renderer->draw_rect_filled(x, y, tp::tab_w, tp::tab_h, bg);
		m_game->m_Renderer->draw_rect_outline(x, y, tp::tab_w, tp::tab_h, tp_col::tab_edge, 1);
		auto color = (i == active) ? tp_col::text_hi : tp_col::text;
		hb::shared::text::draw_text_aligned(GameFont::Default, x, y, tp::tab_w, tp::tab_h, labels[i],
			hb::shared::text::TextStyle::from_color(color), hb::shared::text::Align::Center);
	}
}

void DialogBox_TradingPost::draw_board(int mx, int my)
{
	m_hover_row = -1;

	// The buffer holds a different slice than the active tab — still loading.
	if (m_board_kind != m_view)
	{
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::content_y + 40,
			tp::inner_w, 16, "Loading...", hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopCenter);
		return;
	}

	int rc = std::min<int>(m_board.row_count, tp::row_count);
	if (rc == 0)
	{
		const char* empty = (m_view == view::my_offers) ? TP_EMPTY_MY_OFFERS
			: (m_view == view::my_listings) ? TP_EMPTY_MY_LISTINGS : TP_EMPTY_BOARD;
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::content_y + 40,
			tp::inner_w, 16, empty, hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopCenter);
	}

	for (int i = 0; i < rc; i++)
	{
		const auto& row = m_board.rows[i];
		int x = m_x + tp::inner_x;
		int y = m_y + tp::content_y + i * tp::row_h;
		bool hover = hit(mx, my, x, y, tp::inner_w, tp::row_h - 2);
		if (hover) m_hover_row = i;
		m_game->m_Renderer->draw_rect_filled(x, y, tp::inner_w, tp::row_h - 2,
			hover ? tp_col::row_hover : tp_col::row_bg);

		// Line 1: bundle summary (left) + expiry (right).
		std::string summary;
		int ic = std::min<int>(row.item_count, hb::shared::limits::TpMaxListingItems);
		for (int k = 0; k < ic; k++)
		{
			// PHASE5-TP: TpItemBrief still carries the legacy packed attribute; prefix
			// naming returns when the wire structs adopt item_instance_data fields.
			auto info = item_name_formatter::get().format(row.items[k].item_id);
			if (!summary.empty()) summary += ", ";
			summary += info.name;
			if (row.items[k].count > 1) summary += " x" + m_game->format_comma_number(row.items[k].count);
		}
		hb::shared::text::draw_text_aligned(GameFont::Default, x + 6, y + 3, tp::inner_w - 92, 14,
			fit(summary, 52).c_str(), hb::shared::text::TextStyle::from_color(tp_col::text),
			hb::shared::text::Align::TopLeft);
		hb::shared::text::draw_text_aligned(GameFont::Default, x + tp::inner_w - 84, y + 3, 78, 14,
			fmt_expiry(row.expires_hours).c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopRight);

		// Line 2: seller + offer count + note preview.
		std::string line2 = std::string("Seller: ") + row.seller;
		line2 += std::format("    {} offer{}", static_cast<int>(row.offer_count),
			row.offer_count == 1 ? "" : "s");
		if (row.note[0] != '\0') line2 += std::string("    \"") + fit(row.note, 24) + "\"";
		hb::shared::text::draw_text_aligned(GameFont::Default, x + 6, y + 17, tp::inner_w - 12, 14,
			fit(line2, 70).c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopLeft);
	}

	// Page controls.
	int py = m_y + tp::page_y;
	int px = m_x + tp::inner_x;
	bool has_prev = m_page > 0;
	bool has_next = (m_page + 1) < m_board.page_count;
	draw_button(m_game, px, py, tp::page_btn_w, 16, "< Prev", mx, my, has_prev);
	draw_button(m_game, px + tp::inner_w - tp::page_btn_w, py, tp::page_btn_w, 16, "Next >", mx, my, has_next);
	int shown_page = (m_board.page_count == 0) ? 1 : m_page + 1;
	int total_page = (m_board.page_count == 0) ? 1 : m_board.page_count;
	auto pageinfo = std::format("Page {} / {}", shown_page, total_page);
	hb::shared::text::draw_text_aligned(GameFont::Default, px, py + 1, tp::inner_w, 14, pageinfo.c_str(),
		hb::shared::text::TextStyle::from_color(tp_col::text), hb::shared::text::Align::TopCenter);
}

void DialogBox_TradingPost::draw_detail(int mx, int my)
{
	if (!m_has_detail)
	{
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::content_y + 40,
			tp::inner_w, 16, "Loading...", hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopCenter);
		return;
	}

	int lx = m_x + tp::inner_x + 4;
	bool i_am_seller = is_mine(m_detail.seller);

	// Header: seller (left) + the Seller's nation, in its faction colour (right).
	auto hdr = std::format("Seller: {}", m_detail.seller);
	hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_header_y, tp::inner_w - 120, 14,
		hdr.c_str(), hb::shared::text::TextStyle::from_color(i_am_seller ? tp_col::mine : tp_col::text),
		hb::shared::text::Align::TopLeft);
	hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::det_header_y,
		tp::inner_w - 4, 14, nation_name(m_detail.nation),
		hb::shared::text::TextStyle::from_color(nation_color(m_detail.nation)),
		hb::shared::text::Align::TopRight);

	// Seeking note.
	std::string note = (m_detail.note[0] != '\0')
		? std::string("Seeking: ") + m_detail.note : std::string("Seeking: (no note)");
	hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_note_y, tp::inner_w - 8, 14,
		fit(note, 74).c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
		hb::shared::text::Align::TopLeft);

	// Listing bundle label (left) + expiry (right) + icons.
	hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_bundle_label_y, 200, 14,
		"Listing bundle:", hb::shared::text::TextStyle::from_color(tp_col::header),
		hb::shared::text::Align::TopLeft);
	auto exp = std::string("Expires in ") + fmt_expiry(m_detail.expires_hours);
	hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::det_bundle_label_y,
		tp::inner_w - 4, 14, exp.c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
		hb::shared::text::Align::TopRight);

	int hovered_bundle = -1;
	int ic = std::min<int>(m_detail.item_count, hb::shared::limits::TpMaxListingItems);
	for (int k = 0; k < ic; k++)
	{
		int sx = m_x + tp::inner_x + k * tp::slot_pitch;
		int sy = m_y + tp::det_bundle_y;
		bool hover = hit(mx, my, sx, sy, tp::slot_size, tp::slot_size);
		m_game->m_Renderer->draw_rect_filled(sx, sy, tp::slot_size, tp::slot_size, tp_col::slot_bg);
		m_game->m_Renderer->draw_rect_outline(sx, sy, tp::slot_size, tp::slot_size,
			hover ? tp_col::btn_edge : tp_col::slot_edge, 1);
		draw_item_icon(sx, sy, m_detail.items[k].item_id, static_cast<char>(m_detail.items[k].item_color));
		if (m_detail.items[k].count > 1)
		{
			hb::shared::text::draw_text_aligned(GameFont::Default, sx, sy + tp::slot_size - 13,
				tp::slot_size - 2, 12,
				("x" + m_game->format_comma_number(m_detail.items[k].count)).c_str(),
				hb::shared::text::TextStyle::from_color(tp_col::text_hi), hb::shared::text::Align::TopRight);
		}
		if (hover) hovered_bundle = k;
	}

	// Hovered bundle-item info strip.
	if (hovered_bundle >= 0)
	{
		const auto& it = m_detail.items[hovered_bundle];
		auto info = item_name_formatter::get().format(it.item_id);
		hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_info_y, tp::inner_w - 8, 14,
			info.name.c_str(),
			hb::shared::text::TextStyle::from_color(info.is_special ? GameColors::UIItemName_Special : tp_col::text_hi),
			hb::shared::text::Align::TopLeft);
		std::string extra = info.effect_text();
		if (it.count > 1) extra += (extra.empty() ? "" : "   ") + std::string("Count: ") + m_game->format_comma_number(it.count);
		if (it.cur_lifespan > 0) extra += (extra.empty() ? "" : "   ") + std::format("Dur: {}", it.cur_lifespan);
		if (!extra.empty())
			hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_info_y + 14, tp::inner_w - 8, 14,
				fit(extra, 74).c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
				hb::shared::text::Align::TopLeft);
	}

	// Offers header.
	auto offers_lbl = std::format("Offers ({}/{}):", static_cast<int>(m_detail.offer_count),
		hb::shared::limits::TpMaxOffersPerListing);
	hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_offers_label_y, 200, 14,
		offers_lbl.c_str(), hb::shared::text::TextStyle::from_color(tp_col::header),
		hb::shared::text::Align::TopLeft);

	// Mouse-wheel scroll over the offers list.
	short z = static_cast<short>(hb::shared::input::get_mouse_wheel_delta());
	if (m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::TradingPost && z != 0)
	{
		m_offer_scroll -= z / 60;
		int max_scroll = std::max(0, static_cast<int>(m_detail.offer_count) - tp::det_offers_visible);
		m_offer_scroll = std::clamp(m_offer_scroll, 0, max_scroll);
	}

	int oc = std::min<int>(m_detail.offer_count, hb::shared::limits::TpMaxOffersPerListing);
	if (oc == 0)
	{
		hb::shared::text::draw_text_aligned(GameFont::Default, lx, m_y + tp::det_offers_y + 6, tp::inner_w - 8, 14,
			"No Offers yet.", hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopLeft);
	}
	for (int vis = 0; vis < tp::det_offers_visible; vis++)
	{
		int i = m_offer_scroll + vis;
		if (i >= oc) break;
		const auto& ov = m_detail.offers[i];
		int x = m_x + tp::inner_x;
		int y = m_y + tp::det_offers_y + vis * tp::det_offer_row_h;
		m_game->m_Renderer->draw_rect_filled(x, y, tp::inner_w, tp::det_offer_row_h - 2, tp_col::row_bg);

		bool mine = is_mine(ov.offerer);
		std::string label = std::string(ov.offerer) + ":  ";
		int oic = std::min<int>(ov.item_count, hb::shared::limits::TpMaxOfferItems);
		if (oic > 0)
		{
			auto info = item_name_formatter::get().format(ov.items[0].item_id);
			label += info.name;
			if (ov.items[0].count > 1) label += " x" + m_game->format_comma_number(ov.items[0].count);
			if (oic > 1) label += std::format(" (+{})", oic - 1);
		}
		hb::shared::text::draw_text_aligned(GameFont::Default, x + 6, y + 3, tp::inner_w - 78, 14,
			fit(label, 52).c_str(),
			hb::shared::text::TextStyle::from_color(mine ? tp_col::mine : tp_col::text),
			hb::shared::text::Align::TopLeft);

		// Contextual per-offer button: Finalize (seller) or Rescind (my Offer).
		if (i_am_seller)
			draw_button(m_game, x + tp::inner_w - 66, y + 1, 62, tp::det_offer_row_h - 4, "Finalize", mx, my);
		else if (mine)
			draw_button(m_game, x + tp::inner_w - 66, y + 1, 62, tp::det_offer_row_h - 4, "Rescind", mx, my);
	}

	// Bottom actions.
	int ay = m_y + tp::det_actions_y;
	draw_button(m_game, m_x + tp::inner_x, ay, 70, tp::det_btn_h, "< Back", mx, my);
	if (i_am_seller)
		draw_button(m_game, m_x + tp::inner_x + tp::inner_w - 100, ay, 100, tp::det_btn_h, "Delist", mx, my);
	else if (!has_own_offer())
		draw_button(m_game, m_x + tp::inner_x + tp::inner_w - 100, ay, 100, tp::det_btn_h, "Place Offer", mx, my);
}

void DialogBox_TradingPost::draw_create(int mx, int my)
{
	prune_stale_bundle();
	bool is_listing = (m_view == view::create_listing);

	hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::content_y, tp::inner_w, 14,
		is_listing ? "Create Listing" : "Create Offer",
		hb::shared::text::TextStyle::from_color(tp_col::header), hb::shared::text::Align::TopCenter);

	hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x, m_y + tp::cr_hint_y, tp::inner_w, 14,
		"Drag items from your inventory. Stackables ask for an amount.",
		hb::shared::text::TextStyle::from_color(tp_col::text_dim), hb::shared::text::Align::TopCenter);

	// 4-slot bundle grid.
	int hovered = -1;
	for (int k = 0; k < hb::shared::limits::TpMaxListingItems; k++)
	{
		int sx = m_x + tp::inner_x + 4 + k * tp::slot_pitch;
		int sy = m_y + tp::cr_grid_y;
		bool hover = hit(mx, my, sx, sy, tp::slot_size, tp::slot_size);
		m_game->m_Renderer->draw_rect_filled(sx, sy, tp::slot_size, tp::slot_size, tp_col::slot_bg);
		m_game->m_Renderer->draw_rect_outline(sx, sy, tp::slot_size, tp::slot_size,
			hover ? tp_col::btn_edge : tp_col::slot_edge, 1);

		const auto& b = m_bundle[k];
		if (b.inv_slot >= 0 && player().m_item_list[b.inv_slot] != nullptr)
		{
			CItem* it = player().m_item_list[b.inv_slot].get();
			draw_item_icon(sx, sy, it->m_id_num, static_cast<char>(it->m_instance.item_color));
			if (b.amount > 1)
				hb::shared::text::draw_text_aligned(GameFont::Default, sx, sy + tp::slot_size - 13,
					tp::slot_size - 2, 12, ("x" + m_game->format_comma_number(static_cast<uint64_t>(b.amount))).c_str(),
					hb::shared::text::TextStyle::from_color(tp_col::text_hi), hb::shared::text::Align::TopRight);
			if (hover) hovered = k;
		}
	}

	// Hovered staged-item info + a remove hint.
	if (hovered >= 0 && player().m_item_list[m_bundle[hovered].inv_slot] != nullptr)
	{
		auto info = item_name_formatter::get().format(player().m_item_list[m_bundle[hovered].inv_slot].get());
		auto line = info.name + "   (click to remove)";
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x + 4, m_y + tp::cr_info_y,
			tp::inner_w - 8, 14, fit(line, 74).c_str(),
			hb::shared::text::TextStyle::from_color(tp_col::text), hb::shared::text::Align::TopLeft);
	}

	if (is_listing)
	{
		// Seeking-note label + field + char count. The text_input_manager renders
		// the field content when this dialog is the top one.
		auto lbl = std::format("Seeking note (optional, <= {}):", hb::shared::limits::TpSeekingNoteMaxChars);
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x + 4, m_y + tp::cr_note_label_y,
			300, 14, lbl.c_str(), hb::shared::text::TextStyle::from_color(tp_col::header),
			hb::shared::text::Align::TopLeft);

		int fx = m_x + tp::inner_x + 4;
		int fy = m_y + tp::cr_note_y;
		m_game->m_Renderer->draw_rect_filled(fx, fy, tp::inner_w - 8, tp::cr_note_h, tp_col::slot_bg);
		m_game->m_Renderer->draw_rect_outline(fx, fy, tp::inner_w - 8, tp::cr_note_h, tp_col::slot_edge, 1);

		bool top = m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::TradingPost;
		if (top)
		{
			if (!text_input_manager::get().is_active())
			{
				text_input_manager::get().start_input(fx + 4, fy + 2,
					hb::shared::limits::TpSeekingNoteMaxChars, m_note_text);
				m_last_sx = m_x; m_last_sy = m_y;
			}
			else if (m_x != m_last_sx || m_y != m_last_sy)
			{
				text_input_manager::get().end_input();
				text_input_manager::get().start_input(fx + 4, fy + 2,
					hb::shared::limits::TpSeekingNoteMaxChars, m_note_text);
				m_last_sx = m_x; m_last_sy = m_y;
			}
		}
		auto cnt = std::format("{}/{}", m_note_text.size(), hb::shared::limits::TpSeekingNoteMaxChars);
		hb::shared::text::draw_text_aligned(GameFont::Default, fx, fy - 15, tp::inner_w - 8, 14, cnt.c_str(),
			hb::shared::text::TextStyle::from_color(tp_col::text_dim), hb::shared::text::Align::TopRight);
	}
	else
	{
		// Create Offer: name the target Listing.
		auto tgt = m_has_detail ? std::format("Offering on {}'s Listing", m_detail.seller)
			: std::string("Offering on a Listing");
		hb::shared::text::draw_text_aligned(GameFont::Default, m_x + tp::inner_x + 4, m_y + tp::cr_note_label_y,
			tp::inner_w - 8, 14, tgt.c_str(), hb::shared::text::TextStyle::from_color(tp_col::text_dim),
			hb::shared::text::Align::TopLeft);
	}

	// Confirm + Cancel.
	bool can_send = staged_count() >= 1;
	int by = m_y + tp::cr_buttons_y;
	draw_button(m_game, m_x + tp::inner_x + 4, by, 130, tp::cr_btn_h,
		is_listing ? "Post Listing" : "Place Offer", mx, my, can_send);
	draw_button(m_game, m_x + tp::inner_x + tp::inner_w - 90, by, 86, tp::cr_btn_h, "Cancel", mx, my);
}

// ============================================================================
// Click
// ============================================================================
bool DialogBox_TradingPost::on_click()
{
	int mx = static_cast<int>(hb::shared::input::get_mouse_x());
	int my = static_cast<int>(hb::shared::input::get_mouse_y());

	// Close button.
	if (hit(mx, my, m_x + tp::dlg_w - 22, m_y + 6, 16, 16))
	{
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		disable_this_dialog();
		return true;
	}

	if (click_tabs(mx, my)) return true;

	switch (m_view)
	{
	case view::browse:
	case view::my_listings:
	case view::my_offers:
		return click_board(mx, my);
	case view::detail:
		return click_detail(mx, my);
	case view::create_listing:
	case view::create_offer:
		return click_create(mx, my);
	}
	return false;
}

bool DialogBox_TradingPost::click_tabs(int mx, int my)
{
	for (int i = 0; i < tp::tab_count; i++)
	{
		int x = m_x + tp::inner_x + i * (tp::tab_w + tp::tab_gap);
		int y = m_y + tp::tab_y;
		if (!hit(mx, my, x, y, tp::tab_w, tp::tab_h)) continue;
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		switch (i)
		{
		case 0: open_board_view(view::browse); break;
		case 1: open_board_view(view::my_listings); break;
		case 2: open_board_view(view::my_offers); break;
		case 3: clear_bundle(); switch_view(view::create_listing); break;
		}
		return true;
	}
	return false;
}

bool DialogBox_TradingPost::click_board(int mx, int my)
{
	// Page controls.
	int py = m_y + tp::page_y;
	int px = m_x + tp::inner_x;
	if (hit(mx, my, px, py, tp::page_btn_w, 16))
	{
		if (m_page > 0) { audio_manager::get().play_game_sound(sound_type::effect, 14, 5); request_board(m_page - 1); }
		return true;
	}
	if (hit(mx, my, px + tp::inner_w - tp::page_btn_w, py, tp::page_btn_w, 16))
	{
		if ((m_page + 1) < m_board.page_count)
		{
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			request_board(m_page + 1);
		}
		return true;
	}

	if (m_board_kind != m_view) return false;
	int rc = std::min<int>(m_board.row_count, tp::row_count);
	for (int i = 0; i < rc; i++)
	{
		int x = m_x + tp::inner_x;
		int y = m_y + tp::content_y + i * tp::row_h;
		if (!hit(mx, my, x, y, tp::inner_w, tp::row_h - 2)) continue;
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		m_detail_return = m_view;
		m_has_detail = false;
		m_offer_scroll = 0;
		switch_view(view::detail);
		request_detail(m_board.rows[i].listing_id);
		return true;
	}
	return false;
}

bool DialogBox_TradingPost::click_detail(int mx, int my)
{
	if (!m_has_detail) return false;
	bool i_am_seller = is_mine(m_detail.seller);

	// Per-offer buttons.
	int oc = std::min<int>(m_detail.offer_count, hb::shared::limits::TpMaxOffersPerListing);
	for (int vis = 0; vis < tp::det_offers_visible; vis++)
	{
		int i = m_offer_scroll + vis;
		if (i >= oc) break;
		int x = m_x + tp::inner_x;
		int y = m_y + tp::det_offers_y + vis * tp::det_offer_row_h;
		if (!hit(mx, my, x + tp::inner_w - 66, y + 1, 62, tp::det_offer_row_h - 4)) continue;
		const auto& ov = m_detail.offers[i];
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		if (i_am_seller)
			send_finalize(m_detail.listing_id, ov.offer_id);
		else if (is_mine(ov.offerer))
			send_rescind(ov.offer_id);
		return true;
	}

	// Bottom actions.
	int ay = m_y + tp::det_actions_y;
	if (hit(mx, my, m_x + tp::inner_x, ay, 70, tp::det_btn_h))
	{
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		open_board_view(m_detail_return);
		return true;
	}
	if (hit(mx, my, m_x + tp::inner_x + tp::inner_w - 100, ay, 100, tp::det_btn_h))
	{
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		if (i_am_seller)
		{
			send_delist(m_detail.listing_id);
		}
		else if (!has_own_offer())
		{
			clear_bundle();
			switch_view(view::create_offer);
		}
		return true;
	}
	return false;
}

bool DialogBox_TradingPost::click_create(int mx, int my)
{
	// Click a staged slot to remove it (and unlock the inventory item).
	for (int k = 0; k < hb::shared::limits::TpMaxListingItems; k++)
	{
		int sx = m_x + tp::inner_x + 4 + k * tp::slot_pitch;
		int sy = m_y + tp::cr_grid_y;
		if (!hit(mx, my, sx, sy, tp::slot_size, tp::slot_size)) continue;
		if (m_bundle[k].inv_slot >= 0)
		{
			if (inventory_manager::get().is_locked(m_bundle[k].inv_slot))
				inventory_manager::get().unlock_item(m_bundle[k].inv_slot);
			m_bundle[k].inv_slot = -1;
			m_bundle[k].amount = 0;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		}
		return true;
	}

	bool is_listing = (m_view == view::create_listing);
	int by = m_y + tp::cr_buttons_y;

	// Post / Offer.
	if (hit(mx, my, m_x + tp::inner_x + 4, by, 130, tp::cr_btn_h))
	{
		if (staged_count() >= 1)
		{
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			if (is_listing) send_create_listing();
			else send_place_offer(m_detail_listing_id);
		}
		return true;
	}

	// Cancel.
	if (hit(mx, my, m_x + tp::inner_x + tp::inner_w - 90, by, 86, tp::cr_btn_h))
	{
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		clear_bundle();
		if (is_listing) open_board_view(view::browse);
		else switch_view(view::detail);   // back to the Listing we were offering on
		return true;
	}
	return false;
}

// ============================================================================
// Item drop (Create views only)
// ============================================================================
bool DialogBox_TradingPost::on_item_drop()
{
	if (m_view != view::create_listing && m_view != view::create_offer) return false;
	if (staged_count() >= hb::shared::limits::TpMaxListingItems) return false;   // grid full

	int inv_slot = CursorTarget::get_selected_id();
	if (inv_slot < 0 || inv_slot >= hb::shared::limits::MaxItems) return false;
	if (player().m_item_list[inv_slot] == nullptr) return false;
	for (auto& b : m_bundle) if (b.inv_slot == inv_slot) return false;           // already staged

	CItem* cfg = m_game->get_item_config(player().m_item_list[inv_slot]->m_id_num);
	bool stackable = cfg && cfg->is_stackable() && player().m_item_list[inv_slot]->m_instance.count > 1;

	if (stackable)
	{
		// Route through the amount picker (drop-target 1003 in Screen_OnGame.cpp),
		// which stages the chosen amount and locks the slot for us.
		short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
		short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
		auto* drop = m_game->get_dialog_box_manager().get_dialog_as<DialogBox_ItemDropAmount>(DialogBoxId::ItemDropExternal);
		drop->m_x = mouse_x - 140;
		drop->m_y = mouse_y - 70;
		if (drop->m_y < 0) drop->m_y = 0;
		drop->m_drop_x = player().m_player_x + 1;
		drop->m_drop_y = player().m_player_y + 1;
		drop->m_drop_target_type = 1003;
		drop->m_drop_target_id = inv_slot;
		std::memset(drop->m_target_name, 0, sizeof(drop->m_target_name));
		m_game->get_dialog_box_manager().enable_dialog_box(DialogBoxId::ItemDropExternal, inv_slot,
			static_cast<int64_t>(player().m_item_list[inv_slot]->m_instance.count), 0);
	}
	else
	{
		// Single item — stage the whole thing and lock it.
		if (stage_item(inv_slot, 1))
			inventory_manager::get().lock_item(inv_slot);
	}
	return true;
}
