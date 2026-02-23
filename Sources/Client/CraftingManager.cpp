// crafting_manager.cpp: Handles client-side crafting/portion network messages.
// Extracted from NetworkMessages_Crafting.cpp (Phase B2).

#include "CraftingManager.h"
#include "DialogBox_Manufacture.h"
#include "Game.h"
#include "ObjectIDRange.h"
#include "Packet/SharedPackets.h"
#include "lan_eng.h"
#include "DialogBoxIDs.h"
#include <cstdio>
#include <cstring>

void crafting_manager::handle_crafting_success(char* data)
{
	if (!m_game) return;
	m_game->m_player->m_contribution -= m_game->m_contribution_price;
	m_game->m_contribution_price = 0;
	m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::Noticement);
	m_game->add_event_list(NOTIFY_MSG_HANDLER42, 10);		// "Item manufacture success!"
	m_game->play_game_sound('E', 23, 5);
	switch (m_game->m_player->m_player_type) {
	case 1:
	case 2:
	case 3:
		m_game->play_game_sound('C', 21, 0);
		break;
	case 4:
	case 5:
	case 6:
		m_game->play_game_sound('C', 22, 0);
		break;
	}
}

void crafting_manager::handle_crafting_fail(char* data)
{
	if (!m_game) return;
	int v1;
	m_game->m_contribution_price = 0;
	{
		const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyCraftingFail>(
			data, sizeof(hb::net::PacketNotifyCraftingFail));
		if (!pkt) return;
		v1 = pkt->reason; // Error reason
	}
	switch (v1) {
	case 1:
		m_game->add_event_list(DEF_MSG_NOTIFY_CRAFTING_NO_PART, 10);		// "There is not enough material"
		m_game->play_game_sound('E', 24, 5);
		break;
	case 2:
		m_game->add_event_list(DEF_MSG_NOTIFY_CRAFTING_NO_CONTRIB, 10);	// "There is not enough Contribution Point"
		m_game->play_game_sound('E', 24, 5);
		break;
	default:
	case 3:
		m_game->add_event_list(DEF_MSG_NOTIFY_CRAFTING_FAILED, 10);		// "Crafting failed"
		m_game->play_game_sound('E', 24, 5);
		break;
	}
}

void crafting_manager::handle_build_item_success(char* data)
{
	if (!m_game) return;
	short v1, v2;
	m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::Manufacture);
	{
		const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyBuildItemResult>(
			data, sizeof(hb::net::PacketNotifyBuildItemResult));
		if (!pkt) return;
		v1 = pkt->item_id;
		v2 = pkt->item_count;
	}
	if (hb::shared::object_id::is_player_id(v1))
	{
		m_game->m_dialog_box_manager.enable_dialog_box(DialogBoxId::Manufacture, 6, 1, v1, 0);
		m_game->m_dialog_box_manager.get_dialog_as<DialogBox_Manufacture>(DialogBoxId::Manufacture)->m_slot_1 = v2;
	}
	else
	{
		m_game->m_dialog_box_manager.enable_dialog_box(DialogBoxId::Manufacture, 6, 1, -1 * (v1 - 10000), 0);
		m_game->m_dialog_box_manager.get_dialog_as<DialogBox_Manufacture>(DialogBoxId::Manufacture)->m_slot_1 = v2;
	}
	m_game->add_event_list(NOTIFY_MSG_HANDLER42, 10);
	m_game->play_game_sound('E', 23, 5);
	switch (m_game->m_player->m_player_type) {
	case 1:
	case 2:
	case 3:
		m_game->play_game_sound('C', 21, 0);
		break;

	case 4:
	case 5:
	case 6:
		m_game->play_game_sound('C', 22, 0);
		break;
	}
}

void crafting_manager::handle_build_item_fail(char* data)
{
	if (!m_game) return;
	m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::Manufacture);
	m_game->m_dialog_box_manager.enable_dialog_box(DialogBoxId::Manufacture, 6, 0, 0);
	m_game->add_event_list(NOTIFY_MSG_HANDLER43, 10);
	m_game->play_game_sound('E', 24, 5);
}

void crafting_manager::handle_portion_success(char* data)
{
	if (!m_game) return;
	m_game->add_event_list(NOTIFY_MSG_HANDLER46, 10);
}

void crafting_manager::handle_portion_fail(char* data)
{
	if (!m_game) return;
	m_game->add_event_list(NOTIFY_MSG_HANDLER47, 10);
}

void crafting_manager::handle_low_portion_skill(char* data)
{
	if (!m_game) return;
	m_game->add_event_list(NOTIFY_MSG_HANDLER48, 10);
}

void crafting_manager::handle_no_matching_portion(char* data)
{
	if (!m_game) return;
	m_game->add_event_list(NOTIFY_MSG_HANDLER49, 10);
}
