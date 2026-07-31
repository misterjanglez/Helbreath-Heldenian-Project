#pragma once

#include "Render/PrimitiveTypes.h"

#include <cstdint>
#include <string>
#include <vector>

// A floating item panel in the same visual language as the dialogs: captions in
// a left column, values right-aligned against the panel's own edge, and rules
// between groups.
//
// The value column is the point. A Legendary item carries four rolled lines on
// top of its base stats, and the flow layout this replaces drew each value
// immediately after its caption with no separator at all — so nothing lined up
// down the panel, and every catalog row whose label did not happen to end in a
// space rendered as "Hitting Probability+35". Five of the forty rows had that
// trailing space; the rest did not.
//
// Rows keep their caption and value apart rather than composing one string, so
// a second value column — the worn item, for comparison — can be added later
// without re-deriving any of the text.
class item_tooltip
{
public:
	void clear();

	// The item's name, in whatever color the caller resolved (Tier, dye tint,
	// special, or plain). Drawn at the body size deliberately: measure_text
	// takes no style, so a larger name could not be measured and the panel
	// could not be sized to fit it. Color carries the emphasis instead.
	void add_header(const std::string& text, const hb::shared::render::Color& color);

	// The classification line under the name.
	void add_subhead(const std::string& text);

	// A rule between groups. Leading, trailing and repeated rules are dropped
	// when the panel is drawn, so a caller can close every group with one
	// without knowing whether the next group turns out to be empty.
	void add_rule();

	// Caption left, value right-aligned to the panel edge.
	void add_row(const std::string& caption, const std::string& value,
		const hb::shared::render::Color& value_color);

	// One line spanning the full width, for a sentence the modifier catalog
	// wrote itself ("Replace 5% damage to mana"). There is no value to put in a
	// column, and splitting one out would mean inventing a caption.
	void add_full(const std::string& text, const hb::shared::render::Color& color);

	// Draws with its top-left at (x, y), nudged back inside the window if the
	// panel would otherwise run off the right or bottom edge.
	void draw(int x, int y) const;

	bool empty() const { return m_lines.empty(); }

private:
	enum class kind : uint8_t { header, subhead, rule, row, full };

	struct line
	{
		kind type = kind::full;
		std::string text;                        // caption, or the whole line
		std::string value;                       // row only
		hb::shared::render::Color color{};
		hb::shared::render::Color value_color{};
	};

	// Tighter than a dialog's margins: this panel follows the cursor and a
	// fully rolled item runs to fourteen lines.
	static constexpr int pad_x      = 8;
	static constexpr int pad_y      = 6;
	static constexpr int line_h     = 15;
	static constexpr int header_gap = 3;    // extra air under the name
	static constexpr int rule_h     = 7;    // 3 above, the rule, 3 below
	static constexpr int col_gap    = 24;   // caption column to value column

	// Index one past the last line worth drawing — everything after it is
	// trailing rules.
	std::size_t visible_count() const;

	int content_width(std::size_t count) const;
	int content_height(std::size_t count) const;

	std::vector<line> m_lines;
};
