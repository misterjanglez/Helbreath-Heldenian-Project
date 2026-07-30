#include "DialogBox_SysMenu.h"
#include "Game.h"
#include "ChatManager.h"
#include "GlobalDef.h"
#include "lan_eng.h"
#include "AudioManager.h"
#include "ConfigManager.h"
#include "IInput.h"
#include "RendererFactory.h"
#include "ITextRenderer.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <format>
#include <string>
#include "Screen_OnGame.h"
#include "UITheme.h"
using namespace hb::client::sprite_id;

// Content area constants
static const int CONTENT_X = 21;
static const int CONTENT_Y = 57;
static const int CONTENT_WIDTH = 297;
static const int CONTENT_HEIGHT = 234;

// Widget sizes.
//
// These were sprite frames on sheet 10 — the tab plate (70), the wide value box
// (78), the small toggle box (79), the slider groove (80) and the large
// five-segment box (81) — and the dialog used to read their dimensions back off
// the sprite at runtime, cache them in six members, and carry a hardcoded
// fallback set for the frame where the sprite was not loaded yet. Two of those
// fallbacks were wrong (280 and 36 against the art's 160 and 32), which nothing
// noticed because the real values always won by the second frame.
//
// Drawing the boxes from primitives makes these sizes the layout itself, so they
// are stated here and the draw and click paths read the same numbers.
namespace box
{
	constexpr int tab_w   = 74;
	constexpr int tab_h   = hb::client::ui_theme::metrics::tab_height;
	constexpr int wide_w  = 117, wide_h = 16;
	constexpr int small_w = 32,  small_h = 16;
	constexpr int large_w = 160, large_h = 16;
	constexpr int slider_w = 100;   // 0-100 volume maps 1:1 onto the groove
}

// The graphics list scrollbar. It was InterfaceNdGame1 frames 3 and 4 � a 4x230
// track and a 10x11 thumb � whose sizes the draw and drag paths each looked up
// separately off the sprite. A themed track needs no art, so the two numbers it
// still needs are named once.
static constexpr int graphics_scroll_w = 11;
static constexpr int graphics_scroll_thumb_h = 24;

// Graphics tab scroll
static constexpr int GRAPHICS_LINE_HEIGHT = 18;
static constexpr int GRAPHICS_VISIBLE_ITEMS = 12;
static bool s_bDraggingGraphicsScroll = false;

// Slider tracking
static bool s_bDraggingMasterSlider = false;
static bool s_bDraggingEffectsSlider = false;
static bool s_bDraggingAmbientSlider = false;
static bool s_bDraggingUISlider = false;
static bool s_bDraggingMusicSlider = false;

// 4:3 resolutions from 640x480 to 1920x1440
const Resolution DialogBox_SysMenu::s_Resolutions[] = {
	//{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1280, 960 },
	{ 1440, 1080 },
	{ 1920, 1440 }
};

const int DialogBox_SysMenu::s_NumResolutions = sizeof(s_Resolutions) / sizeof(s_Resolutions[0]);

int DialogBox_SysMenu::get_current_resolution_index()
{
	int currentWidth = config_manager::get().get_window_width();
	int currentHeight = config_manager::get().get_window_height();

	for (int i = 0; i < s_NumResolutions; i++) {
		if (s_Resolutions[i].width == currentWidth && s_Resolutions[i].height == currentHeight) {
			return i;
		}
	}
	return get_nearest_resolution_index(currentWidth, currentHeight);
}

int DialogBox_SysMenu::get_nearest_resolution_index(int width, int height)
{
	int bestIndex = 0;
	int bestDiff = abs(s_Resolutions[0].width - width) + abs(s_Resolutions[0].height - height);

	for (int i = 1; i < s_NumResolutions; i++) {
		int diff = abs(s_Resolutions[i].width - width) + abs(s_Resolutions[i].height - height);
		if (diff < bestDiff) {
			bestDiff = diff;
			bestIndex = i;
		}
	}
	return bestIndex;
}

void DialogBox_SysMenu::cycle_resolution()
{
	int currentIndex = get_current_resolution_index();
	int nextIndex = (currentIndex + 1) % s_NumResolutions;
	apply_resolution(nextIndex);
}

void DialogBox_SysMenu::apply_resolution(int index)
{
	if (index < 0 || index >= s_NumResolutions) return;

	int newWidth = s_Resolutions[index].width;
	int newHeight = s_Resolutions[index].height;

	config_manager::get().set_window_size(newWidth, newHeight);
	config_manager::get().save();

	hb::shared::render::Window::set_size(newWidth, newHeight, true);

	if (hb::shared::render::Renderer::get())
		hb::shared::render::Renderer::get()->resize_back_buffer(newWidth, newHeight);

	hb::shared::input::get()->set_window_active(true);
}

DialogBox_SysMenu::DialogBox_SysMenu(CGame* game)
	: IDialogBox(DialogBoxId::SystemMenu, game)
	, m_iActiveTab(TAB_GENERAL)
{
	set_default_rect(237 , 67 , 331, 303);
}

