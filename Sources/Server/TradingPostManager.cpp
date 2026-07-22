#include "TradingPostManager.h"
#include "TradingPostStore.h"

#include "Game.h"
#include "Client.h"
#include "StringCompat.h"
#include "OwnerType.h"
#include "Packet/PacketTradingPost.h"
#include "Packet/PacketHelpers.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace hb::shared::net;    // MsgId, MsgType, Notify
using namespace hb::net;            // TpAction, TpResultCode, Tp* packet structs
using namespace hb::server::net;    // ItemLogAction
using namespace hb::server::config; // MaxNpcs

namespace
{
	// Expiry granularity is days, so a coarse sweep cadence is plenty; this rides
	// the game tick alongside the per-client autosave, per the plan.
	constexpr uint32_t sweep_interval_ms = 60000;

	// Auctioneer interaction radius in tiles. A dialog opened from a short distance
	// is fine; the scan stays small (a box around the actor) and precise.
	constexpr int auctioneer_range = 5;

	int64_t now_seconds()
	{
		return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	}

	uint16_t hours_until(int64_t expires_at, int64_t now)
	{
		int64_t secs = expires_at - now;
		if (secs < 0) secs = 0;
		int64_t hours = secs / 3600;
		if (hours > 0xFFFF) hours = 0xFFFF;
		return static_cast<uint16_t>(hours);
	}

	// Seeking-note sanitization (implementation-time pick — no chat filter exists
	// in this codebase): bounded length + printable ASCII only. An empty note is
	// allowed (the note is optional). Rejects control characters and high bytes.
	bool note_is_valid(const char* note)
	{
		size_t len = 0;
		while (len < hb::shared::limits::TpSeekingNoteLen && note[len] != '\0') {
			const unsigned char ch = static_cast<unsigned char>(note[len]);
			if (ch < 0x20 || ch > 0x7E) {
				return false;
			}
			len++;
		}
		if (len >= hb::shared::limits::TpSeekingNoteLen) {
			return false; // not null-terminated within the wire buffer
		}
		return len <= hb::shared::limits::TpSeekingNoteMaxChars;
	}

	void fill_brief(hb::net::TpItemBrief& dst, const hb::server::escrow_item& e)
	{
		dst.item_id = e.item_id;
		dst.count = e.count;
		dst.attribute = e.attribute;
	}

	void fill_full(hb::net::TpItemFull& dst, const hb::server::escrow_item& e)
	{
		dst.item_id = e.item_id;
		dst.count = e.count;
		dst.touch_effect_type = e.touch_effect_type;
		dst.touch_effect_value1 = e.touch_effect_value1;
		dst.touch_effect_value2 = e.touch_effect_value2;
		dst.touch_effect_value3 = e.touch_effect_value3;
		dst.item_color = e.item_color;
		dst.spec_effect_value1 = e.spec_effect_value1;
		dst.spec_effect_value2 = e.spec_effect_value2;
		dst.spec_effect_value3 = e.spec_effect_value3;
		dst.cur_lifespan = e.cur_lifespan;
		dst.attribute = e.attribute;
	}

	// Build and send a PacketResponseTpBoardPage from a page of listing_briefs.
	// Shared by the public board (handle_board_page) and the actor's own slices
	// (handle_my_board) — the wire shape is identical.
	void send_board_page_packet(CGame* game, int client_h, uint16_t page,
		const std::vector<hb::server::listing_brief>& rows, int total)
	{
		const int64_t now = now_seconds();
		PacketResponseTpBoardPage pkt{};
		pkt.header.msg_id = MsgId::ResponseTpBoardPage;
		pkt.header.msg_type = MsgType::Confirm;
		pkt.page = page;
		int page_count = (total + hb::shared::limits::TpBoardPageRows - 1)
			/ hb::shared::limits::TpBoardPageRows;
		if (page_count < 1) page_count = 1;
		pkt.page_count = static_cast<uint16_t>(page_count);

		int rc = static_cast<int>(rows.size());
		if (rc > hb::shared::limits::TpBoardPageRows) rc = hb::shared::limits::TpBoardPageRows;
		pkt.row_count = static_cast<uint16_t>(rc);
		for (int i = 0; i < rc; i++) {
			const hb::server::listing_brief& b = rows[i];
			TpBoardRow& row = pkt.rows[i];
			row.listing_id = static_cast<int32_t>(b.listing_id);
			std::snprintf(row.seller, sizeof(row.seller), "%s", b.seller_name.c_str());
			row.nation = static_cast<uint8_t>(b.seller_nation);
			row.offer_count = static_cast<uint8_t>(b.offer_count);
			row.expires_hours = hours_until(b.expires_at, now);
			std::snprintf(row.note, sizeof(row.note), "%s", b.seeking_note.c_str());
			int ic = static_cast<int>(b.items.size());
			if (ic > hb::shared::limits::TpMaxListingItems) ic = hb::shared::limits::TpMaxListingItems;
			row.item_count = static_cast<uint8_t>(ic);
			for (int k = 0; k < ic; k++) {
				fill_brief(row.items[k], b.items[k]);
			}
		}

		CClient* c = game->m_client_list[client_h];
		if (c != nullptr && c->m_socket != nullptr) {
			c->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
		}
	}
}

