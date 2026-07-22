// NetworkMessages_TradingPost.cpp — receive handlers for the Trading Post.
//
// The three Tp response families are top-level MsgIds (not Notify sub-types), so
// they are dispatched from Screen_OnGame::on_game_msg. Each handler validates the
// packet, then feeds the open DialogBox_TradingPost:
//   ResponseTpBoardPage     -> board rows for the active tab (browse / mine)
//   ResponseTpListingDetail -> the open Listing detail
//   ResponseTpActionResult  -> a result toast + pull-only refresh of the board
//
// The board is pull-only: the dialog re-requests the current page or detail after
// each of its own successful actions (see DialogBox_TradingPost::receive_*).

#include "Game.h"
#include "DialogBoxManager.h"
#include "DialogBoxIDs.h"
#include "DialogBox_TradingPost.h"
#include "Packet/PacketTradingPost.h"
#include "Packet/PacketHelpers.h"

namespace NetworkMessageHandlers
{
	void HandleTpBoardPage(CGame* game, char* data)
	{
		const auto* pkt = hb::net::PacketCast<hb::net::PacketResponseTpBoardPage>(
			data, sizeof(hb::net::PacketResponseTpBoardPage));
		if (!pkt) return;
		if (!game->get_dialog_box_manager().is_enabled(DialogBoxId::TradingPost)) return;
		auto* dlg = game->get_dialog_box_manager().get_dialog_as<DialogBox_TradingPost>(DialogBoxId::TradingPost);
		if (dlg) dlg->receive_board_page(pkt);
	}

	void HandleTpListingDetail(CGame* game, char* data)
	{
		const auto* pkt = hb::net::PacketCast<hb::net::PacketResponseTpListingDetail>(
			data, sizeof(hb::net::PacketResponseTpListingDetail));
		if (!pkt) return;
		if (!game->get_dialog_box_manager().is_enabled(DialogBoxId::TradingPost)) return;
		auto* dlg = game->get_dialog_box_manager().get_dialog_as<DialogBox_TradingPost>(DialogBoxId::TradingPost);
		if (dlg) dlg->receive_listing_detail(pkt);
	}

	void HandleTpActionResult(CGame* game, char* data)
	{
		const auto* pkt = hb::net::PacketCast<hb::net::PacketResponseTpActionResult>(
			data, sizeof(hb::net::PacketResponseTpActionResult));
		if (!pkt) return;
		if (!game->get_dialog_box_manager().is_enabled(DialogBoxId::TradingPost)) return;
		auto* dlg = game->get_dialog_box_manager().get_dialog_as<DialogBox_TradingPost>(DialogBoxId::TradingPost);
		if (dlg) dlg->receive_action_result(pkt);
	}
}
