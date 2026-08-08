// DialogBox_GuildMenu.cpp: the guild hub (#124). See the header for the map.
//
// Main-menu rows are built once per event and walked by draw and click
// alike; the ceremony modes keep the pre-gut prose (lan_eng's GUILDMENU
// strings) where the flow survived §3.1.1 — create, disband, the ticket
// purchases — and the new panels (Donate, Treasury) read the cached
// PacketResponseGuildInfo answer, re-requested on mode entry and re-fetched
// from on_update whenever a handler marks it stale.
//
//////////////////////////////////////////////////////////////////////

#include "DialogBox_GuildMenu.h"

#include <cstdlib>
#include <format>
#include <string>

#include "AudioManager.h"
#include "Game.h"
#include "GuildManager.h"
#include "Item/ItemEnums.h"
#include "PacketSendHelpers.h"
#include "Screen_OnGame.h"
#include "TextFieldRenderer.h"
#include "TextInputManager.h"
#include "UITheme.h"
#include "lan_eng.h"

using namespace hb::shared::net;
namespace theme = hb::client::ui_theme;
namespace guild = hb::shared::guild;

namespace
{
	// The two ticket ceremonies differ only in item and prose; the y column
	// is shared so the two pages cannot drift apart.
	struct ticket_desc
	{
		int item_id;
		const char* item_name;
		const char* prose[5];
	};
	constexpr ticket_desc tickets[2] = {
		{ hb::shared::item::ItemId::GuildSecessionTicket, "GuildSecessionTicket",
			{ DRAW_DIALOGBOX_GUILDMENU38, DRAW_DIALOGBOX_GUILDMENU39,
			  DRAW_DIALOGBOX_GUILDMENU40, DRAW_DIALOGBOX_GUILDMENU41,
			  DRAW_DIALOGBOX_GUILDMENU42 } },
		{ hb::shared::item::ItemId::GuildAdmissionTicket, "GuildAdmissionTicket",
			{ DRAW_DIALOGBOX_GUILDMENU32, DRAW_DIALOGBOX_GUILDMENU33,
			  DRAW_DIALOGBOX_GUILDMENU34, DRAW_DIALOGBOX_GUILDMENU35,
			  DRAW_DIALOGBOX_GUILDMENU36 } },
	};
	constexpr int ticket_prose_y[5] = { 100, 115, 130, 145, 175 };
	constexpr int ticket_price_line = 1;   // the "{} Gold" row

	struct lane_desc
	{
		uint8_t lane;
		const char* label;
	};
	constexpr lane_desc donate_lanes[3] = {
		{ guild::guild_donation_lane::gold, UI_GUILD_LANE_GOLD },
		{ guild::guild_donation_lane::enemy_kills, UI_GUILD_LANE_EK },
		{ guild::guild_donation_lane::contribution, UI_GUILD_LANE_CONTRIB },
	};
}

DialogBox_GuildMenu::DialogBox_GuildMenu(CGame* game)
	: IDialogBox(DialogBoxId::GuildMenu, game)
{
	set_default_rect(497, 57, 258, 339);
	m_can_close_on_right_click = true;
}

void DialogBox_GuildMenu::play_click_sound() const
{
	audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
}

guild_manager& DialogBox_GuildMenu::guild_mgr() const
{
	return m_game->on_game()->get_guild_manager();
}

//----------------------------------------------------------------------
// The state-gated main menu
//----------------------------------------------------------------------

int DialogBox_GuildMenu::build_links(link_row (&rows)[8]) const
{
	constexpr auto none = DialogBoxId::None;
	const CPlayer& p = player();
	const bool crusade = m_game->on_game()->m_is_crusade_mode;
	int n = 0;

	if (!p.in_guild())
	{
		const bool can_create = p.m_level >= guild::guild_create_min_level
			&& p.m_charisma >= guild::guild_create_min_charisma
			&& p.m_citizen && !crusade;
		rows[n++] = { mode::create_input, none, DRAW_DIALOGBOX_GUILDMENU1, can_create };
		rows[n++] = { mode::apply_input, none, UI_GUILD_LINK_APPLY, true };
		rows[n++] = { mode::admission_ticket, none, DRAW_DIALOGBOX_GUILDMENU7, true };
		return n;
	}

	const bool master = p.m_guild_rank == guild::guild_rank::guildmaster;
	const bool approver = (p.m_guild_permission_mask
		& (guild::guild_permission::approve_join
			| guild::guild_permission::approve_leave)) != 0;

	rows[n++] = { mode::main_menu, DialogBoxId::GuildRoster, UI_GUILD_LINK_ROSTER, true };
	if (approver)
		rows[n++] = { mode::main_menu, DialogBoxId::GuildOperation, UI_GUILD_LINK_REQUESTS, true };
	rows[n++] = { mode::donate, none, UI_GUILD_LINK_DONATE, true };
	rows[n++] = { mode::treasury, none, UI_GUILD_LINK_TREASURY, true };
	if (!master)
	{
		rows[n++] = { mode::secession_ticket, none, DRAW_DIALOGBOX_GUILDMENU9, true };
		rows[n++] = { mode::leave_confirm, none, UI_GUILD_LINK_LEAVE, true };
		rows[n++] = { mode::self_exit_confirm, none, UI_GUILD_LINK_SELF_EXIT, !crusade };
	}
	else
		rows[n++] = { mode::disband_confirm, none, DRAW_DIALOGBOX_GUILDMENU4, !crusade };
	return n;
}