namespace hb::server
{
	trading_post_store* trading_post_manager::store() const
	{
		if (m_game == nullptr) {
			return nullptr;
		}
		return m_game->m_trading_post_store.get();
	}

	bool trading_post_manager::is_near_auctioneer(int client_h) const
	{
		if (m_game == nullptr) {
			return false;
		}
		CClient* c = m_game->m_client_list[client_h];
		if (c == nullptr) {
			return false;
		}
		CMap* map = m_game->m_map_list[c->m_map_index];
		if (map == nullptr) {
			return false;
		}
		for (int dy = -auctioneer_range; dy <= auctioneer_range; dy++) {
			for (int dx = -auctioneer_range; dx <= auctioneer_range; dx++) {
				short owner = 0;
				char owner_class = 0;
				map->get_owner(&owner, &owner_class,
					static_cast<short>(c->m_x + dx), static_cast<short>(c->m_y + dy));
				if (owner_class == hb::shared::owner_class::Npc && owner > 0 && owner < MaxNpcs) {
					CNpc* npc = m_game->m_npc_list[owner];
					if (npc != nullptr && npc->m_type == hb::shared::owner::auctioneer) {
						return true;
					}
				}
			}
		}
		return false;
	}

	void trading_post_manager::send_result(int client_h, uint16_t action, uint16_t code, int32_t value)
	{
		if (m_game == nullptr) {
			return;
		}
		CClient* c = m_game->m_client_list[client_h];
		if (c == nullptr || c->m_socket == nullptr) {
			return;
		}
		PacketResponseTpActionResult pkt{};
		pkt.header.msg_id = MsgId::ResponseTpActionResult;
		pkt.header.msg_type = (code == TpResultCode::Ok) ? MsgType::Confirm : MsgType::Reject;
		pkt.action = action;
		pkt.result_code = code;
		pkt.value = value;
		c->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}

	bool trading_post_manager::actor_ready(int client_h, uint16_t action)
	{
		if (m_game == nullptr) {
			return false;
		}
		CClient* c = m_game->m_client_list[client_h];
		if (c == nullptr || !c->m_is_init_complete) {
			return false; // offline / not in the world yet — cannot be answered
		}
		if (store() == nullptr || !store()->is_open()) {
			send_result(client_h, action, TpResultCode::Failed);
			return false;
		}
		if (c->m_is_killed || c->m_is_on_server_change || c->m_is_exchange_mode) {
			send_result(client_h, action, TpResultCode::Busy);
			return false;
		}
		if (!is_near_auctioneer(client_h)) {
			send_result(client_h, action, TpResultCode::NotNearAuctioneer);
			return false;
		}
		return true;
	}

	// ---- Handlers ------------------------------------------------------------

