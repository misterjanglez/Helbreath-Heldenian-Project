// TESTER MENU — entire file is tester-only
#ifdef TESTER_ONLY
#include "DialogBox_NpcSpawner.h"
#include "Game.h"
#include "GlobalDef.h"
#include "SpriteID.h"
#include "NetMessages.h"
#include "PacketSendHelpers.h"

#include "GameFonts.h"
#include "TextLibExt.h"
#include "TextInputManager.h"
#include <algorithm>
#include <format>
#include <cstring>
#include "IInput.h"
#include "AudioManager.h"

using namespace hb::shared::net;
using namespace hb::client::sprite_id;

namespace layout
{
	constexpr int content_x1 = 12;
	constexpr int content_x2 = 246;
	constexpr int content_w = content_x2 - content_x1;

	constexpr int search_bar_y = 30;
	constexpr int list_y = 56;
	constexpr int row_h = 18;
	constexpr int list_rows = 10;
	constexpr int list_h = list_rows * row_h;
	constexpr int status_y = list_y + list_h + 2;

	// Amount stepper + spawn button
	constexpr int amount_label_y = status_y + 16;
	constexpr int stepper_y = amount_label_y + 18;
	constexpr int stepper_btn_w = 40;
	constexpr int stepper_btn_gap = 8;
	constexpr int spawn_btn_y = stepper_y + 26;
	constexpr int spawn_btn_w = 100;
}

DialogBox_NpcSpawner::DialogBox_NpcSpawner(CGame* game)
	: IDialogBox(DialogBoxId::NpcSpawner, game)
{
	set_default_rect(0, 0, 258, 339);
	m_can_close_on_right_click = true;
}

bool DialogBox_NpcSpawner::on_disable()
{
	text_input_manager::get().end_input();
	m_initial_load = false;
	m_last_sent_search.clear();
	return true;
}

void DialogBox_NpcSpawner::on_enter_pressed()
{
	// Live search handles everything — Enter is a no-op
}

void DialogBox_NpcSpawner::receive_search_results(const hb::net::PacketNotifyTesterNpcSearchResult* pkt)
{
	m_result_count = std::clamp(static_cast<int>(pkt->count), 0, 50);
	std::memcpy(m_results, pkt->entries, sizeof(m_results));
	m_selected_index = -1;
	m_scroll_offset = 0;
}

void DialogBox_NpcSpawner::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short z = static_cast<short>(hb::shared::input::get_mouse_wheel_delta());
	short sX = m_x;
	short sY = m_y;
	short size_x = m_size_x;

	draw_new_dialog_box(InterfaceNdGame2, sX, sY, 0);

	// Title
	hb::shared::text::draw_text_aligned(GameFont::Bitmap1,
		sX, sY + 8, size_x, 15,
		"Spawn NPC",
		hb::shared::text::TextStyle::with_integrated_shadow(GameColors::UIWarningRed),
		hb::shared::text::Align::TopCenter);

	// Text input — start/restart when inactive or dialog was dragged
	if (!text_input_manager::get().is_active())
	{
		text_input_manager::get().start_input(sX + 70, sY + layout::search_bar_y + 5, 20, m_search_text);
		m_last_sx = sX;
		m_last_sy = sY;
	}
	else if (sX != m_last_sx || sY != m_last_sy)
	{
		text_input_manager::get().end_input();
		text_input_manager::get().start_input(sX + 70, sY + layout::search_bar_y + 5, 20, m_search_text);
		m_last_sx = sX;
		m_last_sy = sY;
	}

	put_string(sX + 16, sY + layout::search_bar_y + 5, "Search:", GameColors::UIWhite);

	// Live search: auto-send whenever text changes (including initial empty load)
	if (!m_initial_load || m_search_text != m_last_sent_search)
	{
		m_initial_load = true;
		m_last_sent_search = m_search_text;
		m_scroll_offset = 0;
		{
			auto pkt = hb::net::make_common_command_str(CommonType::TesterNpcSearch, player().m_player_x, player().m_player_y);
			std::snprintf(pkt.text, sizeof(pkt.text), "%s", m_search_text.empty() ? "" : m_search_text.c_str());
			send_game_packet(pkt);
		}
	}

	// Mouse wheel scrolls the results list
	if (m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::NpcSpawner && z != 0)
	{
		m_scroll_offset -= z / 60;
		int max_scroll = std::max(0, m_result_count - layout::list_rows);
		m_scroll_offset = std::clamp(m_scroll_offset, 0, max_scroll);
	}

	// Results list
	for (int i = 0; i < layout::list_rows && (i + m_scroll_offset) < m_result_count; i++)
	{
		int idx = i + m_scroll_offset;
		auto& entry = m_results[idx];
		int ry = sY + layout::list_y + i * layout::row_h;

		bool hover = (mouse_x >= sX + layout::content_x1 && mouse_x <= sX + layout::content_x2
			&& mouse_y >= ry && mouse_y <= ry + layout::row_h - 2);

		auto color = (idx == m_selected_index) ? GameColors::UIPaleYellow
			: (hover ? GameColors::UIWhite : GameColors::UIMagicBlue);
		auto label = std::format("[{}] {}", entry.npc_id, entry.name);
		hb::shared::text::draw_text_aligned(GameFont::Default,
			sX + layout::content_x1 + 6, ry, layout::content_w - 12, 15,
			label.c_str(),
			hb::shared::text::TextStyle::from_color(color),
			hb::shared::text::Align::TopLeft);
	}

	// Status line
	if (m_result_count > 0)
	{
		int sy = sY + layout::status_y;
		auto count_str = std::format("{} found", m_result_count);
		put_string(sX + layout::content_x1 + 4, sy, count_str.c_str(), GameColors::UIBlack);

		if (m_result_count > layout::list_rows)
		{
			int max_scroll = m_result_count - layout::list_rows;
			auto scroll_str = std::format("[{}/{}]", m_scroll_offset + 1, max_scroll + 1);
			put_string(sX + 100, sy, scroll_str.c_str(), GameColors::UIBlack);
		}
	}

	// Amount label + value
	auto amount_str = std::format("Amount: {}", m_amount);
	hb::shared::text::draw_text_aligned(GameFont::Default,
		sX, sY + layout::amount_label_y, size_x, 15,
		amount_str.c_str(),
		hb::shared::text::TextStyle::from_color(GameColors::UIWhite),
		hb::shared::text::Align::TopCenter);

	// Stepper buttons: [ -10 ] [ -1 ] [ +1 ] [ +10 ]
	constexpr int total_w = layout::stepper_btn_w * 4 + layout::stepper_btn_gap * 3;
	int stepper_start_x = sX + (size_x - total_w) / 2;
	int stepper_y = sY + layout::stepper_y;
	const char* step_labels[] = { "-10", "-1", "+1", "+10" };
	for (int i = 0; i < 4; i++)
	{
		int bx = stepper_start_x + i * (layout::stepper_btn_w + layout::stepper_btn_gap);
		bool hover = (mouse_x >= bx && mouse_x <= bx + layout::stepper_btn_w
			&& mouse_y >= stepper_y && mouse_y <= stepper_y + 18);
		hb::shared::text::draw_text_aligned(GameFont::Default,
			bx, stepper_y, layout::stepper_btn_w, 15,
			step_labels[i],
			hb::shared::text::TextStyle::from_color(hover ? GameColors::UIWhite : GameColors::UIMagicBlue),
			hb::shared::text::Align::TopCenter);
	}

	// Spawn button (disabled look when nothing selected)
	int spawn_x = sX + (size_x - layout::spawn_btn_w) / 2;
	int spawn_y = sY + layout::spawn_btn_y;
	bool has_sel = (m_selected_index >= 0 && m_selected_index < m_result_count);
	bool spawn_hover = has_sel && (mouse_x >= spawn_x && mouse_x <= spawn_x + layout::spawn_btn_w
		&& mouse_y >= spawn_y && mouse_y <= spawn_y + 18);
	auto spawn_label = (m_amount > 1) ? std::format("[Spawn x{}]", m_amount) : std::string("[Spawn]");
	auto spawn_color = !has_sel ? GameColors::UIBlack
		: (spawn_hover ? GameColors::UIWhite : GameColors::UIMagicBlue);
	hb::shared::text::draw_text_aligned(GameFont::Default,
		spawn_x, spawn_y, layout::spawn_btn_w, 15,
		spawn_label.c_str(),
		hb::shared::text::TextStyle::from_color(spawn_color),
		hb::shared::text::Align::TopCenter);

	// Close button (sprite)
	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) &&
		(mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 1);
	else
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 0);
}