void DialogBox_GuildMenu::draw_main_menu(short sX, short sY)
{
	const CPlayer& p = player();
	if (p.in_guild())
	{
		put_aligned_string(sX, sX + m_size_x, sY + 36, p.m_guild_name.c_str(),
			theme::palette::value_hi);
		put_aligned_string(sX, sX + m_size_x, sY + 54,
			std::format(UI_GUILD_LEVEL_RANK, p.m_guild_level,
				p.m_guild_rank_title).c_str(),
			GameColors::UILabel);
	}
	else
	{
		put_aligned_string(sX, sX + m_size_x, sY + 44, UI_GUILD_NO_GUILD,
			GameColors::UIDisabled);
	}
	theme::separator(sX + 14, sY + 74, m_size_x - 28);

	link_row rows[8];
	const int count = build_links(rows);
	for (int i = 0; i < count; i++)
	{
		const ui_rect band = link_band.at_y(link_top + i * link_pitch);
		const auto& color = !rows[i].enabled ? GameColors::UIDisabled
			: mouse_in(band) ? GameColors::UIWhite : GameColors::UIMagicBlue;
		put_aligned_string(sX, sX + m_size_x, sY + band.y + 2, rows[i].label, color);
	}

	draw_button(sX, sY, btn_right, UI_BTN_OK);
}

bool DialogBox_GuildMenu::click_main_menu()
{
	link_row rows[8];
	const int count = build_links(rows);
	for (int i = 0; i < count; i++)
	{
		if (!mouse_in(link_band.at_y(link_top + i * link_pitch)))
			continue;
		if (!rows[i].enabled)
			return false;
		play_click_sound();
		if (rows[i].dialog != DialogBoxId::None)
		{
			enable_dialog_box(rows[i].dialog);
			disable_this_dialog();
		}
		else
			enter_mode(rows[i].target);
		return true;
	}

	if (mouse_in(btn_right))
	{
		play_click_sound();
		disable_this_dialog();
		return true;
	}
	return false;
}

//----------------------------------------------------------------------
// Mode plumbing
//----------------------------------------------------------------------

void DialogBox_GuildMenu::enter_mode(mode next)
{
	unbind_text_input();
	m_mode = next;
	m_input.clear();
	m_donate_lane = guild::guild_donation_lane::gold;
	// The two info panels open on a fresh answer; on_update keeps it fresh
	// afterwards via the staleness flag.
	if (next == mode::donate || next == mode::treasury)
		guild_mgr().send_info_request();
}

std::optional<DialogBox_GuildMenu::field_desc>
DialogBox_GuildMenu::input_field() const
{
	constexpr auto name_len =
		static_cast<unsigned char>(guild::max_guild_name_length);
	switch (m_mode)
	{
	case mode::create_input:
	case mode::apply_input:
		return field_desc{ 145, name_len, hb::client::guild_name_allowed_chars };
	case mode::disband_confirm:
		return field_desc{ 215, name_len, hb::client::guild_name_allowed_chars };
	case mode::donate:
	case mode::treasury:
		return field_desc{ amount_field_y, 10, hb::client::digits_only };
	default:
		return std::nullopt;
	}
}

int64_t DialogBox_GuildMenu::parsed_amount() const
{
	if (m_input.empty())
		return 0;
	return std::strtoll(m_input.c_str(), nullptr, 10);
}

void DialogBox_GuildMenu::clear_amount()
{
	m_input.clear();
	text_input_manager::get().clear_input();
}

// The field chrome; the text_input_manager renders the content on top.
void DialogBox_GuildMenu::draw_text_field(short sX, short sY, int field_y)
{
	theme::content_frame(sX + text_field_x, sY + field_y, text_field_w,
		text_field_h);
}