	void trading_post_manager::handle_board_page(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpBoardPage>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::BoardPage)) {
			return;
		}

		std::vector<listing_brief> rows;
		int total = 0;
		store()->get_board_page(static_cast<int>(req->page),
			hb::shared::limits::TpBoardPageRows, rows, total);

		send_board_page_packet(m_game, client_h, req->page, rows, total);
	}

	void trading_post_manager::handle_my_board(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpMyBoard>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::MyBoard)) {
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		std::vector<listing_brief> rows;
		int total = 0;
		if (req->which == TpMyBoardFilter::MyOffers) {
			store()->get_my_offers_page(c->m_char_name, static_cast<int>(req->page),
				hb::shared::limits::TpBoardPageRows, rows, total);
		}
		else {
			store()->get_my_listings_page(c->m_char_name, static_cast<int>(req->page),
				hb::shared::limits::TpBoardPageRows, rows, total);
		}

		send_board_page_packet(m_game, client_h, req->page, rows, total);
	}

	void trading_post_manager::handle_listing_detail(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpListingDetail>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::ListingDetail)) {
			return;
		}

		listing_detail detail;
		if (!store()->get_listing_detail(req->listing_id, detail)) {
			send_result(client_h, TpAction::ListingDetail, TpResultCode::ListingGone);
			return;
		}

		const int64_t now = now_seconds();
		PacketResponseTpListingDetail pkt{};
		pkt.header.msg_id = MsgId::ResponseTpListingDetail;
		pkt.header.msg_type = MsgType::Confirm;
		pkt.listing_id = static_cast<int32_t>(detail.listing_id);
		std::snprintf(pkt.seller, sizeof(pkt.seller), "%s", detail.seller_name.c_str());
		pkt.nation = static_cast<uint8_t>(detail.seller_nation);
		std::snprintf(pkt.note, sizeof(pkt.note), "%s", detail.seeking_note.c_str());
		pkt.expires_hours = hours_until(detail.expires_at, now);

		int ic = static_cast<int>(detail.items.size());
		if (ic > hb::shared::limits::TpMaxListingItems) ic = hb::shared::limits::TpMaxListingItems;
		pkt.item_count = static_cast<uint8_t>(ic);
		for (int k = 0; k < ic; k++) {
			fill_full(pkt.items[k], detail.items[k]);
		}

		int oc = static_cast<int>(detail.offers.size());
		if (oc > hb::shared::limits::TpMaxOffersPerListing) oc = hb::shared::limits::TpMaxOffersPerListing;
		pkt.offer_count = static_cast<uint8_t>(oc);
		for (int i = 0; i < oc; i++) {
			const offer_view& ov = detail.offers[i];
			TpOfferView& dst = pkt.offers[i];
			dst.offer_id = static_cast<int32_t>(ov.offer_id);
			std::snprintf(dst.offerer, sizeof(dst.offerer), "%s", ov.offerer_name.c_str());
			int oic = static_cast<int>(ov.items.size());
			if (oic > hb::shared::limits::TpMaxOfferItems) oic = hb::shared::limits::TpMaxOfferItems;
			dst.item_count = static_cast<uint8_t>(oic);
			for (int k = 0; k < oic; k++) {
				fill_full(dst.items[k], ov.items[k]);
			}
		}

		CClient* c = m_game->m_client_list[client_h];
		if (c != nullptr && c->m_socket != nullptr) {
			c->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
		}
	}

	void trading_post_manager::handle_create_listing(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpCreateListing>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::CreateListing)) {
			return;
		}

		if (!note_is_valid(req->note)) {
			send_result(client_h, TpAction::CreateListing, TpResultCode::InvalidNote);
			return;
		}
		if (req->item_count < 1 || req->item_count > hb::shared::limits::TpMaxListingItems) {
			send_result(client_h, TpAction::CreateListing, TpResultCode::InvalidBundle);
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		if (store()->count_active_listings(c->m_char_name) >= hb::shared::limits::TpMaxListingsPerChar) {
			send_result(client_h, TpAction::CreateListing, TpResultCode::TooManyListings);
			return;
		}

		int64_t listing_id = 0;
		uint16_t code = TpResultCode::Failed;
		store()->create_listing(client_h, req->note, req->items,
			static_cast<int>(req->item_count), listing_id, code);
		send_result(client_h, TpAction::CreateListing, code, static_cast<int32_t>(listing_id));
	}

	void trading_post_manager::handle_place_offer(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpPlaceOffer>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::PlaceOffer)) {
			return;
		}

		if (req->item_count < 1 || req->item_count > hb::shared::limits::TpMaxOfferItems) {
			send_result(client_h, TpAction::PlaceOffer, TpResultCode::InvalidBundle);
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		std::string seller_name, seller_account;
		int64_t expires_at = 0;
		if (!store()->get_listing_owner(req->listing_id, seller_name, seller_account, expires_at)) {
			send_result(client_h, TpAction::PlaceOffer, TpResultCode::ListingGone);
			return;
		}
		// Same-ACCOUNT block: no character on the Seller's account may Offer.
		if (hb_strnicmp(seller_account.c_str(), c->m_account_name,
			hb::shared::limits::AccountNameLen - 1) == 0) {
			send_result(client_h, TpAction::PlaceOffer, TpResultCode::AccountSelfTrade);
			return;
		}
		if (store()->count_offers(req->listing_id) >= hb::shared::limits::TpMaxOffersPerListing) {
			send_result(client_h, TpAction::PlaceOffer, TpResultCode::TooManyOffers);
			return;
		}

		int64_t offer_id = 0;
		uint16_t code = TpResultCode::Failed;
		// place_offer re-checks ListingGone + AlreadyOffered before escrow-in.
		store()->place_offer(client_h, req->listing_id, req->items,
			static_cast<int>(req->item_count), offer_id, code);
		send_result(client_h, TpAction::PlaceOffer, code, static_cast<int32_t>(offer_id));
	}

	void trading_post_manager::handle_rescind_offer(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpRescindOffer>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::RescindOffer)) {
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		std::string offerer_name, offerer_account;
		int64_t listing_id = 0;
		if (!store()->get_offer_owner(req->offer_id, offerer_name, offerer_account, listing_id)) {
			send_result(client_h, TpAction::RescindOffer, TpResultCode::OfferGone);
			return;
		}
		// Only the offerer may rescind their own Offer (do not leak others' ids).
		if (hb_strnicmp(offerer_name.c_str(), c->m_char_name,
			hb::shared::limits::CharNameLen - 1) != 0) {
			send_result(client_h, TpAction::RescindOffer, TpResultCode::OfferGone);
			return;
		}

		const bool ok = store()->refund_offer(req->offer_id, ItemLogAction::TpRescind);
		send_result(client_h, TpAction::RescindOffer,
			ok ? TpResultCode::Ok : TpResultCode::OfferGone,
			static_cast<int32_t>(req->offer_id));
	}

	void trading_post_manager::handle_finalize(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpFinalize>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::Finalize)) {
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		std::string seller_name, seller_account;
		int64_t expires_at = 0;
		if (!store()->get_listing_owner(req->listing_id, seller_name, seller_account, expires_at)) {
			send_result(client_h, TpAction::Finalize, TpResultCode::ListingGone);
			return;
		}
		// Only the Seller may finalize their own Listing.
		if (hb_strnicmp(seller_name.c_str(), c->m_char_name,
			hb::shared::limits::CharNameLen - 1) != 0) {
			send_result(client_h, TpAction::Finalize, TpResultCode::NotSeller);
			return;
		}

		uint16_t code = TpResultCode::Failed;
		store()->finalize(req->listing_id, req->offer_id, code);
		send_result(client_h, TpAction::Finalize, code, static_cast<int32_t>(req->listing_id));
	}

	void trading_post_manager::handle_delist(int client_h, char* data, size_t size)
	{
		const auto* req = hb::net::PacketCast<PacketRequestTpDelist>(data, size);
		if (req == nullptr) {
			return;
		}
		if (!actor_ready(client_h, TpAction::Delist)) {
			return;
		}

		CClient* c = m_game->m_client_list[client_h];
		std::string seller_name, seller_account;
		int64_t expires_at = 0;
		if (!store()->get_listing_owner(req->listing_id, seller_name, seller_account, expires_at)) {
			send_result(client_h, TpAction::Delist, TpResultCode::ListingGone);
			return;
		}
		// Only the Seller may delist their own Listing.
		if (hb_strnicmp(seller_name.c_str(), c->m_char_name,
			hb::shared::limits::CharNameLen - 1) != 0) {
			send_result(client_h, TpAction::Delist, TpResultCode::NotSeller);
			return;
		}

		uint16_t code = TpResultCode::Failed;
		store()->delist(req->listing_id, code);
		send_result(client_h, TpAction::Delist, code, static_cast<int32_t>(req->listing_id));
	}

	// ---- Hooks ---------------------------------------------------------------

	void trading_post_manager::on_player_login(int client_h)
	{
		if (store() == nullptr || !store()->is_open()) {
			return;
		}
		store()->flush_notices(client_h);
	}

	void trading_post_manager::process_expiry(uint32_t now_ms)
	{
		if (store() == nullptr || !store()->is_open()) {
			return;
		}
		if (m_swept_once && (now_ms - m_last_sweep_ms) < sweep_interval_ms) {
			return;
		}
		m_swept_once = true;
		m_last_sweep_ms = now_ms;
		store()->sweep_expired();
	}
}
