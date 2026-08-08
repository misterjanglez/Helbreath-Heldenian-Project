// DialogBox_GuildRoster.cpp: the Roster surface (#124). See the header.
//
//////////////////////////////////////////////////////////////////////

#include "DialogBox_GuildRoster.h"

#include <algorithm>
#include <format>
#include <string>

#include "AudioManager.h"
#include "Game.h"
#include "Screen_OnGame.h"
#include "UITheme.h"
#include "lan_eng.h"

namespace theme = hb::client::ui_theme;
namespace guild = hb::shared::guild;

DialogBox_GuildRoster::DialogBox_GuildRoster(CGame* game)
	: IDialogBox(DialogBoxId::GuildRoster, game)
{
	set_default_rect(170, 60, panel_w, panel_h);
	m_can_close_on_right_click = true;
}

void DialogBox_GuildRoster::play_click_sound() const
{
	audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
}

void DialogBox_GuildRoster::refresh()
{
	guild_manager& manager = m_game->on_game()->get_guild_manager();
	manager.send_roster_request();
	manager.send_info_request();
}

//----------------------------------------------------------------------
// Data views
//----------------------------------------------------------------------

void DialogBox_GuildRoster::build_title_lines(title_line (&lines)[3]) const
{
	const guild_manager& manager = m_game->on_game()->get_guild_manager();
	const auto& info = manager.info();
	const bool have = manager.info_valid();
	const CPlayer& p = player();

	lines[0] = { guild::guild_title::commander, {},
		have ? info.commander_slots : 0, 0, false, false, {} };
	lines[1] = { guild::guild_title::huntmaster, {},
		have ? info.hunt_slots : 0, 0, false, false, {} };
	lines[2] = { guild::guild_title::raidmaster, {},
		have ? info.raid_slots : 0, 0, false, false, {} };

	for (const auto& row : manager.roster())
	{
		for (title_line& line : lines)
		{
			if (row.title != line.kind)
				continue;
			line.held++;
			if (!line.holders.empty())
				line.holders += ", ";
			line.holders += row.member;
		}
	}

	for (int i = 0; i < 3; i++)
	{
		title_line& line = lines[i];
		line.rect = title_btn.at_y(title_top + i * title_pitch);
		line.held_by_me = p.m_guild_title == line.kind;
		// Claiming ships always-on (§3.1.2 item 6): one Title per member,
		// a free slot, and the Commander seat additionally wants the
		// eligibility bit (ADR 0007).
		line.can_claim = p.m_guild_title == 0 && line.held < line.slots
			&& (line.kind != guild::guild_title::commander
				|| (p.m_guild_permission_mask
					& guild::guild_permission::commander_eligible) != 0);
		if (line.holders.size() > 26)
		{
			line.holders.resize(23);
			line.holders += "...";
		}
	}
}

const guild_manager::roster_row* DialogBox_GuildRoster::selected() const
{
	if (m_selected.empty())
		return nullptr;
	for (const auto& row : m_game->on_game()->get_guild_manager().roster())
	{
		if (m_selected == row.member)
			return &row;
	}
	return nullptr;
}

//----------------------------------------------------------------------
// Titles strip (ADR 0007's slot machine)
//----------------------------------------------------------------------

void DialogBox_GuildRoster::draw_titles(short sX, short sY)
{
	title_line lines[3];
	build_title_lines(lines);

	for (const title_line& line : lines)
	{
		theme::label(sX + 14, sY + line.rect.y,
			std::format(UI_GUILD_TITLE_SLOTS,
				guild::guild_title_display_name(line.kind),
				line.held, line.slots).c_str());
		theme::label(sX + 168, sY + line.rect.y, line.holders.c_str(),
			theme::palette::dim);

		if (line.held_by_me)
			draw_button(sX, sY, line.rect, UI_BTN_DROP);
		else
			draw_button(sX, sY, line.rect, UI_BTN_CLAIM, line.can_claim);
	}
}

bool DialogBox_GuildRoster::click_titles()
{
	title_line lines[3];
	build_title_lines(lines);

	for (const title_line& line : lines)
	{
		if (!mouse_in(line.rect))
			continue;
		guild_manager& manager = m_game->on_game()->get_guild_manager();
		if (line.held_by_me)
		{
			manager.send_title_drop();
			play_click_sound();
			return true;
		}
		if (line.can_claim)
		{
			manager.send_title_claim(line.kind);
			play_click_sound();
			return true;
		}
		return false;
	}
	return false;
}

//----------------------------------------------------------------------
// The member list
//----------------------------------------------------------------------