//----------------------------------------------------------------------
// Ceremony modes
//----------------------------------------------------------------------

void DialogBox_GuildMenu::draw_prompt(short sX, short sY, const char* prompt,
	const char* hint1, const char* hint2)
{
	put_aligned_string(sX, sX + m_size_x, sY + 118, prompt, GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 145, hint1, GameColors::UIDisabled);
	put_aligned_string(sX, sX + m_size_x, sY + 160, hint2, GameColors::UIDisabled);
	draw_button(sX, sY, btn_left, UI_BTN_YES);
	draw_button(sX, sY, btn_right, UI_BTN_NO);
}

void DialogBox_GuildMenu::draw_field_prompt(short sX, short sY,
	const char* prompt, const char* hint1, const char* hint2,
	const char* submit_label, bool submit_enabled)
{
	put_aligned_string(sX, sX + m_size_x, sY + 118, prompt, GameColors::UILabel);
	draw_text_field(sX, sY, 145);
	if (hint1 != nullptr)
		put_aligned_string(sX, sX + m_size_x, sY + 175, hint1,
			GameColors::UIDisabled);
	if (hint2 != nullptr)
		put_aligned_string(sX, sX + m_size_x, sY + 190, hint2,
			GameColors::UIDisabled);
	draw_button(sX, sY, btn_left, submit_label, submit_enabled);
	draw_button(sX, sY, btn_right, UI_BTN_CANCEL);
}

void DialogBox_GuildMenu::draw_disband_confirm(short sX, short sY)
{
	put_aligned_string(sX, sX + m_size_x, sY + 88, DRAW_DIALOGBOX_GUILDMENU24,
		GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 104,
		player().m_guild_name.c_str(), theme::palette::value_hi);
	put_aligned_string(sX, sX + m_size_x, sY + 126, DRAW_DIALOGBOX_GUILDMENU25,
		GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 141, DRAW_DIALOGBOX_GUILDMENU26,
		GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 156, DRAW_DIALOGBOX_GUILDMENU27,
		GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 171, DRAW_DIALOGBOX_GUILDMENU28,
		GameColors::UILabel);
	put_aligned_string(sX, sX + m_size_x, sY + 195, UI_GUILD_DISBAND_RETYPE,
		GameColors::UILabel);
	draw_text_field(sX, sY, 215);
	draw_button(sX, sY, btn_left, UI_BTN_YES, m_input == player().m_guild_name);
	draw_button(sX, sY, btn_right, UI_BTN_NO);
}

void DialogBox_GuildMenu::draw_ticket(short sX, short sY, bool admission)
{
	const ticket_desc& ticket = tickets[admission ? 1 : 0];
	CItem* cfg = m_game->get_item_config(ticket.item_id);
	const int price = cfg != nullptr ? static_cast<int>(cfg->m_sell_price) : 0;

	for (int i = 0; i < 5; i++)
	{
		const std::string line = i == ticket_price_line
			? std::vformat(ticket.prose[i], std::make_format_args(price))
			: std::string(ticket.prose[i]);
		put_aligned_string(sX, sX + m_size_x, sY + ticket_prose_y[i],
			line.c_str(), GameColors::UILabel);
	}
	draw_button(sX, sY, btn_left, UI_BTN_PURCHASE);
	draw_button(sX, sY, btn_right, UI_BTN_CANCEL);
}

void DialogBox_GuildMenu::purchase_ticket(bool admission)
{
	const ticket_desc& ticket = tickets[admission ? 1 : 0];
	auto pkt = hb::net::make_common_command_str(CommonType::ReqPurchaseItem,
		player().m_player_x, player().m_player_y);
	pkt.v1 = 1;
	pkt.v2 = ticket.item_id;
	std::snprintf(pkt.text, sizeof(pkt.text), "%s", ticket.item_name);
	send_game_packet(pkt);
}

//----------------------------------------------------------------------
// Donate / Treasury
//----------------------------------------------------------------------