void DialogBox_SysMenu::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short z = static_cast<short>(hb::shared::input::get_mouse_wheel_delta());
	char lb = hb::shared::input::is_mouse_button_down(hb::shared::input::MouseButton::Left) ? 1 : 0;
	short sX = m_x;
	short sY = m_y;

	// draw dialog background
	draw_new_dialog_box(InterfaceNdGame1, sX, sY, 0);
	hb::client::ui_theme::header(sX, sY, m_size_x, UI_TITLE_SYSTEM_MENU);

	// Handle mouse scroll over dialog to cycle tabs (or scroll graphics content)
	if (m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::SystemMenu && z != 0)
	{
		if (m_iActiveTab == TAB_GRAPHICS)
		{
			int total_items = 11;
#ifdef _DEBUG
			total_items = 13;
#endif
			int max_scroll = total_items - GRAPHICS_VISIBLE_ITEMS;

			bool in_content = (mouse_x >= sX + CONTENT_X &&
				mouse_x <= sX + CONTENT_X + CONTENT_WIDTH &&
				mouse_y >= sY + CONTENT_Y &&
				mouse_y <= sY + CONTENT_Y + CONTENT_HEIGHT);

			if (max_scroll > 0 && in_content)
			{
				if (z > 0) m_graphics_scroll_offset--;
				if (z < 0) m_graphics_scroll_offset++;
				if (m_graphics_scroll_offset < 0) m_graphics_scroll_offset = 0;
				if (m_graphics_scroll_offset > max_scroll) m_graphics_scroll_offset = max_scroll;
			}
			else
			{
				int prev_tab = m_iActiveTab;
				if (z > 0) m_iActiveTab = (m_iActiveTab - 1 + TAB_COUNT) % TAB_COUNT;
				else m_iActiveTab = (m_iActiveTab + 1) % TAB_COUNT;
				if (m_iActiveTab != prev_tab) m_graphics_scroll_offset = 0;
			}
		}
		else
		{
			if (z > 0) m_iActiveTab = (m_iActiveTab - 1 + TAB_COUNT) % TAB_COUNT;
			else m_iActiveTab = (m_iActiveTab + 1) % TAB_COUNT;
		}
	}

	// Update graphics scrollbar drag while mouse is held
	if (s_bDraggingGraphicsScroll && lb != 0)
	{
		int total_items = 11;
#ifdef _DEBUG
		total_items = 13;
#endif
		int max_scroll = total_items - GRAPHICS_VISIBLE_ITEMS;
		// Against the thumb that is drawn, not the pak frame the art used to
		// supply: that frame is 11px tall and the themed thumb is not, so the
		// cursor and the thumb were tracking at different rates.
		int track_top = sY + CONTENT_Y;
		int track_height = std::max(CONTENT_HEIGHT - graphics_scroll_thumb_h, 1);
		int rel_y = mouse_y - track_top;
		m_graphics_scroll_offset = (rel_y * max_scroll + track_height / 2) / track_height;
		if (m_graphics_scroll_offset < 0) m_graphics_scroll_offset = 0;
		if (m_graphics_scroll_offset > max_scroll) m_graphics_scroll_offset = max_scroll;
	}

	draw_tabs(sX, sY);
	draw_tab_content(sX, sY, mouse_x, mouse_y, lb);

	// save slider values to config_manager when drag ends (mouse released)
	if (lb == 0)
	{
		if (s_bDraggingGraphicsScroll)
		{
			s_bDraggingGraphicsScroll = false;
		}
		if (s_bDraggingMasterSlider)
		{
			config_manager::get().set_master_volume(audio_manager::get().get_master_volume());
		}
		if (s_bDraggingEffectsSlider)
		{
			config_manager::get().set_sound_volume(audio_manager::get().get_sound_volume());
		}
		if (s_bDraggingAmbientSlider)
		{
			config_manager::get().set_ambient_volume(audio_manager::get().get_ambient_volume());
		}
		if (s_bDraggingUISlider)
		{
			config_manager::get().set_ui_volume(audio_manager::get().get_ui_volume());
		}
		if (s_bDraggingMusicSlider)
		{
			config_manager::get().set_music_volume(audio_manager::get().get_music_volume());
		}
		s_bDraggingMasterSlider = false;
		s_bDraggingEffectsSlider = false;
		s_bDraggingAmbientSlider = false;
		s_bDraggingUISlider = false;
		s_bDraggingMusicSlider = false;
		m_is_scroll_selected = false;
	}
}

// Panel-relative x of tab i. The draw and the click test share it so a tab can
// never be lit in one place and hit in another.
static int tab_x(int i) { return 17 + box::tab_w * i; }
static constexpr int tab_y = 33;

void DialogBox_SysMenu::draw_tabs(short sX, short sY)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());

	static const char* const captions[TAB_COUNT] = {
		UI_BTN_GENERAL, UI_BTN_GRAPHICS, UI_BTN_AUDIO, UI_BTN_SYSTEM
	};

	hb::client::ui_theme::tab_bar(sX + tab_x(0), sY + tab_y, box::tab_w * TAB_COUNT, box::tab_h);

	// The art had one lit face doing double duty for "selected" and "hovered".
	// The theme separates them: the underline marks the selection and the caption
	// colour follows the cursor.
	for (int i = 0; i < TAB_COUNT; i++)
	{
		// Hit-tested against the whole slot, the same rect on_click uses; drawn
		// inset by a pixel so the active tab's underline lands on the bar's edge.
		const ui_rect slot{ tab_x(i), tab_y, box::tab_w, box::tab_h };
		hb::client::ui_theme::tab(sX + slot.x, sY + slot.y + 1, slot.w, slot.h - 2, captions[i],
			m_iActiveTab == i || mouse_in(slot));
	}
}

void DialogBox_SysMenu::draw_tab_content(short sX, short sY, short mouse_x, short mouse_y, char lb)
{
	switch (m_iActiveTab)
	{
	case TAB_GENERAL:
		draw_general_tab(sX, sY);
		break;
	case TAB_GRAPHICS:
		draw_graphics_tab(sX, sY);
		break;
	case TAB_AUDIO:
		draw_audio_tab(sX, sY, mouse_x, mouse_y, lb);
		break;
	case TAB_SYSTEM:
		draw_system_tab(sX, sY);
		break;
	}
}