bool DialogBox_NpcSpawner::on_click()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;
	short size_x = m_size_x;

	// Results list — clicking selects the NPC
	for (int i = 0; i < layout::list_rows && (i + m_scroll_offset) < m_result_count; i++)
	{
		int idx = i + m_scroll_offset;
		int ry = sY + layout::list_y + i * layout::row_h;
		if (mouse_x >= sX + layout::content_x1 && mouse_x <= sX + layout::content_x2
			&& mouse_y >= ry && mouse_y <= ry + layout::row_h - 2)
		{
			m_selected_index = idx;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	// Stepper buttons
	constexpr int total_w = layout::stepper_btn_w * 4 + layout::stepper_btn_gap * 3;
	int stepper_start_x = sX + (size_x - total_w) / 2;
	int stepper_y = sY + layout::stepper_y;
	int deltas[] = { -10, -1, 1, 10 };
	for (int i = 0; i < 4; i++)
	{
		int bx = stepper_start_x + i * (layout::stepper_btn_w + layout::stepper_btn_gap);
		if (mouse_x >= bx && mouse_x <= bx + layout::stepper_btn_w
			&& mouse_y >= stepper_y && mouse_y <= stepper_y + 18)
		{
			m_amount = std::clamp(m_amount + deltas[i], 1, 50);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	// Spawn button
	int spawn_x = sX + (size_x - layout::spawn_btn_w) / 2;
	int spawn_y = sY + layout::spawn_btn_y;
	if (mouse_x >= spawn_x && mouse_x <= spawn_x + layout::spawn_btn_w
		&& mouse_y >= spawn_y && mouse_y <= spawn_y + 18)
	{
		if (m_selected_index >= 0 && m_selected_index < m_result_count)
		{
			auto pkt = hb::net::make_common_command(CommonType::TesterAction, player().m_player_x, player().m_player_y);
			pkt.v1 = 10;                                 // TesterAction case 10 = Spawn NPC
			pkt.v2 = m_results[m_selected_index].npc_id;
			pkt.v3 = m_amount;
			send_game_packet(pkt);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		}
		return true;
	}

	// Close button
	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) &&
		(mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
	{
		disable_this_dialog();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	return false;
}

bool DialogBox_NpcSpawner::on_enable(int type, int64_t v1, int v2, const char* string)
{
	if (is_enabled()) return true;
	int ns_x = LOGICAL_WIDTH() - 258 * 2 - 20;
	int ns_y = LOGICAL_HEIGHT() - 339 - ICON_PANEL_HEIGHT() - 10;
	m_x = ns_x;
	m_y = ns_y;
	m_selected_index = -1;
	m_amount = 1;
	return true;
}
#endif // TESTER_ONLY
