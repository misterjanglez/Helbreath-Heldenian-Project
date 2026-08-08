// DialogBox_GuildOperation.cpp: the requests queue (#124). See the header.
//
//////////////////////////////////////////////////////////////////////

#include "DialogBox_GuildOperation.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <string>

#include "AudioManager.h"
#include "Game.h"
#include "GuildManager.h"
#include "Screen_OnGame.h"
#include "UITheme.h"
#include "lan_eng.h"

namespace theme = hb::client::ui_theme;
namespace guild = hb::shared::guild;

namespace
{
	// "3m" / "5h" / "2d" since the filing; the wire carries unix seconds.
	std::string age_text(int64_t created_at, int64_t now)
	{
		const int64_t age = now - created_at;
		if (age < 60)
			return "<1m";
		if (age < 3600)
			return std::format("{}m", age / 60);
		if (age < 86400)
			return std::format("{}h", age / 3600);
		return std::format("{}d", age / 86400);
	}
}

DialogBox_GuildOperation::DialogBox_GuildOperation(CGame* game)
	: IDialogBox(DialogBoxId::GuildOperation, game)
{
	set_default_rect(300, 100, panel_w, panel_h);
	m_can_close_on_right_click = true;
}

void DialogBox_GuildOperation::play_click_sound() const
{
	audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
}

// The decide buttons of every visible row, one build walked by draw and
// click alike. A row's pair only appears when the actor holds the bit that
// decides its kind — a holder of one bit sees the other kind read-only.
int DialogBox_GuildOperation::build_row_buttons(row_button (&buttons)[visible_rows * 2]) const
{
	const auto& queue = m_game->on_game()->get_guild_manager().queue();
	const uint32_t mask = player().m_guild_permission_mask;

	int n = 0;
	for (int i = 0; i < visible_rows; i++)
	{
		const std::size_t index = static_cast<std::size_t>(m_scroll + i);
		if (index >= queue.size())
			break;
		const auto& row = queue[index];
		if ((mask & guild::deciding_bit_for(row.kind)) == 0)
			continue;
		const int y = list_top + i * row_pitch;
		buttons[n++] = { row.request_id, true, btn_approve.at_y(y) };
		buttons[n++] = { row.request_id, false, btn_reject.at_y(y) };
	}
	return n;
}

void DialogBox_GuildOperation::on_draw()
{
	const short sX = m_x;
	const short sY = m_y;
	const guild_manager& manager = m_game->on_game()->get_guild_manager();
	const auto& queue = manager.queue();

	theme::panel(sX, sY, panel_w, panel_h);
	theme::header(sX, sY, panel_w, UI_TITLE_GUILD_REQUESTS);

	if (queue.empty())
	{
		put_aligned_string(sX, sX + panel_w, sY + list_top + 60,
			UI_GUILD_QUEUE_EMPTY, GameColors::UIDisabled);
	}

	const int max_scroll =
		std::max(0, static_cast<int>(queue.size()) - visible_rows);
	handle_vscroll(scroll_bar, max_scroll, m_scroll);

	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	for (int i = 0; i < visible_rows; i++)
	{
		const std::size_t index = static_cast<std::size_t>(m_scroll + i);
		if (index >= queue.size())
			break;
		const auto& row = queue[index];
		const int y = list_top + i * row_pitch;

		const bool is_join = row.kind == guild::guild_request::join;
		theme::label(sX + col_kind, sY + y,
			is_join ? UI_GUILD_KIND_JOIN : UI_GUILD_KIND_LEAVE,
			is_join ? theme::palette::value : theme::palette::value_hi);
		theme::label(sX + col_applicant, sY + y, row.applicant);
		theme::label(sX + col_age, sY + y,
			age_text(row.created_at, now).c_str(), theme::palette::dim);
	}

	row_button buttons[visible_rows * 2];
	const int count = build_row_buttons(buttons);
	for (int i = 0; i < count; i++)
		draw_button(sX, sY, buttons[i].rect,
			buttons[i].approve ? UI_BTN_APPROVE : UI_BTN_REJECT);

	// The wire clips the queue at kGuildQueueMaxEntries; say so rather than
	// letting a clipped view read as the whole story.
	if (manager.queue_total() > queue.size())
	{
		put_aligned_string(sX, sX + panel_w, sY + list_bottom + 8,
			std::format(UI_GUILD_QUEUE_MORE,
				manager.queue_total() - queue.size()).c_str(),
			GameColors::UIDisabled);
	}

	if (max_scroll > 0)
	{
		const int track = scroll_bar.h;
		const int thumb = std::clamp(track * visible_rows
			/ static_cast<int>(queue.size()), row_pitch, track);
		theme::scrollbar(sX + scroll_bar.x, sY + scroll_bar.y, scroll_bar.w,
			track, (track - thumb) * m_scroll / max_scroll, thumb);
	}

	draw_button(sX, sY, btn_refresh, UI_BTN_REFRESH);
	draw_button(sX, sY, btn_close, UI_BTN_OK);
}

bool DialogBox_GuildOperation::on_click()
{
	guild_manager& manager = m_game->on_game()->get_guild_manager();

	row_button buttons[visible_rows * 2];
	const int count = build_row_buttons(buttons);
	for (int i = 0; i < count; i++)
	{
		if (!mouse_in(buttons[i].rect))
			continue;
		manager.send_decide(buttons[i].request_id, buttons[i].approve);
		play_click_sound();
		return true;
	}

	if (mouse_in(btn_refresh))
	{
		manager.send_queue_request();
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

void DialogBox_GuildOperation::on_update()
{
	guild_manager& manager = m_game->on_game()->get_guild_manager();
	if (manager.queue_stale())
		manager.send_queue_request();
}

PressResult DialogBox_GuildOperation::on_press()
{
	if (mouse_in(theme::grab_area(scroll_bar)))
		return PressResult::ScrollClaimed;
	return PressResult::Normal;
}

bool DialogBox_GuildOperation::on_enable(int type, int64_t v1, int v2, const char* string)
{
	m_scroll = 0;
	m_game->on_game()->get_guild_manager().send_queue_request();
	return true;
}
