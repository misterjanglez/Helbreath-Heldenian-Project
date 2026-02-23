#include "DialogBox_GuildOperation.h"
#include "Game.h"
#include "lan_eng.h"
#include <string>
#include "IInput.h"

using namespace hb::shared::net;
using namespace hb::client::sprite_id;
DialogBox_GuildOperation::DialogBox_GuildOperation(CGame* game)
	: IDialogBox(DialogBoxId::GuildOperation, game)
{
	set_default_rect(497 , 57 , 295, 346);
}

void DialogBox_GuildOperation::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;

	m_game->draw_new_dialog_box(InterfaceNdGame2, sX, sY, 0);
	m_game->draw_new_dialog_box(InterfaceNdText, sX, sY, 19);

	switch (m_op_list[0].op_mode) {
	case 1:
		draw_join_request(sX, sY);
		break;
	case 2:
		draw_dismiss_request(sX, sY);
		break;
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		draw_info_message(sX, sY, mouse_x, mouse_y, m_op_list[0].op_mode);
		break;
	}
}

void DialogBox_GuildOperation::draw_join_request(short sX, short sY)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION1);
	put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
	put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
	put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION2);
	put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION3);
	put_aligned_string(sX + 24, sX + 248, sY + 120, DRAW_DIALOGBOX_GUILD_OPERATION4);
	put_aligned_string(sX + 24, sX + 248, sY + 160, DRAW_DIALOGBOX_GUILD_OPERATION5, GameColors::UILabel);

	if ((mouse_x >= sX + ui_layout::left_btn_x) && (mouse_x <= sX + ui_layout::left_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::left_btn_x, sY + ui_layout::btn_y, 33);
	else m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::left_btn_x, sY + ui_layout::btn_y, 32);

	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 35);
	else m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 34);
}

void DialogBox_GuildOperation::draw_dismiss_request(short sX, short sY)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION6);
	put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
	put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
	put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION7);
	put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION8);
	put_aligned_string(sX + 24, sX + 248, sY + 120, DRAW_DIALOGBOX_GUILD_OPERATION9);
	put_aligned_string(sX + 24, sX + 248, sY + 160, DRAW_DIALOGBOX_GUILD_OPERATION10, GameColors::UILabel);

	if ((mouse_x >= sX + ui_layout::left_btn_x) && (mouse_x <= sX + ui_layout::left_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::left_btn_x, sY + ui_layout::btn_y, 33);
	else m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::left_btn_x, sY + ui_layout::btn_y, 32);

	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 35);
	else m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 34);
}

void DialogBox_GuildOperation::draw_info_message(short sX, short sY, short mouse_x, short mouse_y, int mode)
{
	switch (mode) {
	case 3:
		put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION11);
		put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
		put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
		put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION12);
		put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION13);
		break;
	case 4:
		put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION14);
		put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
		put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
		put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION15);
		put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION16);
		break;
	case 5:
		put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION17);
		put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
		put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
		put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION18);
		put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION19);
		put_aligned_string(sX + 24, sX + 248, sY + 120, DRAW_DIALOGBOX_GUILD_OPERATION20);
		break;
	case 6:
		put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION21);
		put_aligned_string(sX + 24, sX + 248, sY + 65, m_op_list[0].name.c_str(), GameColors::UILabel);
		put_aligned_string(sX + 24, sX + 248, sY + 78, "____________________", GameColors::UIBlack);
		put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION22);
		put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION23);
		break;
	case 7:
		put_aligned_string(sX + 24, sX + 248, sY + 50, DRAW_DIALOGBOX_GUILD_OPERATION24);
		put_aligned_string(sX + 24, sX + 248, sY + 90, DRAW_DIALOGBOX_GUILD_OPERATION25);
		put_aligned_string(sX + 24, sX + 248, sY + 105, DRAW_DIALOGBOX_GUILD_OPERATION26);
		put_aligned_string(sX + 24, sX + 248, sY + 120, DRAW_DIALOGBOX_GUILD_OPERATION27);
		break;
	}

	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) && (mouse_y > sY + ui_layout::btn_y) && (mouse_y < sY + ui_layout::btn_y + ui_layout::btn_size_y))
		m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 1);
	else m_game->draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 0);
}

bool DialogBox_GuildOperation::on_click()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;
	std::string name20;


	switch (m_op_list[0].op_mode) {
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) && (mouse_y > sY + ui_layout::btn_y) && (mouse_y < sY + ui_layout::btn_y + ui_layout::btn_size_y)) {
			shift();
			if (m_op_list[0].op_mode == 0)
				m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::GuildOperation);
			return true;
		}
		return false;
	}

	// Approve button
	if ((mouse_x >= sX + ui_layout::left_btn_x) && (mouse_x <= sX + ui_layout::left_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y)) {
		play_sound_effect('E', 14, 5);

		switch (m_op_list[0].op_mode) {
		case 1:
			name20 = m_op_list[0].name;
			send_command(MsgId::CommandCommon, CommonType::JoinGuildApprove, 0, 0, 0, 0, name20.c_str());
			break;
		case 2:
			name20 = m_op_list[0].name;
			send_command(MsgId::CommandCommon, CommonType::DismissGuildApprove, 0, 0, 0, 0, name20.c_str());
			break;
		}
		shift();
		if (m_op_list[0].op_mode == 0)
			m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::GuildOperation);
		return true;
	}

	// Reject button
	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) && (mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y)) {
		play_sound_effect('E', 14, 5);

		switch (m_op_list[0].op_mode) {
		case 1:
			name20 = m_op_list[0].name;
			send_command(MsgId::CommandCommon, CommonType::JoinGuildReject, 0, 0, 0, 0, name20.c_str());
			break;
		case 2:
			name20 = m_op_list[0].name;
			send_command(MsgId::CommandCommon, CommonType::DismissGuildReject, 0, 0, 0, 0, name20.c_str());
			break;
		}
		shift();
		if (m_op_list[0].op_mode == 0)
			m_game->m_dialog_box_manager.disable_dialog_box(DialogBoxId::GuildOperation);
		return true;
	}

	return false;
}

void DialogBox_GuildOperation::put(const char* name, char op_mode)
{
	for (int i = 0; i < 100; i++)
		if (m_op_list[i].op_mode == 0)
		{
			m_op_list[i].op_mode = op_mode;
			m_op_list[i].name.assign(name, strnlen(name, 20));
			return;
		}
}

void DialogBox_GuildOperation::shift()
{
	m_op_list[0].op_mode = 0;

	for (int i = 1; i < 100; i++)
		if ((m_op_list[i - 1].op_mode == 0) && (m_op_list[i].op_mode != 0)) {
			m_op_list[i - 1].op_mode = m_op_list[i].op_mode;
			m_op_list[i - 1].name = m_op_list[i].name;

			m_op_list[i].op_mode = 0;
		}
}

void DialogBox_GuildOperation::reset()
{
	for (int i = 0; i < 100; i++)
		m_op_list[i].op_mode = 0;
}
