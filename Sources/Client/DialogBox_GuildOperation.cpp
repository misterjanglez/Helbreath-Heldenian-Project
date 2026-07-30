#include "DialogBox_GuildOperation.h"
#include "Game.h"
#include "UITheme.h"
#include "lan_eng.h"

using namespace hb::client::sprite_id;

DialogBox_GuildOperation::DialogBox_GuildOperation(CGame* game)
	: IDialogBox(DialogBoxId::GuildOperation, game)
{
	set_default_rect(497, 57, 295, 346);
}

void DialogBox_GuildOperation::on_draw()
{
	short sX = m_x;
	short sY = m_y;

	m_game->draw_new_dialog_box(InterfaceNdGame2, sX, sY, 0);
	hb::client::ui_theme::header(sX, sY, m_size_x, UI_TITLE_GUILD_MENU);
}

bool DialogBox_GuildOperation::on_click()
{
	return false;
}

void DialogBox_GuildOperation::put(const char* /*name*/, char /*op_mode*/)
{
}

void DialogBox_GuildOperation::shift()
{
}

void DialogBox_GuildOperation::reset()
{
}
