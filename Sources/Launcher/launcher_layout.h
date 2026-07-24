#pragma once

// Shared launcher window geometry. Both GUI backends (Win32 / X11) draw the
// same rects so the two platforms stay pixel-identical by construction.
// Everything derives from a 480x270-era base design times ui_scale; change
// ui_scale (and regenerate launcher_art.h at the matching resolution via
// tools/gen_launcher_art.py) to resize the whole window.

namespace hb::launcher
{
	struct ui_rect
	{
		int x;
		int y;
		int w;
		int h;

		bool contains(int px, int py) const
		{
			return px >= x && px < x + w && py >= y && py < y + h;
		}
	};

	namespace layout
	{
		// Single knob: window/widget/font scale over the 480x360 base design.
		// launcher_art.h must be baked at (480*ui_scale) x (360*ui_scale).
		constexpr int ui_scale = 3;

		constexpr int window_width = 480 * ui_scale;
		constexpr int window_height = 360 * ui_scale;

		constexpr const char* window_title = "Helbreath: Medieval Times";

		// Font pixel heights (backends use these so both platforms match)
		constexpr int font_normal = 13 * ui_scale;
		constexpr int font_bold = 14 * ui_scale;
		constexpr int font_small = 10 * ui_scale;

		constexpr ui_rect scaled(int x, int y, int w, int h)
		{
			return {x * ui_scale, y * ui_scale, w * ui_scale, h * ui_scale};
		}

		// Status line (state text: "Checking for updates...", errors, hints)
		constexpr ui_rect status_line = scaled(16, 258, 448, 20);

		// Progress bar (visible while installing/updating) and the install
		// row (path field + browse) share the same slot — never both visible.
		constexpr ui_rect progress_bar = scaled(16, 284, 448, 14);
		constexpr ui_rect path_field = scaled(16, 282, 356, 22);
		constexpr ui_rect browse_button = scaled(380, 282, 84, 22);

		// Options row: launch-mode and resolution cycle selectors
		constexpr ui_rect mode_selector = scaled(16, 314, 148, 26);
		constexpr ui_rect resolution_selector = scaled(172, 314, 120, 26);

		// Primary call-to-action: Play Game / Update / Install / Retry
		constexpr ui_rect primary_button = scaled(344, 310, 120, 34);

		// Version line ("Installed: x  |  Latest: y")
		constexpr ui_rect version_line = scaled(16, 342, 320, 14);
	}
}