void DialogBox_SysMenu::draw_toggle(int x, int y, bool enabled)
{
	// The toggle is a button whose caption is its state, so the themed button
	// draws the whole thing — face, rim and centred caption — where the pak
	// version needed a box frame plus hand-measured text centring.
	const bool hover = is_in_toggle_area(x, y);
	hb::client::ui_theme::button(x, y - 2, box::small_w, box::small_h,
		enabled ? DRAW_DIALOGBOX_SYSMENU_ON : DRAW_DIALOGBOX_SYSMENU_OFF,
		hover, enabled || hover);
}

bool DialogBox_SysMenu::is_in_toggle_area(int x, int y)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	return (mouse_x >= x && mouse_x <= x + box::small_w
	     && mouse_y >= y - 2 && mouse_y <= y - 2 + box::small_h);
}

void DialogBox_SysMenu::draw_option(int x, int y, int w, const char* text,
	bool selected, bool enabled)
{
	// Hover is derived here rather than passed in: every caller was running the
	// same rect test against the rect it already hands over, and two of them had
	// drifted to an inclusive right edge.
	const short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	const short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	const bool lit = enabled && (selected
		|| (mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + box::wide_h));

	hb::shared::text::draw_text_aligned(GameFont::Default, x, y, w, box::wide_h, text,
		hb::shared::text::TextStyle::from_color(
			lit ? hb::client::ui_theme::palette::label : hb::client::ui_theme::palette::dim),
		hb::shared::text::Align::Center);
}

// =============================================================================
// GENERAL TAB
// =============================================================================

// The general tab's two buttons take the footer slots' x and width but sit at the
// bottom of the content area rather than in the panel footer. Panel-relative, so
// draw_button and mouse_in both accept them.
static constexpr int general_btn_y = CONTENT_Y + CONTENT_HEIGHT - 30;
static constexpr ui_rect general_btn_left {
	ui_layout::left_btn_x,  general_btn_y, ui_layout::btn_size_x, ui_layout::btn_size_y };
static constexpr ui_rect general_btn_right{
	ui_layout::right_btn_x, general_btn_y, ui_layout::btn_size_x, ui_layout::btn_size_y };

void DialogBox_SysMenu::draw_general_tab(short sX, short sY)
{
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	const int contentBottom = contentY + CONTENT_HEIGHT;
	const int centerX = contentX + (CONTENT_WIDTH / 2);

	// Server name at top left
	put_string(contentX + 5, contentY + 5, UPDATE_SCREEN_ON_SELECT_CHARACTER36, GameColors::UILabel);
	put_string(contentX + 6, contentY + 5, UPDATE_SCREEN_ON_SELECT_CHARACTER36, GameColors::UILabel);

	// Current time centered below server name (MM/DD/YYYY HH:MM AM/PM)
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm local_time{};
#ifdef _WIN32
	localtime_s(&local_time, &t);
#else
	localtime_r(&t, &local_time);
#endif
	std::string timeBuf;
	int hour12 = local_time.tm_hour % 12;
	if (hour12 == 0) hour12 = 12;
	const char* ampm = (local_time.tm_hour < 12) ? "AM" : "PM";
	timeBuf = std::format("{:02}/{:02}/{:04} {}:{:02} {}",
		local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_year + 1900,
		hour12, local_time.tm_min, ampm);

	int textWidth = hb::shared::text::GetTextRenderer()->measure_text(timeBuf.c_str()).width;
	int timeX = centerX - (textWidth / 2);
	put_string(timeX, contentY + 25, timeBuf.c_str(), GameColors::UILabel);
	put_string(timeX + 1, contentY + 25, timeBuf.c_str(), GameColors::UILabel);

	// Buttons sit at the bottom of the content area, not in the panel footer, so
	// they take the footer slots' x and width but their own y.
	// A logout already counting down turns the button into "Continue", which
	// cancels it.
	const bool counting_down = (m_game->on_game()->m_logout_count != -1);
	draw_button(sX, sY, general_btn_left, counting_down ? UI_BTN_CONTINUE : UI_BTN_LOG_OUT);

	if ((player().m_hp <= 0) && (m_game->m_restart_count == -1))
		draw_button(sX, sY, general_btn_right, UI_BTN_RESTART);
}

// =============================================================================
// GRAPHICS TAB
// =============================================================================
void DialogBox_SysMenu::draw_graphics_tab(short sX, short sY)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	const int contentRight = contentX + CONTENT_WIDTH;
	const int labelX = contentX + 5;

	// Right margin for box alignment
	const int rightMargin = 8;
	const int boxRightEdge = contentRight - rightMargin;

	// Right-align the large box, then left-align all smaller boxes to its left edge
	const int largeBoxX = boxRightEdge - box::large_w;
	const int wideBoxX = largeBoxX;
	const int smallBoxX = largeBoxX;

	const bool fullscreen = m_game->m_Renderer->is_fullscreen();

	// Scroll support
	int total_items = 11;
#ifdef _DEBUG
	total_items = 13;
