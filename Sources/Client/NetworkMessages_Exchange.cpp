#include "Game.h"
#include "InventoryManager.h"
#include "NetworkMessageManager.h"
#include "Packet/SharedPackets.h"
#include "lan_eng.h"
#include "DialogBoxIDs.h"
#include "DialogBox_Exchange.h"
#include <cstring>
#include <cstdio>

namespace NetworkMessageHandlers {
	void HandleExchangeItemComplete(CGame* game, char* data)
	{
		game->add_event_list(NOTIFYMSG_EXCHANGEITEM_COMPLETE1, 10);
		game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::Exchange);
		//Snoopy: MultiTrade
		game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::ConfirmExchange);
		game->play_game_sound('E', 23, 5);
	}

	void HandleCancelExchangeItem(CGame* game, char* data)
	{
		game->play_game_sound('E', 24, 5);
		game->add_event_list(NOTIFYMSG_CANCEL_EXCHANGEITEM1, 10);
		game->add_event_list(NOTIFYMSG_CANCEL_EXCHANGEITEM2, 10);

		// Explicitly clear item disabled flags before disabling dialogs.
		// disable_dialog_box(Exchange) also clears these, but if exchange info
		// was partially cleared by item removal, flags could get stuck.
		auto* exDlg = game->m_dialog_box_manager.get_dialog_as<DialogBox_Exchange>(DialogBoxId::Exchange);
		for (int i = 0; i < 4; i++)
		{
			int slot = exDlg->m_slots[i].inv_slot;
			if (slot >= 0 && slot < hb::shared::limits::MaxItems)
				inventory_manager::get().unlock_item(slot);
		}

		//Snoopy: MultiTrade
		game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::ConfirmExchange);
		game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::Exchange);
	}
} // namespace NetworkMessageHandlers
