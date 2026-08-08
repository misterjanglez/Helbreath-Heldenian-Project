#include "IDialogBox.h"
#include "DialogBoxManager.h"
#include "Player.h"
#include "Game.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "IInput.h"
#include "TextInputManager.h"
#include "UITheme.h"

#include <algorithm>

IDialogBox::IDialogBox(DialogBoxId::Type id, CGame* game)
	: m_game(game)
	, m_id(id)
{
}

bool IDialogBox::mouse_in(const ui_rect& r) const
{
	int mx = hb::shared::input::get_mouse_x();
	int my = hb::shared::input::get_mouse_y();
	return mx >= m_x + r.x && mx < m_x + r.x + r.w
		&& my >= m_y + r.y && my < m_y + r.y + r.h;
}

void IDialogBox::draw_new_dialog_box(char type, int sX, int sY, int frame, bool is_no_color_key, bool is_trans)
{
	m_game->draw_new_dialog_box(type, sX, sY, frame, is_no_color_key, is_trans);
}

void IDialogBox::draw_button(int sX, int sY, const ui_rect& r, const char* caption, bool enabled)
{
	hb::client::ui_theme::button(sX + r.x, sY + r.y, r.w, r.h, caption,
	                             enabled && mouse_in(r), enabled);
}

void IDialogBox::handle_vscroll(const ui_rect& bar, int max_scroll, int& scroll)
{
	if (max_scroll > 0 && m_manager->get_top_id() == m_id)
	{
		if (const int wheel = hb::shared::input::get_mouse_wheel_delta(); wheel != 0)
			scroll += wheel > 0 ? -1 : 1;

		// Dragging maps the cursor's position in the track straight onto the
		// scroll range, so the thumb follows the pointer rather than
		// accumulating deltas.
		if (hb::shared::input::is_mouse_button_down(hb::shared::input::MouseButton::Left)
			&& mouse_in(hb::client::ui_theme::grab_area(bar)))
		{
			const int offset = hb::shared::input::get_mouse_y() - (m_y + bar.y);
			scroll = (offset * max_scroll + bar.h / 2) / bar.h;
		}
	}
	scroll = std::clamp(scroll, 0, max_scroll);
}

void IDialogBox::bind_text_input(int x, int y, unsigned char max_len,
	std::string& buffer, std::string_view filter, bool hidden)
{
	if (m_manager->get_top_id() != m_id)
		return;
	auto& input = text_input_manager::get();
	// Re-anchor only a binding this dialog holds — an active input we never
	// bound (the chat line) is someone else's and stays untouched.
	if (input.is_active() && m_bound_input_x != -1
		&& (x != m_bound_input_x || y != m_bound_input_y))
		input.end_input();
	if (!input.is_active())
	{
		input.start_input(x, y, max_len, buffer, hidden, filter);
		m_bound_input_x = x;
		m_bound_input_y = y;
	}
}

void IDialogBox::unbind_text_input()
{
	if (m_bound_input_x != -1 && text_input_manager::get().is_active())
		text_input_manager::get().end_input();
	m_bound_input_x = -1;
	m_bound_input_y = -1;
}

void IDialogBox::put_string(int iX, int iY, const char* string, const hb::shared::render::Color& color)
{
	hb::shared::text::draw_text(GameFont::Default, iX, iY, string, hb::shared::text::TextStyle::from_color(color));
}

void IDialogBox::put_aligned_string(int x1, int x2, int iY, const char* string, const hb::shared::render::Color& color)
{
	hb::shared::text::draw_text_aligned(GameFont::Default, x1, iY, x2 - x1, 15, string,
	                         hb::shared::text::TextStyle::from_color(color), hb::shared::text::Align::TopCenter);
}

void IDialogBox::add_event_list(const char* txt, char color, bool dup_allow)
{
	m_game->add_event_list(txt, color, dup_allow);
}

bool IDialogBox::send_game_packet_impl(const hb::net::packet_base& pkt, size_t size, bool encrypt)
{
	return m_game->send_game_packet_impl(pkt, size, encrypt);
}

void IDialogBox::set_default_rect(short sX, short sY, short size_x, short size_y)
{
	m_x = sX;
	m_y = sY;
	m_size_x = size_x;
	m_size_y = size_y;
}

void IDialogBox::enable_dialog_box(DialogBoxId::Type id, int type, int64_t v1, int v2, const char* string)
{
	m_manager->enable_dialog_box(id, type, v1, v2, string);
}

void IDialogBox::disable_dialog_box(DialogBoxId::Type id)
{
	m_manager->disable_dialog_box(id);
}

void IDialogBox::disable_this_dialog()
{
	m_manager->disable_dialog_box(m_id);
}

IDialogBox* IDialogBox::get_dialog_box(DialogBoxId::Type id)
{
	return m_manager->get_dialog_box(id);
}

CPlayer& IDialogBox::player() const
{
	return m_manager->get_player();
}