#endif
	bool scrollable = total_items > GRAPHICS_VISIBLE_ITEMS;

	int lineY = contentY + 5 - (m_graphics_scroll_offset * GRAPHICS_LINE_HEIGHT);

	auto is_item_visible = [&](int ly) {
		return (ly >= contentY - 2) && (ly + 16 <= contentY + CONTENT_HEIGHT);
	};

	// --- FPS Limit --- large box (frame 81) with 5 options (disabled when VSync is on)
	if (is_item_visible(lineY))
	{
		const bool v_sync_on = config_manager::get().is_vsync_enabled();
		put_string(labelX, lineY, "FPS Limit:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "FPS Limit:", GameColors::UILabel);

		const int fpsBoxY = lineY - 2;
		hb::client::ui_theme::content_frame(largeBoxX, fpsBoxY, box::large_w, box::large_h);

		static const int s_FpsOptions[] = { 60, 100, 144, 240, 0 };
		static const char* s_FpsLabels[] = { "60", "100", "144", "240", "Max" };
		static const int s_NumFpsOptions = 5;
		const int fpsRegionWidth = box::large_w / s_NumFpsOptions;
		const int currentFps = config_manager::get().get_fps_limit();

		for (int i = 0; i < s_NumFpsOptions; i++)
		{
			draw_option(largeBoxX + fpsRegionWidth * i, fpsBoxY, fpsRegionWidth, s_FpsLabels[i],
				currentFps == s_FpsOptions[i], !v_sync_on);
		}
	}

	lineY += 18;

	// --- Aspect Ratio --- wide box (Letterbox / Widescreen), only enabled in fullscreen
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Aspect Ratio:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Aspect Ratio:", GameColors::UILabel);

		const int aspectBoxY = lineY - 2;
		hb::client::ui_theme::content_frame(wideBoxX, aspectBoxY, box::wide_w, box::wide_h);

		const bool stretch = config_manager::get().is_fullscreen_stretch_enabled();
		const int aspectRegionWidth = box::wide_w / 2;

		draw_option(wideBoxX, aspectBoxY, aspectRegionWidth, "Letterbox", !stretch, fullscreen);
		draw_option(wideBoxX + aspectRegionWidth, aspectBoxY, aspectRegionWidth, "Widescreen",
			stretch, fullscreen);
	}

	lineY += 18;

	// --- VSync ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "VSync:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "VSync:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_vsync_enabled());
	}

	lineY += 18;

	// --- Detail Level --- wide box with Low/Normal/High
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, DRAW_DIALOGBOX_SYSMENU_DETAILLEVEL, GameColors::UILabel);
		put_string(labelX + 1, lineY, DRAW_DIALOGBOX_SYSMENU_DETAILLEVEL, GameColors::UILabel);

		const int detailLevel = config_manager::get().get_detail_level();
		const int boxY = lineY - 2;

		hb::client::ui_theme::content_frame(wideBoxX, boxY, box::wide_w, box::wide_h);

		const int regionWidth = box::wide_w / 3;
		static const char* const levels[3] = {
			DRAW_DIALOGBOX_SYSMENU_LOW, DRAW_DIALOGBOX_SYSMENU_NORMAL, DRAW_DIALOGBOX_SYSMENU_HIGH
		};

		for (int i = 0; i < 3; i++)
			draw_option(wideBoxX + regionWidth * i, boxY, regionWidth, levels[i], detailLevel == i);
	}

	lineY += 18;

	// --- Dialog Transparency ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Transparency:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Transparency:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_dialog_transparency_enabled());
	}

	lineY += 18;

	// --- Show FPS ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Show FPS:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Show FPS:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_show_fps_enabled());
	}

	lineY += 18;

	// --- Show Latency ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Show Latency:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Show Latency:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_show_latency_enabled());
	}

	lineY += 18;

	// --- Background FPS Throttle ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Power Saving:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Power Saving:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_background_fps_throttle_enabled());
	}

	lineY += 18;

	// --- Map Zoom ---
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Map Zoom:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Map Zoom:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_zoom_map_enabled());
	}

	lineY += 18;

#ifdef _DEBUG
	// Tile Grid (simple dark lines) - DEBUG ONLY
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Tile Grid:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Tile Grid:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_tile_grid_enabled());
	}

	lineY += 18;

	// Patching Grid (debug with zone colors) - DEBUG ONLY
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Patching Grid:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Patching Grid:", GameColors::UILabel);
		draw_toggle(smallBoxX, lineY, config_manager::get().is_patching_grid_enabled());
	}

	lineY += 18;
#endif

	// --- Display Mode --- wide box (Fullscreen / Windowed)
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Display Mode:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Display Mode:", GameColors::UILabel);

		const int modeBoxY = lineY - 2;
		hb::client::ui_theme::content_frame(wideBoxX, modeBoxY, box::wide_w, box::wide_h);

		draw_option(wideBoxX, modeBoxY, box::wide_w, fullscreen ? "Fullscreen" : "Windowed", false);
	}

	lineY += 18;

	// --- hb::shared::render::Window Style --- wide box (Borderless / Bordered, disabled when fullscreen)
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Window Style:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Window Style:", GameColors::UILabel);

		const int styleBoxY = lineY - 2;
		hb::client::ui_theme::content_frame(wideBoxX, styleBoxY, box::wide_w, box::wide_h);

		draw_option(wideBoxX, styleBoxY, box::wide_w,
			config_manager::get().is_borderless_enabled() ? "Borderless" : "Bordered",
			false, !fullscreen);
	}

	lineY += 18;

	// --- Resolution --- wide box with centered text (disabled when fullscreen)
	if (is_item_visible(lineY))
	{
		put_string(labelX, lineY, "Resolution:", GameColors::UILabel);
		put_string(labelX + 1, lineY, "Resolution:", GameColors::UILabel);

		int resWidth, resHeight;
		if (fullscreen) {
			resWidth = hb::platform::get_screen_width();
			resHeight = hb::platform::get_screen_height();
		}
		else {
			int resIndex = get_current_resolution_index();
			resWidth = s_Resolutions[resIndex].width;
			resHeight = s_Resolutions[resIndex].height;
		}

		std::string resBuf;
		resBuf = std::format("{}x{}", resWidth, resHeight);

		const int resBoxY = lineY - 2;
		hb::client::ui_theme::content_frame(wideBoxX, resBoxY, box::wide_w, box::wide_h);

		draw_option(wideBoxX, resBoxY, box::wide_w, resBuf.c_str(), false, !fullscreen);
	}

	// --- Scrollbar ---
	if (scrollable)
	{
		const int max_scroll = total_items - GRAPHICS_VISIBLE_ITEMS;
		const int offset = max_scroll > 0
			? ((CONTENT_HEIGHT - graphics_scroll_thumb_h) * m_graphics_scroll_offset) / max_scroll
			: 0;
		hb::client::ui_theme::scrollbar(contentX + CONTENT_WIDTH - graphics_scroll_w / 2, contentY,
			graphics_scroll_w, CONTENT_HEIGHT, offset, graphics_scroll_thumb_h);
	}
}

