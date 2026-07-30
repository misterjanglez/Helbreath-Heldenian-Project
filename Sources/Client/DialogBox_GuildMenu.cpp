#include "DialogBox_GuildMenu.h"
#include "Game.h"
#include "UITheme.h"
#include "lan_eng.h"

using namespace hb::client::sprite_id;

DialogBox_GuildMenu::DialogBox_GuildMenu(CGame* game)
	: IDialogBox(DialogBoxId::GuildMenu, game)
{
	set_default_rect(497, 57, 258, 339);
	m_can_close_on_right_click = true;
}

void DialogBox_GuildMenu::on_draw()
{
	short sX = m_x;
	short sY = m_y;

	m_game->draw_new_dialog_box(InterfaceNdGame2, sX, sY, 2);
	hb::client::ui_theme::header(sX, sY, m_size_x, UI_TITLE_GUILD_MENU);
}

bool DialogBox_GuildMenu::on_click()
{
	return false;
}

bool DialogBox_GuildMenu::on_enable(int type, int64_t v1, int v2, const char* string)
{
	return true;
}

bool DialogBox_GuildMenu::on_disable()
{
	return true;
}
