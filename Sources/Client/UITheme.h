#pragma once

// Flat-panel dialog styling drawn from renderer primitives instead of pak
// sprites.
//
// The pak-sprite approach bakes layout into art: a label column, the value
// boxes and the row pitch all live in a PNG, so the drawing code carries
// hand-measured offsets that must be re-measured whenever the art changes, and
// a panel can never be resized or relabelled without regenerating an atlas.
// Drawing the chrome from primitives moves layout back into code, which is what
// lets a dialog size itself to its content and lets labels be real strings.
//
// Text is TTF throughout (GameFont::Default resolves to fonts/default.ttf,
// which is Tahoma) — no bitmap glyph sheets, including for titles.
//
// The visual target is a translucent warm near-black panel: dark enough to read
// against, light enough that the world stays visible behind it. Captions are
// near-white and left-aligned; values carry the colour, because colour is what
// makes a dense stat list scannable without boxing every field.

#include "CommonTypes.h"

#include <cstdint>

namespace hb::client::ui_theme
{
	using color = hb::shared::render::Color;

	namespace palette
	{
		// --- surfaces -------------------------------------------------------
		// Alpha is deliberately well below opaque. A solid panel turns the game
		// into a series of full-screen modals; keeping the world faintly visible
		// is most of what makes this style feel light.
		inline constexpr color panel        { 18,  16,  14, 196 };
		inline constexpr color panel_edge   { 96,  90,  78, 255 };
		inline constexpr color tab_bar      { 30,  28,  24, 205 };
		inline constexpr color content      { 12,  11,   9, 150 };  // inner well
		inline constexpr color content_edge {110, 104,  90, 255 };
		inline constexpr color button       { 34,  31,  26, 214 };
		inline constexpr color button_hover { 62,  56,  46, 228 };
		inline constexpr color button_edge  {150, 145, 132, 255 };
		inline constexpr color separator    { 96,  90,  78, 160 };
		inline constexpr color scroll_track { 10,   9,   8, 170 };
		inline constexpr color scroll_thumb { 96,  90,  78, 235 };

		// --- text -----------------------------------------------------------
		// "Body text" and "disabled" are aliases rather than fresh values:
		// GameColors::UILabel and UIDisabled were re-tuned for exactly these dark
		// panels (see CommonTypes.h), so a second near-identical pair here would
		// be two spellings of one colour, free to drift. The names still read as
		// theme vocabulary; the value has one home.
		inline constexpr color label        = GameColors::UILabel;    // captions
		inline constexpr color title        = GameColors::UILabel;
		inline constexpr color dim          = GameColors::UIDisabled; // disabled

		// Value colours are the theme's own: colour is what makes a dense stat
		// list scannable, and nothing in GameColors carried that role.
		inline constexpr color value        { 96, 224,  96, 255 };  // ordinary values
		inline constexpr color value_hi     {228, 205,  90, 255 };  // notable values

		inline constexpr color tab_active   { 96, 224,  96, 255 };
		inline constexpr color tab_idle     {150, 146, 138, 255 };
	}

	// Shared metrics. A dialog lays out its own content, but sharing the row
	// pitch and margins is what makes separate dialogs look like one program.
	namespace metrics
	{
		inline constexpr int margin      = 10;
		inline constexpr int row_height  = 17;   // one caption/value line
		inline constexpr int row_pitch   = 20;   // row_height + gutter
		inline constexpr int tab_height  = 22;   // the bar; a tab sits 1px inset
		inline constexpr int title_size  = 14;   // TTF point size for titles

		// Grab bands are inflated by this much over the chrome they target. A
		// 15px-wide scrollbar is a fiddly thing to catch, and every dialog whose
		// hand-written band this replaces already accepted more than it drew.
		inline constexpr int grab_pad    = 5;
	}

	// The scrollbar gutter the six list dialogs share (bank, shop, npc talk,
	// repair, skills, text), panel-relative.
	//
	// All six positioned a 15x15 pak thumb with a (-6,-5) pivot at
	// (x + 242, y + 35 + offset) over 274px of travel, and all six carried their
	// own copy of those numbers plus a separate, differently-sized hand-written
	// band to hit-test. Stating the geometry once is what keeps the track a
	// dialog draws and the band it clicks from disagreeing.
	inline constexpr int list_thumb  = 15;
	inline constexpr int list_travel = 274;
	inline constexpr ui_rect list_gutter{ 242 - 6, 35 - 5, list_thumb, list_travel + list_thumb };

	// A rect inflated to a comfortable click target.
	constexpr ui_rect grab_area(const ui_rect& r)
	{
		return { r.x - metrics::grab_pad, r.y - metrics::grab_pad,
		         r.w + metrics::grab_pad * 2, r.h + metrics::grab_pad * 2 };
	}

	// Body plus 1px rim. Draw first; everything else sits on top.
	void panel(int x, int y, int w, int h);

	// Slightly lighter strip across the top of a panel, behind a row of tabs.
	void tab_bar(int x, int y, int w, int h = metrics::tab_height);

	// One tab caption. The active tab is coloured and underlined rather than
	// given a raised face — an underline reads as selection at any panel width.
	void tab(int x, int y, int w, int h, const char* text, bool active);

	// Inset well with a thin rim — the paperdoll / list background.
	void content_frame(int x, int y, int w, int h);

	// Horizontal rule.
	void separator(int x, int y, int w);

	// Centred title in TTF at metrics::title_size.
	void title(int x, int y, int w, const char* text);

	// Panel header: a band across the top of the panel with a centred caption
	// and a rule beneath it. This is the flat-panel replacement for the pak
	// title strips, which were separate small frames with the lettering painted
	// in — small enough that the size-gated sprite swap leaves them alone, so a
	// dialog is only truly free of pak art once it calls this instead.
	void header(int x, int y, int w, const char* text);

	// Vertical scrollbar. The track is drawn explicitly because a flat panel has
	// no groove baked into it the way the art did.
	//
	// thumb_offset is measured from the top of the track, matching how the pak
	// versions positioned their thumb sprite, so a caller porting from art does
	// not have to re-derive its scroll maths.
	void scrollbar(int x, int y, int w, int track_h, int thumb_offset, int thumb_h);

	// The list scrollbar, drawn into list_gutter. thumb_offset is the caller's
	// existing pointer_loc, unchanged.
	//
	// This is the whole gutter now: the panel art carried a 23x339 vertical rail
	// on its own frame that the dialogs drew here, and at 23px wide it slipped
	// under the size gate that flattens panels, so it survived as the one strip
	// of brass on an otherwise flat dialog. The track below replaces it.
	void list_scrollbar(int dialog_x, int dialog_y, int thumb_offset);

	// Horizontal slider: a thin groove with a square handle riding it.
	//
	// value_offset is the handle's distance from the left end of the groove. The
	// settings dialog stores volumes 0-100 and a 100px groove, so the value is
	// already the offset; a caller with a different range scales it first.
	void slider(int x, int y, int w, int value_offset);

	// Left-aligned caption, vertically centred in a metrics::row_height row.
	void label(int x, int y, const char* text, const color& text_color = palette::label);

	// Right-aligned value ending at x + w.
	void value(int x, int y, int w, const char* text, const color& text_color = palette::value);

	// A caption/value pair on one row. Returns the y of the next row so callers
	// can stack rows without restating the pitch.
	int label_value(int x_label, int x_value, int value_w, int y,
	                const char* caption, const char* text,
	                const color& value_color = palette::value);

	// Flat button face plus centred caption.
	void button(int x, int y, int w, int h, const char* text, bool hovered, bool enabled = true);
}