void DialogBox_GuildMenu::draw_donate(short sX, short sY)
{
	const guild_manager& manager = guild_mgr();
	const auto& info = manager.info();
	const bool have = manager.info_valid();

	// Re-format the lane totals only when a new info answer landed.
	if (m_donate_info_version != manager.info_version())
	{
		m_donate_info_version = manager.info_version();
		const int64_t burned[3] = { info.burned_gold, info.burned_ek,
			info.burned_contribution };
		const int64_t next[3] = { info.next_gold, info.next_ek,
			info.next_contribution };
		// At the curve's top every next_* threshold is zero (§3.1.2); below
		// it each lane reads "burned so far / next level's requirement".
		m_donate_at_top = have && next[0] == 0 && next[1] == 0 && next[2] == 0;
		for (int i = 0; i < 3; i++)
		{
			m_donate_totals[i] = m_donate_at_top
				? m_game->format_comma_number(static_cast<uint64_t>(burned[i]))
				: std::format("{} / {}",
					m_game->format_comma_number(static_cast<uint64_t>(burned[i])),
					m_game->format_comma_number(static_cast<uint64_t>(next[i])));
		}
	}

	put_aligned_string(sX, sX + m_size_x, sY + 36,
		std::format(UI_GUILD_NAME_LEVEL, player().m_guild_name,
			have ? info.level : player().m_guild_level).c_str(),
		theme::palette::value_hi);

	if (have && info.donations_enabled == 0)
		put_aligned_string(sX, sX + m_size_x, sY + 58, UI_GUILD_DONATE_DISABLED,
			GameColors::UIDarkRed);
	else
		put_aligned_string(sX, sX + m_size_x, sY + 58, UI_GUILD_DONATE_BURN_HINT,
			GameColors::UIDisabled);

	put_aligned_string(sX, sX + m_size_x, sY + 88,
		m_donate_at_top ? UI_GUILD_AT_MAX_LEVEL : UI_GUILD_NEXT_LEVEL_AT,
		GameColors::UILabel);

	for (int i = 0; i < 3; i++)
	{
		const ui_rect band = lane_band.at_y(lane_top + i * lane_pitch);
		const bool picked = m_donate_lane == donate_lanes[i].lane;
		const auto& color = picked ? theme::palette::value_hi
			: mouse_in(band) ? GameColors::UIWhite : GameColors::UIMagicBlue;
		if (picked)
			theme::label(sX + 20, sY + band.y, ">", theme::palette::value_hi);
		theme::label(sX + 32, sY + band.y, donate_lanes[i].label, color);
		theme::value(sX + 92, sY + band.y, 142, m_donate_totals[i].c_str(),
			theme::palette::value);
	}

	theme::separator(sX + 14, sY + 200, m_size_x - 28);
	theme::label(sX + text_field_x, sY + 210, UI_GUILD_AMOUNT);
	draw_text_field(sX, sY, amount_field_y);

	const bool can_send = have && info.donations_enabled != 0
		&& parsed_amount() > 0;
	draw_button(sX, sY, btn_left, UI_BTN_DONATE, can_send);
	draw_button(sX, sY, btn_right, UI_BTN_BACK);
}

bool DialogBox_GuildMenu::click_donate()
{
	for (int i = 0; i < 3; i++)
	{
		if (mouse_in(lane_band.at_y(lane_top + i * lane_pitch)))
		{
			m_donate_lane = donate_lanes[i].lane;
			play_click_sound();
			return true;
		}
	}

	guild_manager& manager = guild_mgr();
	const int64_t amount = parsed_amount();
	if (mouse_in(btn_left) && manager.info_valid()
		&& manager.info().donations_enabled != 0 && amount > 0)
	{
		manager.send_donate(m_donate_lane, amount);
		play_click_sound();
		clear_amount();
		return true;
	}
	if (mouse_in(btn_right))
	{
		play_click_sound();
		enter_mode(mode::main_menu);
		return true;
	}
	return false;
}

void DialogBox_GuildMenu::draw_treasury(short sX, short sY)
{
	const guild_manager& manager = guild_mgr();
	theme::label_value(sX + 24, sX + 92, 142, sY + 44,
		UI_GUILD_TREASURY_BALANCE,
		manager.info_valid()
			? m_game->format_comma_number(
				static_cast<uint64_t>(manager.info().treasury_gold)).c_str()
			: "-",
		theme::palette::value_hi);
	put_aligned_string(sX, sX + m_size_x, sY + 72, UI_GUILD_TREASURY_HINT,
		GameColors::UIDisabled);

	theme::separator(sX + 14, sY + 200, m_size_x - 28);
	theme::label(sX + text_field_x, sY + 210, UI_GUILD_AMOUNT);
	draw_text_field(sX, sY, amount_field_y);

	const bool amount_ok = parsed_amount() > 0;
	const bool can_withdraw = (player().m_guild_permission_mask
		& guild::guild_permission::treasury_withdraw) != 0;
	draw_button(sX, sY, btn_deposit, UI_BTN_DEPOSIT, amount_ok);
	draw_button(sX, sY, btn_withdraw, UI_BTN_WITHDRAW, amount_ok && can_withdraw);
	draw_button(sX, sY, btn_treasury_back, UI_BTN_BACK);
}