// =============================================================================
// AUDIO TAB
// =============================================================================
void DialogBox_SysMenu::draw_volume_row(int label_x, int toggle_x, int slider_x, int y,
	const char* caption, bool available, bool enabled, int volume)
{
	put_string(label_x, y, caption, GameColors::UILabel);
	put_string(label_x + 1, y, caption, GameColors::UILabel);

	if (available)
		draw_toggle(toggle_x, y, enabled);
	else
		put_string(toggle_x, y, DRAW_DIALOGBOX_SYSMENU_DISABLED, GameColors::UIDisabled);

	hb::client::ui_theme::slider(slider_x, y, box::slider_w, volume);
}

void DialogBox_SysMenu::draw_audio_tab(short sX, short sY, short mouse_x, short mouse_y, char lb)
{
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	const int labelX = contentX + 5;
	const int toggleX = contentX + 68;
	const int sliderX = contentX + 110;

	auto& audio = audio_manager::get();
	const bool available = audio.is_sound_available();

	// A drag in progress writes the volume straight from the cursor. The rows all
	// did this identically; on_press decided which flag is set, so the mapping from
	// flag to setter is the only thing that differs.
	const int dragged = std::clamp(mouse_x - sliderX, 0, box::slider_w);
	if (lb != 0)
	{
		if (s_bDraggingMasterSlider)  audio.set_master_volume(dragged);
		if (s_bDraggingEffectsSlider) audio.set_sound_volume(dragged);
		if (s_bDraggingAmbientSlider) audio.set_ambient_volume(dragged);
		if (s_bDraggingUISlider)      audio.set_ui_volume(dragged);
		if (s_bDraggingMusicSlider)   audio.set_music_volume(dragged);
	}

	draw_volume_row(labelX, toggleX, sliderX, contentY + 8, "Master:",
		available, audio.is_master_enabled(), audio.get_master_volume());
	draw_volume_row(labelX, toggleX, sliderX, contentY + 52, "Effects:",
		available, audio.is_sound_enabled(), audio.get_sound_volume());
	draw_volume_row(labelX, toggleX, sliderX, contentY + 92, "Ambient:",
		available, audio.is_ambient_enabled(), audio.get_ambient_volume());
	draw_volume_row(labelX, toggleX, sliderX, contentY + 132, "UI:",
		available, audio.is_ui_enabled(), audio.get_ui_volume());
	draw_volume_row(labelX, toggleX, sliderX, contentY + 172, DRAW_DIALOGBOX_SYSMENU_MUSIC,
		available, audio.is_music_enabled(), audio.get_music_volume());
}

// =============================================================================
// SYSTEM TAB
// =============================================================================
void DialogBox_SysMenu::draw_system_tab(short sX, short sY)
{
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	int lineY = contentY + 5;
	const int labelX = contentX + 5;
	const int valueX = contentX + 140;

	// Whisper toggle
	put_string(labelX, lineY, DRAW_DIALOGBOX_SYSMENU_WHISPER, GameColors::UILabel);
	put_string(labelX + 1, lineY, DRAW_DIALOGBOX_SYSMENU_WHISPER, GameColors::UILabel);
	draw_toggle(valueX, lineY, ChatManager::get().is_whisper_enabled());

	lineY += 20;

	// Shout toggle
	put_string(labelX, lineY, DRAW_DIALOGBOX_SYSMENU_SHOUT, GameColors::UILabel);
	put_string(labelX + 1, lineY, DRAW_DIALOGBOX_SYSMENU_SHOUT, GameColors::UILabel);
	draw_toggle(valueX, lineY, ChatManager::get().is_shout_enabled());

	lineY += 20;

	// Running Mode toggle
	put_string(labelX, lineY, "Running Mode:", GameColors::UILabel);
	put_string(labelX + 1, lineY, "Running Mode:", GameColors::UILabel);
	draw_toggle(valueX, lineY, config_manager::get().is_running_mode_enabled());

	lineY += 20;

	// Capture Mouse toggle
	put_string(labelX, lineY, "Capture Mouse:", GameColors::UILabel);
	put_string(labelX + 1, lineY, "Capture Mouse:", GameColors::UILabel);
	draw_toggle(valueX, lineY, config_manager::get().is_mouse_capture_enabled());

	lineY += 20;

	// Guide Map toggle
	put_string(labelX, lineY, DRAW_DIALOGBOX_SYSMENU_GUIDEMAP, GameColors::UILabel);
	put_string(labelX + 1, lineY, DRAW_DIALOGBOX_SYSMENU_GUIDEMAP, GameColors::UILabel);
	draw_toggle(valueX, lineY, m_game->get_dialog_box_manager().is_enabled(DialogBoxId::GuideMap));

	lineY += 20;

	// Reduced Motion toggle
	put_string(labelX, lineY, "Reduced Motion:", GameColors::UILabel);
	put_string(labelX + 1, lineY, "Reduced Motion:", GameColors::UILabel);
	draw_toggle(valueX, lineY, config_manager::get().is_reduced_motion_enabled());

	lineY += 20;

	// Toggle to Chat toggle
	put_string(labelX, lineY, "Toggle to Chat:", GameColors::UILabel);
	put_string(labelX + 1, lineY, "Toggle to Chat:", GameColors::UILabel);
	draw_toggle(valueX, lineY, config_manager::get().is_toggle_to_chat_enabled());
}

