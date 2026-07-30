#include "UITheme.h"

#include "GameFonts.h"

#include "Render/RendererFactory.h"
#include "Render/TextLib.h"

#include <algorithm>

namespace hb::client::ui_theme
{
	namespace
	{
		namespace text = hb::shared::text;

		hb::shared::render::IRenderer& renderer()
		{
			return *hb::shared::render::Renderer::get();
		}

		// Every caption and value goes through one call so the font and the
		// vertical centring rule stay in one place. Callers give the row origin,
		// not a baseline — the row is metrics::row_height tall and the glyphs
		// centre inside it.
		void row_text(int x, int y, int w, const char* string,
		              const color& text_color, text::Align alignment)
		{
			text::draw_text_aligned(GameFont::Default, x, y, w, metrics::row_height, string,
			                        text::TextStyle::from_color(text_color), alignment);
		}
	}

	void panel(int x, int y, int w, int h)
	{
		renderer().draw_rect_filled(x, y, w, h, palette::panel);
		renderer().draw_rect_outline(x, y, w, h, palette::panel_edge, 1);
	}

	void tab_bar(int x, int y, int w, int h)
	{
		renderer().draw_rect_filled(x, y, w, h, palette::tab_bar);
	}

	void tab(int x, int y, int w, int h, const char* text_string, bool active)
	{
		text::draw_text_aligned(GameFont::Default, x, y, w, h, text_string,
		                        text::TextStyle::from_color(active ? palette::tab_active
		                                                           : palette::tab_idle),
		                        text::Align::Center);

		// The underline is the selection cue. It is inset by a couple of pixels
		// so adjacent active tabs would never appear to share one rule.
		if (active)
			renderer().draw_rect_filled(x + 2, y + h - 2, w - 4, 2, palette::tab_active);
	}

	void content_frame(int x, int y, int w, int h)
	{
		renderer().draw_rect_filled(x, y, w, h, palette::content);
		renderer().draw_rect_outline(x, y, w, h, palette::content_edge, 1);
	}

	void separator(int x, int y, int w)
	{
		renderer().draw_rect_filled(x, y, w, 1, palette::separator);
	}

	void title(int x, int y, int w, const char* text_string)
	{
		text::draw_text_aligned(GameFont::Default, x, y, w, metrics::tab_height, text_string,
		                        text::TextStyle::from_color(palette::title)
		                            .with_font_size(metrics::title_size),
		                        text::Align::Center);
	}

	void header(int x, int y, int w, const char* text_string)
	{
		tab_bar(x + 1, y + 1, w - 2);
		title(x, y + 1, w, text_string);
		separator(x + 1, y + metrics::tab_height + 1, w - 2);
	}

	void scrollbar(int x, int y, int w, int track_h, int thumb_offset, int thumb_h)
	{
		renderer().draw_rect_filled(x, y, w, track_h, palette::scroll_track);
		renderer().draw_rect_outline(x, y, w, track_h, palette::separator, 1);

		// Clamp so a caller whose scroll maths runs a pixel past the end cannot
		// draw a thumb hanging outside its own track.
		const int height = std::min(thumb_h, track_h);
		const int offset = std::clamp(thumb_offset, 0, track_h - height);

		renderer().draw_rect_filled(x + 1, y + offset + 1, w - 2, std::max(height - 2, 1),
		                            palette::scroll_thumb);
	}

	void list_scrollbar(int dialog_x, int dialog_y, int thumb_offset)
	{
		scrollbar(dialog_x + list_gutter.x, dialog_y + list_gutter.y,
		          list_gutter.w, list_gutter.h, thumb_offset, list_thumb);
	}

	void slider(int x, int y, int w, int value_offset)
	{
		constexpr int handle = 13;
		constexpr int groove = 3;
		const int groove_y = y + handle / 2 - groove / 2;

		renderer().draw_rect_filled(x, groove_y, w, groove, palette::scroll_track);
		renderer().draw_rect_outline(x, groove_y, w, groove, palette::separator, 1);

		const int at = std::clamp(value_offset, 0, w);
		renderer().draw_rect_filled(x + at - handle / 2, y, handle, handle, palette::scroll_thumb);
		renderer().draw_rect_outline(x + at - handle / 2, y, handle, handle, palette::button_edge, 1);
	}

	void label(int x, int y, const char* text_string, const color& text_color)
	{
		// Left-aligned text needs no box to align against, so the width passed to
		// the aligner only has to be large enough not to clip.
		constexpr int loose = 400;
		row_text(x, y, loose, text_string, text_color, text::Align::MiddleLeft);
	}

	void value(int x, int y, int w, const char* text_string, const color& text_color)
	{
		row_text(x, y, w, text_string, text_color, text::Align::MiddleRight);
	}

	int label_value(int x_label, int x_value, int value_w, int y,
	                const char* caption, const char* text_string, const color& value_color)
	{
		label(x_label, y, caption);
		value(x_value, y, value_w, text_string, value_color);
		return y + metrics::row_pitch;
	}

	void button(int x, int y, int w, int h, const char* text_string, bool hovered, bool enabled)
	{
		renderer().draw_rect_filled(x, y, w, h, hovered && enabled ? palette::button_hover
		                                                          : palette::button);
		renderer().draw_rect_outline(x, y, w, h, palette::button_edge, 1);

		text::draw_text_aligned(GameFont::Default, x, y, w, h, text_string,
		                        text::TextStyle::from_color(enabled ? palette::label : palette::dim),
		                        text::Align::Center);
	}
}