void DialogBox_GuildRoster::draw_list(short sX, short sY)
{
	const guild_manager& manager = m_game->on_game()->get_guild_manager();
	const auto& roster = manager.roster();

	theme::label(sX + col_member, sY + head_y, UI_GUILD_COL_MEMBER);
	theme::label(sX + col_rank, sY + head_y, UI_GUILD_COL_RANK);
	theme::label(sX + col_title, sY + head_y, UI_GUILD_COL_TITLE);
	theme::label(sX + col_donated, sY + head_y, UI_GUILD_COL_DONATED);
	theme::label(sX + col_status, sY + head_y, UI_GUILD_COL_STATUS);

	if (roster.empty())
	{
		put_aligned_string(sX, sX + panel_w, sY + list_top + 40,
			UI_GUILD_ROSTER_EMPTY, GameColors::UIDisabled);
		return;
	}

	const int max_scroll =
		std::max(0, static_cast<int>(roster.size()) - visible_rows);
	handle_vscroll(scroll_bar, max_scroll, m_scroll);

	const std::string& own_name = player().m_player_name;
	for (int i = 0; i < visible_rows; i++)
	{
		const std::size_t index = static_cast<std::size_t>(m_scroll + i);
		if (index >= roster.size())
			break;
		const auto& row = roster[index];
		const int y = list_top + i * row_pitch;

		const ui_rect band = row_band.at_y(y);
		const bool own = own_name == row.member;
		if (m_selected == row.member)
			m_game->m_Renderer->draw_rect_filled(sX + band.x, sY + band.y,
				band.w, band.h, theme::palette::button_hover);
		else if (mouse_in(band))
			m_game->m_Renderer->draw_rect_filled(sX + band.x, sY + band.y,
				band.w, band.h, theme::palette::button);

		theme::label(sX + col_member, sY + y, row.member,
			own ? theme::palette::value : theme::palette::label);
		theme::label(sX + col_rank, sY + y,
			guild::default_rank_title(row.rank));
		if (row.title != 0)
			theme::label(sX + col_title, sY + y,
				guild::guild_title_display_name(row.title),
				theme::palette::value_hi);
		theme::value(sX + col_donated, sY + y, col_donated_w,
			m_game->format_comma_number(
				static_cast<uint64_t>(row.donated_gold)).c_str());
		if (row.online != 0)
			theme::label(sX + col_status, sY + y, UI_GUILD_ONLINE,
				theme::palette::value);
		else
			theme::label(sX + col_status, sY + y, UI_GUILD_OFFLINE,
				theme::palette::dim);
	}

	if (max_scroll > 0)
	{
		const int track = scroll_bar.h;
		const int thumb = std::clamp(track * visible_rows
			/ static_cast<int>(roster.size()), row_pitch, track);
		theme::scrollbar(sX + scroll_bar.x, sY + scroll_bar.y, scroll_bar.w,
			track, (track - thumb) * m_scroll / max_scroll, thumb);
	}
}

bool DialogBox_GuildRoster::click_list()
{
	const auto& roster = m_game->on_game()->get_guild_manager().roster();
	for (int i = 0; i < visible_rows; i++)
	{
		const std::size_t index = static_cast<std::size_t>(m_scroll + i);
		if (index >= roster.size())
			break;
		if (!mouse_in(row_band.at_y(list_top + i * row_pitch)))
			continue;
		const char* member = roster[index].member;
		m_selected = (m_selected == member) ? std::string{} : member;
		m_confirm = action::none;
		play_click_sound();
		return true;
	}
	return false;
}

//----------------------------------------------------------------------
// Moderation actions (§3.1.1 UI-first; the server re-checks every rule)
//----------------------------------------------------------------------

void DialogBox_GuildRoster::act_on(action what, const char* target)
{
	guild_manager& manager = m_game->on_game()->get_guild_manager();
	switch (what)
	{
	case action::kick:        manager.send_kick(target); break;
	case action::promote:     manager.send_promote(target); break;
	case action::demote:      manager.send_demote(target); break;
	case action::strip_title: manager.send_title_strip(target); break;
	case action::transfer:    manager.send_transfer(target); break;
	case action::none:        break;
	}
}

// The action bar's rows, built once per event so the drawn buttons and the
// hit-tested ones cannot disagree. An action appears at all only when the
// actor's permissions surface it (permission-gated buttons), and is enabled
// only when the selected row qualifies for it.
int DialogBox_GuildRoster::build_action_buttons(action_button (&buttons)[5]) const
{
	const CPlayer& p = player();
	const auto* row = selected();
	const bool own_row = row != nullptr && p.m_player_name == row->member;
	const uint32_t mask = p.m_guild_permission_mask;
	const bool is_master = p.m_guild_rank == guild::guild_rank::guildmaster;

	struct candidate
	{
		action act;
		const char* label;
		bool shown;
		bool enabled;
	};
	const candidate all[] = {
		{ action::kick, UI_BTN_KICK,
			(mask & guild::guild_permission::kick) != 0,
			row != nullptr && !own_row && row->rank > p.m_guild_rank },
		{ action::promote, UI_BTN_PROMOTE,
			(mask & guild::guild_permission::promote_demote) != 0,
			row != nullptr && row->rank == guild::guild_rank::guildsman },
		{ action::demote, UI_BTN_DEMOTE,
			(mask & guild::guild_permission::promote_demote) != 0,
			row != nullptr && row->rank == guild::guild_rank::officer },
		{ action::strip_title, UI_BTN_STRIP,
			(mask & guild::guild_permission::title_manage) != 0,
			row != nullptr && row->title != 0 },
		{ action::transfer, UI_BTN_TRANSFER,
			is_master,
			row != nullptr && !own_row && row->online != 0 },
	};

	int n = 0;
	int x = 14;
	for (const candidate& btn : all)
	{
		if (!btn.shown)
			continue;
		buttons[n] = { btn.act, btn.label,
			{ x, action_y, action_w, 20 }, btn.enabled };
		x += action_w + action_gap;
		n++;
	}
	return n;
}