// =============================================================================
// CLICK HANDLERS
// =============================================================================
bool DialogBox_SysMenu::on_click()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;

	// Check tab button clicks
	for (int i = 0; i < TAB_COUNT; i++)
	{
		if (mouse_in(ui_rect{ tab_x(i), tab_y, box::tab_w, box::tab_h }))
		{
			if (m_iActiveTab != i) m_graphics_scroll_offset = 0;
			m_iActiveTab = i;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	// Handle clicks for active tab content
	switch (m_iActiveTab)
	{
	case TAB_GENERAL:
		return on_click_general();
	case TAB_GRAPHICS:
		return on_click_graphics(sX, sY);
	case TAB_AUDIO:
		return on_click_audio(sX, sY);
	case TAB_SYSTEM:
		return on_click_system(sX, sY);
	}

	return false;
}

PressResult DialogBox_SysMenu::on_press()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;

	// Graphics tab scrollbar drag
	if (m_iActiveTab == TAB_GRAPHICS)
	{
		int total_items = 11;
#ifdef _DEBUG
		total_items = 13;
#endif
		if (total_items > GRAPHICS_VISIBLE_ITEMS)
		{
			int scroll_x = sX + CONTENT_X + CONTENT_WIDTH - graphics_scroll_w / 2;
			int track_top = sY + CONTENT_Y;
			int track_bottom = sY + CONTENT_Y + CONTENT_HEIGHT;
			if (mouse_x >= scroll_x && mouse_x <= scroll_x + graphics_scroll_w &&
				mouse_y >= track_top && mouse_y <= track_bottom)
			{
				s_bDraggingGraphicsScroll = true;
				m_is_scroll_selected = true;
				return PressResult::ScrollClaimed;
			}
		}
	}

	// Only claim scroll for Audio tab slider areas
	if (m_iActiveTab != TAB_AUDIO)
		return PressResult::Normal;

	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	const int sliderX = contentX + 110;

	// Helper lambda for slider hit detection
	auto checkSlider = [&](int sliderY, bool& dragFlag) -> bool {
		if ((mouse_x >= sliderX) && (mouse_x <= sliderX + box::slider_w + 10) &&
			(mouse_y >= sliderY - 5) && (mouse_y <= sliderY + 15))
		{
			dragFlag = true;
			m_is_scroll_selected = true;
			return true;
		}
		return false;
	};

	// Master slider at contentY + 8
	if (checkSlider(contentY + 8, s_bDraggingMasterSlider))
		return PressResult::ScrollClaimed;

	// Effects slider at contentY + 52
	if (checkSlider(contentY + 52, s_bDraggingEffectsSlider))
		return PressResult::ScrollClaimed;

	// Ambient slider at contentY + 92
	if (checkSlider(contentY + 92, s_bDraggingAmbientSlider))
		return PressResult::ScrollClaimed;

	// UI slider at contentY + 132
	if (checkSlider(contentY + 132, s_bDraggingUISlider))
		return PressResult::ScrollClaimed;

	// Music slider at contentY + 172
	if (checkSlider(contentY + 172, s_bDraggingMusicSlider))
		return PressResult::ScrollClaimed;

	return PressResult::Normal;
}

