#include "ItemTooltip.h"

#include "GameFonts.h"
#include "UITheme.h"

#include "Render/RendererFactory.h"
#include "Render/TextLib.h"

#include <algorithm>

namespace
{
	namespace text = hb::shared::text;
	namespace theme = hb::client::ui_theme;

	int measure(const std::string& string)
	{
		return string.empty() ? 0 : text::measure_text(GameFont::Default, string.c_str()).width;
	}

	// One line of text in the panel's own box. Shadowed because the panel is
	// translucent and the world behind it can be any brightness — which is the
	// one thing a dialog row never has to cope with, and why this does not go
	// through ui_theme::label.
	void put(int x, int y, int w, int h, const std::string& string,
		const hb::shared::render::Color& color, text::Align alignment)
	{
		if (string.empty()) return;
		text::draw_text_aligned(GameFont::Default, x, y, w, h, string.c_str(),
			text::TextStyle::with_shadow(color), alignment);
	}
}

void item_tooltip::clear()
{
	m_lines.clear();
}

void item_tooltip::add_header(const std::string& text, const hb::shared::render::Color& color)
{
	if (text.empty()) return;
	m_lines.push_back({kind::header, text, {}, color, {}});
}

void item_tooltip::add_subhead(const std::string& text)
{
	if (text.empty()) return;
	m_lines.push_back({kind::subhead, text, {}, theme::palette::dim, {}});
}

void item_tooltip::add_rule()
{
	// A rule with nothing above it, or a second one in a row, would be a rule
	// separating nothing from nothing.
	if (m_lines.empty() || m_lines.back().type == kind::rule) return;
	m_lines.push_back({kind::rule, {}, {}, {}, {}});
}

void item_tooltip::add_row(const std::string& caption, const std::string& value,
	const hb::shared::render::Color& value_color)
{
	if (caption.empty() && value.empty()) return;
	m_lines.push_back({kind::row, caption, value, theme::palette::label, value_color});
}

void item_tooltip::add_full(const std::string& text, const hb::shared::render::Color& color)
{
	if (text.empty()) return;
	m_lines.push_back({kind::full, text, {}, color, {}});
}

std::size_t item_tooltip::visible_count() const
{
	std::size_t count = m_lines.size();
	while (count > 0 && m_lines[count - 1].type == kind::rule) count--;
	return count;
}

int item_tooltip::content_width(std::size_t count) const
{
	int widest = 0;
	for (std::size_t i = 0; i < count; i++)
	{
		const line& row = m_lines[i];
		if (row.type == kind::rule) continue;

		// A row has to fit both columns and the gutter between them; everything
		// else only has to fit itself. A row that ended up with no value is
		// measured as a plain line, so it cannot reserve a gutter for a second
		// column that will not be drawn.
		int width = measure(row.text);
		if (row.type == kind::row && !row.value.empty())
			width += col_gap + measure(row.value);

		widest = std::max(widest, width);
	}
	return widest;
}

int item_tooltip::content_height(std::size_t count) const
{
	int height = 0;
	for (std::size_t i = 0; i < count; i++)
	{
		switch (m_lines[i].type)
		{
		case kind::rule:   height += rule_h; break;
		case kind::header: height += line_h + header_gap; break;
		default:           height += line_h; break;
		}
	}
	return height;
}

void item_tooltip::draw(int x, int y) const
{
	const std::size_t count = visible_count();
	if (count == 0) return;

	const int w = content_width(count) + pad_x * 2;
	const int h = content_height(count) + pad_y * 2;

	// The panel follows the cursor, so at the right or bottom edge of the
	// window it would be clipped exactly when it is most wanted — a pile of
	// loot on the last visible tile.
	if (const auto* renderer = hb::shared::render::Renderer::get())
	{
		x = std::min(x, renderer->get_width() - w - 2);
		y = std::min(y, renderer->get_height() - h - 2);
	}
	x = std::max(x, 2);
	y = std::max(y, 2);

	theme::panel(x, y, w, h);

	const int left = x + pad_x;
	const int inner_w = w - pad_x * 2;
	int row_y = y + pad_y;

	for (std::size_t i = 0; i < count; i++)
	{
		const line& row = m_lines[i];

		if (row.type == kind::rule)
		{
			theme::separator(left, row_y + rule_h / 2, inner_w);
			row_y += rule_h;
			continue;
		}

		// Every remaining kind puts its text at the left edge of the row and
		// differs only in what else it draws and how far it then advances.
		put(left, row_y, inner_w, line_h, row.text, row.color, text::Align::TopLeft);
		if (row.type == kind::row)
			put(left, row_y, inner_w, line_h, row.value, row.value_color, text::Align::TopRight);

		row_y += line_h + (row.type == kind::header ? header_gap : 0);
	}
}