void DialogBox_GuildRoster::draw_actions(short sX, short sY)
{
	if (m_confirm != action::none)
	{
		const std::string prompt = m_confirm == action::kick
			? std::format(UI_GUILD_CONFIRM_KICK, m_selected)
			: std::format(UI_GUILD_CONFIRM_TRANSFER, m_selected);
		theme::label(sX + 14, sY + action_y + 2, prompt.c_str(),
			theme::palette::value_hi);
		draw_button(sX, sY, btn_confirm_yes, UI_BTN_YES);
		draw_button(sX, sY, btn_confirm_no, UI_BTN_NO);
		return;
	}

	action_button buttons[5];
	const int count = build_action_buttons(buttons);
	for (int i = 0; i < count; i++)
		draw_button(sX, sY, buttons[i].rect, buttons[i].label,
			buttons[i].enabled);
}

bool DialogBox_GuildRoster::click_actions()
{
	if (m_confirm != action::none)
	{
		if (mouse_in(btn_confirm_yes))
		{
			act_on(m_confirm, m_selected.c_str());
			m_confirm = action::none;
			play_click_sound();
			return true;
		}
		if (mouse_in(btn_confirm_no))
		{
			m_confirm = action::none;
			play_click_sound();
			return true;
		}
		return false;
	}

	action_button buttons[5];
	const int count = build_action_buttons(buttons);
	for (int i = 0; i < count; i++)
	{
		const action_button& btn = buttons[i];
		if (!mouse_in(btn.rect))
			continue;
		if (!btn.enabled)
			return false;
		play_click_sound();
		// Kick and transfer are the irreversible pair; the rest act at once.
		if (btn.act == action::kick || btn.act == action::transfer)
			m_confirm = btn.act;
		else
			act_on(btn.act, m_selected.c_str());
		return true;
	}
	return false;
}

//----------------------------------------------------------------------
// The frame
//----------------------------------------------------------------------

void DialogBox_GuildRoster::on_draw()
{
	const short sX = m_x;
	const short sY = m_y;
	const guild_manager& manager = m_game->on_game()->get_guild_manager();

	theme::panel(sX, sY, panel_w, panel_h);
	theme::header(sX, sY, panel_w, UI_TITLE_GUILD_ROSTER);

	// Summary line: name+level left, member count right, from the info
	// answer (falls back to the CPlayer snapshot until it lands).
	if (manager.info_valid())
	{
		const auto& info = manager.info();
		theme::label(sX + 14, sY + 30,
			std::format(UI_GUILD_NAME_LEVEL, info.guild_name,
				info.level).c_str(),
			theme::palette::value_hi);
		theme::value(sX + panel_w - 174, sY + 30, 160,
			std::format(UI_GUILD_MEMBERS_OF, info.member_count,
				info.member_cap).c_str(),
			theme::palette::label);
	}
	else
	{
		theme::label(sX + 14, sY + 30, player().m_guild_name.c_str(),
			theme::palette::value_hi);
	}

	draw_titles(sX, sY);
	theme::separator(sX + 12, sY + 116, panel_w - 24);
	draw_list(sX, sY);
	theme::separator(sX + 12, sY + list_bottom + 2, panel_w - 24);
	draw_actions(sX, sY);

	draw_button(sX, sY, btn_refresh, UI_BTN_REFRESH);
	draw_button(sX, sY, btn_close, UI_BTN_OK);
}

bool DialogBox_GuildRoster::on_click()
{
	if (click_titles())
		return true;
	if (click_actions())
		return true;
	if (click_list())
		return true;
	if (mouse_in(btn_refresh))
	{
		refresh();
		play_click_sound();
		return true;
	}
	if (mouse_in(btn_close))
	{
		play_click_sound();
		disable_this_dialog();
		return true;
	}
	return false;
}

void DialogBox_GuildRoster::on_update()
{
	guild_manager& manager = m_game->on_game()->get_guild_manager();
	if (manager.roster_stale())
		manager.send_roster_request();
	if (manager.info_stale())
		manager.send_info_request();
}

PressResult DialogBox_GuildRoster::on_press()
{
	// Claim the scrollbar so a drag scrolls instead of moving the dialog.
	if (mouse_in(theme::grab_area(scroll_bar)))
		return PressResult::ScrollClaimed;
	return PressResult::Normal;
}

bool DialogBox_GuildRoster::on_enable(int type, int64_t v1, int v2, const char* string)
{
	m_selected.clear();
	m_confirm = action::none;
	m_scroll = 0;
	refresh();
	return true;
}