bool DialogBox_SysMenu::on_click_general()
{
	// Log-Out / Continue button
	if (mouse_in(general_btn_left))
	{
		if (!m_game->m_force_disconn)
		{
			if (m_game->on_game()->m_logout_count == -1) {
				m_game->on_game()->m_logout_count = 11;
				m_game->on_game()->m_logout_count_time = GameClock::get_time_ms();
			}
			else {
				m_game->on_game()->m_logout_count = -1;
				add_event_list(DLGBOX_CLICK_SYSMENU2, 10);
				disable_this_dialog();
			}
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	// Restart button (only when dead)
	if ((player().m_hp <= 0) && (m_game->m_restart_count == -1))
	{
		if (mouse_in(general_btn_right))
		{
			m_game->m_restart_count = 5;
			m_game->m_restart_count_time = GameClock::get_time_ms();
			disable_this_dialog();
			std::string restartBuf;
			restartBuf = std::format(DLGBOX_CLICK_SYSMENU1, m_game->m_restart_count);
			add_event_list(restartBuf.c_str(), 10);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	return false;
}

bool DialogBox_SysMenu::on_click_graphics(short sX, short sY)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;

	// Match draw positions - right-align large box, left-align others to its left edge
	const int contentRight = contentX + CONTENT_WIDTH;
	const int rightMargin = 8;
	const int boxRightEdge = contentRight - rightMargin;
	const int largeBoxX = boxRightEdge - box::large_w;
	const int wideBoxX = largeBoxX;
	const int smallBoxX = largeBoxX;

	const bool fullscreen = m_game->m_Renderer->is_fullscreen();

	// Scroll offset applied to lineY
	int lineY = contentY + 5 - (m_graphics_scroll_offset * GRAPHICS_LINE_HEIGHT);

	auto is_item_visible = [&](int ly) {
		return (ly >= contentY - 2) && (ly + 16 <= contentY + CONTENT_HEIGHT);
	};

	// --- FPS Limit --- (disabled when VSync is on)
	if (is_item_visible(lineY))
	{
		const bool v_sync_on = config_manager::get().is_vsync_enabled();
		const int fpsBoxY = lineY - 2;
		static const int s_FpsOptions[] = { 60, 100, 144, 240, 0 };
		static const int s_NumFpsOptions = 5;
		const int fpsRegionWidth = box::large_w / s_NumFpsOptions;

		if (!v_sync_on && mouse_y >= fpsBoxY && mouse_y <= fpsBoxY + box::large_h && mouse_x >= largeBoxX && mouse_x <= largeBoxX + box::large_w) {
			int clickedRegion = (mouse_x - largeBoxX) / fpsRegionWidth;
			if (clickedRegion >= 0 && clickedRegion < s_NumFpsOptions) {
				int newLimit = s_FpsOptions[clickedRegion];
				config_manager::get().set_fps_limit(newLimit);
				hb::shared::render::Window::get()->set_framerate_limit(newLimit);
				audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
				return true;
			}
		}
	}

	lineY += 18;

	// --- Aspect Ratio --- (only enabled when fullscreen)
	if (is_item_visible(lineY))
	{
		const int aspectBoxY = lineY - 2;
		const int aspectRegionWidth = box::wide_w / 2;
		if (fullscreen && mouse_y >= aspectBoxY && mouse_y <= aspectBoxY + box::wide_h && mouse_x >= wideBoxX && mouse_x <= wideBoxX + box::wide_w) {
			bool new_stretch = (mouse_x >= wideBoxX + aspectRegionWidth);
			config_manager::get().set_fullscreen_stretch_enabled(new_stretch);
			hb::shared::render::Window::get()->set_fullscreen_stretch(new_stretch);
			if (hb::shared::render::Renderer::get())
				hb::shared::render::Renderer::get()->set_fullscreen_stretch(new_stretch);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- VSync toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_vsync_enabled();
			config_manager::get().set_vsync_enabled(!enabled);
			hb::shared::render::Window::get()->set_vsync_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Detail Level --- wide box with three regions
	if (is_item_visible(lineY))
	{
		const int boxY = lineY - 2;
		const int regionWidth = box::wide_w / 3;
		if (mouse_y >= boxY && mouse_y <= boxY + box::wide_h && mouse_x >= wideBoxX && mouse_x <= wideBoxX + box::wide_w) {
			if (mouse_x < wideBoxX + regionWidth) {
				config_manager::get().set_detail_level(0);
				add_event_list(NOTIFY_MSG_DETAIL_LEVEL_LOW, 10);
				audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
				return true;
			}
			if (mouse_x < wideBoxX + (regionWidth * 2)) {
				config_manager::get().set_detail_level(1);
				add_event_list(NOTIFY_MSG_DETAIL_LEVEL_MEDIUM, 10);
				audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
				return true;
			}
			config_manager::get().set_detail_level(2);
			add_event_list(NOTIFY_MSG_DETAIL_LEVEL_HIGH, 10);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Dialog Transparency toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_dialog_transparency_enabled();
			config_manager::get().set_dialog_transparency_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Show FPS toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_show_fps_enabled();
			config_manager::get().set_show_fps_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Show Latency toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_show_latency_enabled();
			config_manager::get().set_show_latency_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Background FPS Throttle toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_background_fps_throttle_enabled();
			config_manager::get().set_background_fps_throttle_enabled(!enabled);
			hb::shared::render::Window::get()->set_background_fps_limit(enabled ? 0 : 5);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Map Zoom toggle ---
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_zoom_map_enabled();
			config_manager::get().set_zoom_map_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

#ifdef _DEBUG
	// Tile Grid toggle - DEBUG ONLY
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_tile_grid_enabled();
			config_manager::get().set_tile_grid_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// Patching Grid toggle - DEBUG ONLY
	if (is_item_visible(lineY))
	{
		if (is_in_toggle_area(smallBoxX, lineY)) {
			bool enabled = config_manager::get().is_patching_grid_enabled();
			config_manager::get().set_patching_grid_enabled(!enabled);
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;
#endif

	// --- Display Mode toggle --- (wide box, toggles windowed/fullscreen)
	if (is_item_visible(lineY))
	{
		const int modeBoxY = lineY - 2;
		if (mouse_x >= wideBoxX && mouse_x <= wideBoxX + box::wide_w && mouse_y >= modeBoxY && mouse_y <= modeBoxY + box::wide_h) {
			m_game->m_Renderer->set_fullscreen(!fullscreen);
			m_game->m_Renderer->change_display_mode(hb::shared::render::Window::get_handle());
			hb::shared::input::get()->set_window_active(true);
			config_manager::get().set_fullscreen_enabled(!fullscreen);
			config_manager::get().save();
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- hb::shared::render::Window Style toggle --- (wide box, only in windowed mode)
	if (is_item_visible(lineY))
	{
		const int styleBoxY = lineY - 2;
		if (!fullscreen && mouse_x >= wideBoxX && mouse_x <= wideBoxX + box::wide_w && mouse_y >= styleBoxY && mouse_y <= styleBoxY + box::wide_h) {
			bool borderless = config_manager::get().is_borderless_enabled();
			config_manager::get().set_borderless_enabled(!borderless);
			hb::shared::render::Window::set_borderless(!borderless);
			hb::shared::input::get()->set_window_active(true);
			config_manager::get().save();
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
	}

	lineY += 18;

	// --- Resolution click --- (wide box, only in windowed mode)
	if (is_item_visible(lineY))
	{
		const int resBoxY = lineY - 2;
		if (!fullscreen && mouse_x >= wideBoxX && mouse_x <= wideBoxX + box::wide_w && mouse_y >= resBoxY && mouse_y <= resBoxY + box::wide_h) {
			cycle_resolution();
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			add_event_list("Resolution changed.", 10);
			return true;
		}
	}

	return false;
}

bool DialogBox_SysMenu::on_click_audio(short sX, short sY)
{
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	const int toggleX = contentX + 68;

	if (!audio_manager::get().is_sound_available())
		return false;

	// Master toggle (lineY = contentY + 8)
	int lineY = contentY + 8;
	if (is_in_toggle_area(toggleX, lineY)) {
		bool enabled = audio_manager::get().is_master_enabled();
		audio_manager::get().set_master_enabled(!enabled);
		config_manager::get().set_master_enabled(!enabled);
		config_manager::get().save();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	// Effects toggle (lineY = contentY + 52)
	lineY = contentY + 52;
	if (is_in_toggle_area(toggleX, lineY)) {
		bool enabled = audio_manager::get().is_sound_enabled();
		audio_manager::get().set_sound_enabled(!enabled);
		config_manager::get().set_sound_enabled(!enabled);
		config_manager::get().save();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	// Ambient toggle (lineY = contentY + 92)
	lineY = contentY + 92;
	if (is_in_toggle_area(toggleX, lineY)) {
		bool enabled = audio_manager::get().is_ambient_enabled();
		audio_manager::get().set_ambient_enabled(!enabled);
		config_manager::get().set_ambient_enabled(!enabled);
		config_manager::get().save();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	// UI toggle (lineY = contentY + 132)
	lineY = contentY + 132;
	if (is_in_toggle_area(toggleX, lineY)) {
		bool enabled = audio_manager::get().is_ui_enabled();
		audio_manager::get().set_ui_enabled(!enabled);
		config_manager::get().set_ui_enabled(!enabled);
		config_manager::get().save();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	// Music toggle (lineY = contentY + 172)
	lineY = contentY + 172;
	if (is_in_toggle_area(toggleX, lineY)) {
		if (audio_manager::get().is_music_enabled()) {
			audio_manager::get().set_music_enabled(false);
			config_manager::get().set_music_enabled(false);
			audio_manager::get().stop_music();
		}
		else {
			audio_manager::get().set_music_enabled(true);
			config_manager::get().set_music_enabled(true);
			m_game->start_bgm();
		}
		config_manager::get().save();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	return false;
}

bool DialogBox_SysMenu::on_click_system(short sX, short sY)
{
	const int contentX = sX + CONTENT_X;
	const int contentY = sY + CONTENT_Y;
	int lineY = contentY + 5;
	const int valueX = contentX + 140;

	// Whisper toggle
	if (is_in_toggle_area(valueX, lineY)) {
		if (ChatManager::get().is_whisper_enabled()) {
			ChatManager::get().set_whisper_enabled(false);
			add_event_list(BCHECK_LOCAL_CHAT_COMMAND7, 10);
		}
		else {
			ChatManager::get().set_whisper_enabled(true);
			add_event_list(BCHECK_LOCAL_CHAT_COMMAND6, 10);
		}
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Shout toggle
	if (is_in_toggle_area(valueX, lineY)) {
		if (ChatManager::get().is_shout_enabled()) {
			ChatManager::get().set_shout_enabled(false);
			add_event_list(BCHECK_LOCAL_CHAT_COMMAND9, 10);
		}
		else {
			ChatManager::get().set_shout_enabled(true);
			add_event_list(BCHECK_LOCAL_CHAT_COMMAND8, 10);
		}
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Running Mode toggle
	if (is_in_toggle_area(valueX, lineY)) {
		bool enabled = config_manager::get().is_running_mode_enabled();
		config_manager::get().set_running_mode_enabled(!enabled);
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Capture Mouse toggle
	if (is_in_toggle_area(valueX, lineY)) {
		bool enabled = config_manager::get().is_mouse_capture_enabled();
		config_manager::get().set_mouse_capture_enabled(!enabled);
		hb::shared::render::Window::get()->set_mouse_capture_enabled(!enabled);
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Guide Map toggle
	if (is_in_toggle_area(valueX, lineY)) {
		if (m_game->get_dialog_box_manager().is_enabled(DialogBoxId::GuideMap))
			disable_dialog_box(DialogBoxId::GuideMap);
		else
			enable_dialog_box(DialogBoxId::GuideMap, 0, 0, 0);
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Reduced Motion toggle
	if (is_in_toggle_area(valueX, lineY)) {
		bool enabled = config_manager::get().is_reduced_motion_enabled();
		config_manager::get().set_reduced_motion_enabled(!enabled);
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	lineY += 20;

	// Toggle to Chat toggle
	if (is_in_toggle_area(valueX, lineY)) {
		bool enabled = config_manager::get().is_toggle_to_chat_enabled();
		config_manager::get().set_toggle_to_chat_enabled(!enabled);
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	return false;
}