bool DialogBox_GuildMenu::click_treasury()
{
	guild_manager& manager = guild_mgr();
	const int64_t amount = parsed_amount();
	const bool can_withdraw = (player().m_guild_permission_mask
		& guild::guild_permission::treasury_withdraw) != 0;

	if (mouse_in(btn_deposit) && amount > 0)
	{
		manager.send_treasury(guild::guild_treasury_op::deposit, amount);
		play_click_sound();
		clear_amount();
		return true;
	}
	if (mouse_in(btn_withdraw) && amount > 0 && can_withdraw)
	{
		manager.send_treasury(guild::guild_treasury_op::withdraw, amount);
		play_click_sound();
		clear_amount();
		return true;
	}
	if (mouse_in(btn_treasury_back))
	{
		play_click_sound();
		enter_mode(mode::main_menu);
		return true;
	}
	return false;
}

//----------------------------------------------------------------------
// The frame
//----------------------------------------------------------------------

void DialogBox_GuildMenu::on_draw()
{
	const short sX = m_x;
	const short sY = m_y;

	theme::panel(sX, sY, m_size_x, m_size_y);
	theme::header(sX, sY, m_size_x, UI_TITLE_GUILD_MENU);

	switch (m_mode)
	{
	case mode::main_menu:        draw_main_menu(sX, sY); break;
	case mode::create_input:
		draw_field_prompt(sX, sY, DRAW_DIALOGBOX_GUILDMENU18,
			UI_GUILD_NAME_RULES, nullptr, UI_BTN_CREATE,
			guild::is_valid_guild_name(m_input));
		break;
	case mode::apply_input:
		draw_field_prompt(sX, sY, UI_GUILD_APPLY_PROMPT,
			UI_GUILD_APPLY_HINT1, UI_GUILD_APPLY_HINT2, UI_BTN_OK,
			!m_input.empty());
		break;
	case mode::disband_confirm:  draw_disband_confirm(sX, sY); break;
	case mode::admission_ticket: draw_ticket(sX, sY, true); break;
	case mode::secession_ticket: draw_ticket(sX, sY, false); break;
	case mode::leave_confirm:
		draw_prompt(sX, sY, UI_GUILD_LEAVE_PROMPT, UI_GUILD_LEAVE_HINT1,
			UI_GUILD_LEAVE_HINT2);
		break;
	case mode::self_exit_confirm:
		draw_prompt(sX, sY, UI_GUILD_SELF_EXIT_PROMPT,
			UI_GUILD_SELF_EXIT_HINT1, UI_GUILD_SELF_EXIT_HINT2);
		break;
	case mode::donate:           draw_donate(sX, sY); break;
	case mode::treasury:         draw_treasury(sX, sY); break;
	}

	if (const auto field = input_field())
		bind_text_input(m_x + text_field_x + 6, m_y + field->y + 3,
			field->max_len, m_input, field->filter);
}

bool DialogBox_GuildMenu::on_click()
{
	guild_manager& manager = guild_mgr();
	switch (m_mode)
	{
	case mode::main_menu:
		return click_main_menu();
	case mode::create_input:
		return click_footer(guild::is_valid_guild_name(m_input),
			[&] { manager.send_create(m_input.c_str()); });
	case mode::apply_input:
		return click_footer(!m_input.empty(),
			[&] { manager.send_join(m_input.c_str()); });
	case mode::disband_confirm:
		return click_footer(m_input == player().m_guild_name,
			[&] { manager.send_disband(m_input.c_str()); });
	case mode::admission_ticket:
		return click_footer(true, [&] { purchase_ticket(true); });
	case mode::secession_ticket:
		return click_footer(true, [&] { purchase_ticket(false); });
	case mode::leave_confirm:
		return click_footer(true, [&] { manager.send_leave(); });
	case mode::self_exit_confirm:
		return click_footer(true, [&] { manager.send_self_exit(); });
	case mode::donate:
		return click_donate();
	case mode::treasury:
		return click_treasury();
	}
	return false;
}

void DialogBox_GuildMenu::on_update()
{
	// A handler marked the info answer stale while one of the panels shows
	// it; nothing else in this dialog reads the answer.
	if ((m_mode == mode::donate || m_mode == mode::treasury)
		&& guild_mgr().info_stale())
	{
		guild_mgr().send_info_request();
	}
}

bool DialogBox_GuildMenu::on_enable(int type, int64_t v1, int v2, const char* string)
{
	enter_mode(mode::main_menu);
	return true;
}

bool DialogBox_GuildMenu::on_disable()
{
	unbind_text_input();
	m_mode = mode::main_menu;
	return true;
}
