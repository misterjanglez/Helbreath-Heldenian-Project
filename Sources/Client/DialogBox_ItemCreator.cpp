// TESTER MENU — entire file is tester-only
#ifdef TESTER_ONLY
#include "DialogBox_ItemCreator.h"
#include "Game.h"
#include "GlobalDef.h"
#include "SpriteID.h"
#include "NetMessages.h"
#include "PacketSendHelpers.h"

#include "GameFonts.h"
#include "TextLibExt.h"
#include "TextInputManager.h"
#include "TextFieldRenderer.h"
#include "ItemNameFormatter.h"
#include "Item/ItemEnums.h"
#include <algorithm>
#include <format>
#include <cstring>
#include "IInput.h"
#include "AudioManager.h"

using namespace hb::shared::net;
using namespace hb::client::sprite_id;
using namespace hb::shared::item;
using render_color = hb::shared::render::Color;

// Layout constants
namespace layout
{
	// Shared
	constexpr int pad = 12;
	constexpr int content_x1 = 12;
	constexpr int content_x2 = 246;
	constexpr int content_w = content_x2 - content_x1;

	// Search page
	constexpr int search_bar_y = 30;
	constexpr int search_bar_h = 20;
	constexpr int list_y = 56;
	constexpr int row_h = 18;
	constexpr int list_rows = 12;
	constexpr int list_h = list_rows * row_h;
	constexpr int status_y = list_y + list_h + 2;

	// Configure page — two-column layout
	constexpr int item_info_y = 38;    // "Dagger (Weapon)" combined line

	// Column boundaries (inset from frame edges)
	constexpr int col_left_x1 = 22;
	constexpr int col_left_x2 = 122;
	constexpr int col_left_w = col_left_x2 - col_left_x1;
	constexpr int col_right_x1 = 136;
	constexpr int col_right_x2 = 236;
	constexpr int col_right_w = col_right_x2 - col_right_x1;

	// Row 1: First Stat / Second Stat type dropdowns
	constexpr int row1_label_y = 62;
	constexpr int row1_sel_y = 78;

	// Row 2: Value dropdowns
	constexpr int row2_label_y = 100;
	constexpr int row2_sel_y = 116;

	// Row 3: Upgrade / Count dropdowns
	constexpr int row3_label_y = 138;
	constexpr int row3_sel_y = 154;

	// Preview + buttons
	constexpr int preview_label_y = 180;
	constexpr int preview_text_y = 198;
	constexpr int btn_y = 234;
	constexpr int btn_w = 100;

	// --- Tiered configure page (Item Tiers 4-D) ---------------------------
	// The frame is a fixed 258x339 sprite, so the vertical budget is sized
	// against the worst case: a Legendary with four modifier rows.
	constexpr int t_col_w = 68;                    // three columns across the top row
	constexpr int t_col1_x = 22;
	constexpr int t_col2_x = 95;
	constexpr int t_col3_x = 168;
	constexpr int t_top_label_y = 56;
	constexpr int t_top_sel_y = 70;
	constexpr int t_slot_label_y = 90;
	constexpr int t_slot_y = 104;                  // first modifier row
	constexpr int t_slot_pitch = 18;
	constexpr int t_type_x = 22;
	constexpr int t_type_w = 140;
	constexpr int t_value_x = 166;
	constexpr int t_value_w = 70;
	constexpr int t_pair_w = 34;                   // a pair splits the value column
	constexpr int t_pair2_x = 202;
	constexpr int t_preview_y = 180;
	constexpr int t_notice_y = 198;                // server reply, up to two lines
	constexpr int t_notice_pitch = 13;
	constexpr int t_notice_chars = 44;             // wrap width at GameFont::Default
}

// Dropdown visual style — warm tones to match parchment dialog background
namespace dd_style
{
	const auto bg           = render_color(40, 35, 28, 190);
	const auto border       = render_color(80, 70, 50);
	const auto border_hover = render_color(140, 125, 90);
	const auto border_open  = render_color(180, 160, 100);
	const auto list_bg      = render_color(30, 25, 18, 235);
	const auto list_border  = render_color(100, 90, 60);
	const auto item_hover   = render_color(90, 75, 45, 180);
	const auto scrollbar    = render_color(130, 115, 75, 160);
}

DialogBox_ItemCreator::DialogBox_ItemCreator(CGame* game)
	: IDialogBox(DialogBoxId::ItemCreator, game)
{
	set_default_rect(0, 0, 258, 339);
	m_can_close_on_right_click = true;
}

bool DialogBox_ItemCreator::on_disable()
{
	text_input_manager::get().end_input();
	m_initial_load = false;
	m_last_sent_search.clear();
	m_open_dropdown = dropdown_id::none;
	reset_tier_state();
	return true;
}

DialogBox_ItemCreator::item_category DialogBox_ItemCreator::classify_item(int16_t effect_type)
{
	auto et = static_cast<ItemEffectType>(effect_type);
	if (et == ItemEffectType::AttackManaSave)
		return item_category::magic_weapon;
	if (is_attack_effect_type(et))
		return item_category::weapon;
	if (et == ItemEffectType::Defense)
		return item_category::armor;
	return item_category::none;
}

const char* DialogBox_ItemCreator::category_name(item_category cat)
{
	switch (cat)
	{
	case item_category::weapon:       return "Weapon";
	case item_category::armor:        return "Armor";
	case item_category::magic_weapon: return "Magic Weapon";
	default:                          return "Other";
	}
}

void DialogBox_ItemCreator::build_valid_options(int16_t effect_type)
{
	m_category = classify_item(effect_type);
	m_valid_prefixes.clear();
	m_valid_secondaries.clear();

	m_valid_prefixes.push_back({0, "None", 0});
	m_valid_secondaries.push_back({0, "None", 0});

	// Option ids are the legacy 4-bit nibble values the TesterCreateItem
	// bitmask carries (the server translates them to unified modifier IDs);
	// multiplier display lookups route through the one translation table.
	auto add_prefix = [this](uint8_t legacy_id, const char* name)
	{
		m_valid_prefixes.push_back({legacy_id, name, m_game->m_modifier_catalog[legacy_prefix_to_modifier_id(legacy_id)].multiplier});
	};
	auto add_secondary = [this](uint8_t legacy_id, const char* name)
	{
		m_valid_secondaries.push_back({legacy_id, name, m_game->m_modifier_catalog[legacy_secondary_to_modifier_id(legacy_id)].multiplier});
	};

	switch (m_category)
	{
	case item_category::weapon:
		add_prefix(1, "Critical");
		add_prefix(2, "Poisoning");
		add_prefix(3, "Righteous");
		add_prefix(5, "Agile");
		add_prefix(6, "Light");
		add_prefix(7, "Sharp");
		add_prefix(8, "Strong");
		add_prefix(9, "Ancient");
		add_secondary(2, "HitProb");
		add_secondary(10, "ConsecAtk");
		add_secondary(11, "ExpBonus");
		add_secondary(12, "GoldBonus");
		break;

	case item_category::armor:
		add_prefix(8, "Strong");
		add_prefix(6, "Light");
		add_prefix(11, "ManaConvert");
		add_prefix(12, "CritChance");
		add_secondary(3, "DefRatio");
		add_secondary(1, "PoisonRes");
		add_secondary(5, "SPRecov");
		add_secondary(4, "HPRecov");
		add_secondary(6, "MPRecov");
		add_secondary(7, "MagicRes");
		add_secondary(8, "PhysAbsorb");
		add_secondary(9, "MagicAbsorb");
		break;

	case item_category::magic_weapon:
		add_prefix(10, "Special");
		add_secondary(2, "HitProb");
		add_secondary(10, "ConsecAtk");
		add_secondary(11, "ExpBonus");
		add_secondary(12, "GoldBonus");
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// TIERED MODE (Item Tiers 4-D)
// ---------------------------------------------------------------------------

namespace {

// Picker label for a catalog row: the item-name prefix word when the row has
// one, else the tooltip label (trailing padding trimmed), clipped to what a
// dropdown column can actually render.
std::string catalog_label(const modifier_catalog_entry& row, uint8_t modifier_id)
{
	std::string label = row.display_name.empty() ? row.effect_label : row.display_name;
	while (!label.empty() && label.back() == ' ') label.pop_back();
	if (label.empty()) label = std::format("Modifier {}", (int)modifier_id);
	if (label.size() > 26) label.resize(26);
	return label;
}

} // namespace

void DialogBox_ItemCreator::reset_tier_state()
{
	m_tier = 0;
	for (auto& slot : m_tier_slots) slot = {};
	m_awaiting_mint_reply = false;
	m_server_notice.clear();
	m_server_notice_ok = false;
}

// Picking a Tier retires every slot the new Tier cannot carry: the ones past
// the count (count == tier, spec §3) and any modifier whose min-tier ladder
// now sits above it. Both rules are replicated, so the dialog can hold them
// without asking.
void DialogBox_ItemCreator::apply_tier_change(int tier)
{
	m_tier = tier;
	for (int i = 0; i < static_cast<int>(hb::shared::item::modifier_slot_count); i++)
	{
		const uint8_t id = m_tier_slots[i].modifier_id;
		if (i >= tier || (id != 0 && m_game->m_modifier_catalog[id].min_tier > tier))
			m_tier_slots[i] = {};
	}
}

std::vector<DialogBox_ItemCreator::tier_option> DialogBox_ItemCreator::tier_options_for_slot(int slot) const
{
	std::vector<tier_option> options;
	options.push_back({ 0, "None" });
	if (m_tier <= 0) return options;

	// A Bucket another slot already spoke for is absent rather than greyed:
	// the Bucket law is structural, and offering a pick the server must
	// refuse is not a choice.
	bool bucket_taken[256] = {};
	for (int i = 0; i < static_cast<int>(hb::shared::item::modifier_slot_count); i++)
	{
		if (i == slot) continue;
		const uint8_t id = m_tier_slots[i].modifier_id;
		if (id != 0) bucket_taken[m_game->m_modifier_catalog[id].bucket_id] = true;
	}

	for (int id = 1; id < 256; id++)
	{
		const auto& row = m_game->m_modifier_catalog[id];
		if (!row.present || row.min_tier > m_tier || bucket_taken[row.bucket_id]) continue;
		options.push_back({ static_cast<uint8_t>(id), catalog_label(row, static_cast<uint8_t>(id)) });
	}
	return options;
}

// The raw values a modifier may legally hold. Bands are display units and the
// stored byte is raw, so the legal raw window is the band divided by the
// multiplier — rounded INWARD at both ends, because a raw value whose display
// falls outside the band is exactly what the server rejects. An empty range
// means the row has no legal value at all (a value-less modifier like Agile,
// or a band the multiplier cannot land inside).
DialogBox_ItemCreator::raw_range DialogBox_ItemCreator::legal_raw_range(
	const modifier_catalog_entry& row)
{
	if (row.multiplier <= 0) return {};

	raw_range range;
	// Ceiling division for the low end, floor for the high end.
	range.min = std::max(1, (row.band_min + row.multiplier - 1) / row.multiplier);
	range.max = row.band_max / row.multiplier;
	if (range.max < range.min) return {};
	return range;
}

// Bands replicate as of #65, so the picker offers only values the server will
// accept instead of the whole raw span with the rejection string as the only
// teacher. Options read in display units, which is what the tooltip and the
// rejection string both speak.
std::vector<std::string> DialogBox_ItemCreator::value_options(const modifier_catalog_entry& row) const
{
	std::vector<std::string> options;
	const raw_range range = legal_raw_range(row);
	for (int raw = range.min; raw <= range.max; raw++)
		options.push_back(std::to_string(raw * row.multiplier));
	return options;
}

// The one local gate: count == tier is the invariant the pickers own, and an
// empty slot would be rejected on arrival with nothing to point at.
std::string DialogBox_ItemCreator::missing_tier_input() const
{
	for (int i = 0; i < m_tier; i++)
		if (m_tier_slots[i].modifier_id == 0)
			return std::format("Pick a modifier for all {} slots.", m_tier);
	return {};
}

hb::shared::item::item_attribute_data DialogBox_ItemCreator::build_requested_attributes() const
{
	hb::shared::item::item_attribute_data attributes{};
	attributes.tier = static_cast<uint8_t>(m_tier);
	attributes.enchant_bonus = static_cast<uint8_t>(m_enchant_value);

	for (int i = 0; i < m_tier; i++)
	{
		const auto& slot = m_tier_slots[i];
		if (slot.modifier_id == 0) continue;
		const auto& row = m_game->m_modifier_catalog[slot.modifier_id];
		attributes.modifiers[i].type = slot.modifier_id;
		// A value-less modifier must ride at 0 and a non-pair must leave
		// value2 at 0 — both are structural rules, so the picker never even
		// shows the field that would break them.
		attributes.modifiers[i].value = row.multiplier == 0 ? 0 : static_cast<uint8_t>(slot.value);
		attributes.modifiers[i].value2 = row.is_pair()
			? static_cast<uint8_t>(slot.value2) : 0;
	}
	return attributes;
}

void DialogBox_ItemCreator::send_tiered_mint()
{
	if (m_selected_index < 0 || m_selected_index >= m_result_count) return;

	hb::net::PacketCommandTesterCreateItemTiered pkt{};
	pkt.base.header.msg_id = hb::shared::net::MsgId::CommandCommon;
	pkt.base.header.msg_type = CommonType::TesterCreateItemTiered;
	pkt.base.x = player().m_player_x;
	pkt.base.y = player().m_player_y;
	pkt.item_id = m_results[m_selected_index].item_id;
	pkt.count = m_item_count;
	pkt.attributes = build_requested_attributes();

	m_awaiting_mint_reply = true;
	m_server_notice.clear();
	send_game_packet(pkt);
}

void DialogBox_ItemCreator::receive_server_notice(const char* text)
{
	m_awaiting_mint_reply = false;
	m_server_notice = text != nullptr ? text : "";
	// The server phrases an accepted mint as "Created Nx ..." and a refused
	// one as "Mint rejected: <reason>"; a partial run says both.
	m_server_notice_ok = m_server_notice.rfind("Created", 0) == 0;
}

std::string DialogBox_ItemCreator::build_preview_string() const
{
	if (m_selected_index < 0 || m_selected_index >= m_result_count)
		return "";

	// Tiered mode previews the real thing: the same POD the mint will carry,
	// run through the same formatter every other surface uses, so the Tier
	// word and the modifier lines are the server's own presentation data.
	if (m_game->is_tiered_mode())
	{
		hb::shared::item::item_instance_data data{};
		data.attributes = build_requested_attributes();
		return item_name_formatter::get().format(m_results[m_selected_index].item_id, data).name;
	}

	std::string result;

	if (m_prefix_index > 0 && m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
	{
		result += m_valid_prefixes[m_prefix_index].name;
		result += " ";
	}

	result += m_results[m_selected_index].name;

	if (m_prefix_index > 0 && m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
	{
		int real_val = m_prefix_value * m_valid_prefixes[m_prefix_index].multiplier;
		result += std::format(" {}", real_val);
	}

	if (m_secondary_index > 0 && m_secondary_index < static_cast<int>(m_valid_secondaries.size()))
	{
		int real_val = m_secondary_value * m_valid_secondaries[m_secondary_index].multiplier;
		result += std::format(" {} {}", m_valid_secondaries[m_secondary_index].name, real_val);
	}

	if (m_enchant_value > 0)
		result += std::format(" +{}", m_enchant_value);

	return result;
}

void DialogBox_ItemCreator::on_enter_pressed()
{
	// Live search handles everything — Enter is a no-op
}

void DialogBox_ItemCreator::receive_search_results(const hb::net::PacketNotifyTesterItemSearchResult* pkt)
{
	m_result_count = std::clamp(static_cast<int>(pkt->count), 0, 50);
	std::memcpy(m_results, pkt->entries, sizeof(m_results));
	m_selected_index = -1;
	m_scroll_offset = 0;
}

// ---------------------------------------------------------------------------
// UI HELPERS
// ---------------------------------------------------------------------------

void DialogBox_ItemCreator::draw_dropdown_field(int x, int y, int w,
	const char* text, bool is_open, bool is_hover, const hb::shared::render::Color& text_color)
{
	// Background box
	m_game->m_Renderer->draw_rect_filled(x, y, w, dropdown_h, dd_style::bg);

	// Border — color depends on state
	auto border = is_open ? dd_style::border_open : (is_hover ? dd_style::border_hover : dd_style::border);
	m_game->m_Renderer->draw_rect_outline(x, y, w, dropdown_h, border);

	// Selected value text (left-aligned with padding)
	put_string(x + 4, y + 2, text, text_color);
}

// The one description of the open dropdown. The overlay draws what this
// returns and the click handler hit-tests the same thing, so the list on
// screen and the list being indexed can never drift apart.
std::vector<std::string> DialogBox_ItemCreator::open_dropdown_options(short sX, short sY,
	int& out_x, int& out_y, int& out_w, int& out_selected) const
{
	const int lx = sX + layout::col_left_x1;
	const int rx = sX + layout::col_right_x1;

	std::vector<std::string> options;
	out_x = out_y = out_w = 0;
	out_selected = -1;

	auto anchor = [&](int x, int y, int w, int selected)
	{
		out_x = x; out_y = sY + y; out_w = w; out_selected = selected;
	};

	switch (m_open_dropdown)
	{
	case dropdown_id::prefix_type:
		anchor(lx, layout::row1_sel_y, layout::col_left_w, m_prefix_index);
		for (const auto& p : m_valid_prefixes) options.push_back(p.name);
		break;
	case dropdown_id::effect_type:
		anchor(rx, layout::row1_sel_y, layout::col_right_w, m_secondary_index);
		for (const auto& s : m_valid_secondaries) options.push_back(s.name);
		break;
	case dropdown_id::prefix_value:
		if (m_prefix_index > 0 && m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
		{
			anchor(lx, layout::row2_sel_y, layout::col_left_w, m_prefix_value - 1);
			const int mult = m_valid_prefixes[m_prefix_index].multiplier;
			for (int i = 1; i <= max_value; i++) options.push_back(std::to_string(i * mult));
		}
		break;
	case dropdown_id::effect_value:
		if (m_secondary_index > 0 && m_secondary_index < static_cast<int>(m_valid_secondaries.size()))
		{
			anchor(rx, layout::row2_sel_y, layout::col_right_w, m_secondary_value - 1);
			const int mult = m_valid_secondaries[m_secondary_index].multiplier;
			for (int i = 1; i <= max_value; i++) options.push_back(std::to_string(i * mult));
		}
		break;

	// Upgrade and Count sit on row 3 in legacy mode and on the tiered page's
	// top row; the mode picks the anchor, the contents are the same.
	case dropdown_id::upgrade:
		if (m_game->is_tiered_mode())
			anchor(sX + layout::t_col2_x, layout::t_top_sel_y, layout::t_col_w, m_enchant_value);
		else
			anchor(lx, layout::row3_sel_y, layout::col_left_w, m_enchant_value);
		for (int i = 0; i <= 15; i++) options.push_back(std::format("+{}", i));
		break;
	case dropdown_id::count:
		if (m_game->is_tiered_mode())
			anchor(sX + layout::t_col3_x, layout::t_top_sel_y, layout::t_col_w, m_item_count - 1);
		else
			anchor(rx, layout::row3_sel_y, layout::col_right_w, m_item_count - 1);
		for (int i = 1; i <= 10; i++) options.push_back(std::to_string(i));
		break;

	case dropdown_id::tier:
	{
		anchor(sX + layout::t_col1_x, layout::t_top_sel_y, layout::t_col_w, m_tier);
		options.push_back("None");
		for (uint8_t t = 1; t <= hb::shared::item::tier_count; t++)
		{
			const auto& row = m_game->m_tier_presentation[t - 1];
			options.push_back(row.name.empty() ? std::format("Tier {}", (int)t) : row.name);
		}
		break;
	}
	case dropdown_id::mod_type:
	{
		const auto tier_opts = tier_options_for_slot(m_open_slot);
		int selected = 0;
		for (size_t i = 0; i < tier_opts.size(); i++)
		{
			options.push_back(tier_opts[i].label);
			if (tier_opts[i].modifier_id == m_tier_slots[m_open_slot].modifier_id)
				selected = static_cast<int>(i);
		}
		anchor(sX + layout::t_type_x, layout::t_slot_y + m_open_slot * layout::t_slot_pitch,
			layout::t_type_w, selected);
		break;
	}
	case dropdown_id::mod_value:
	case dropdown_id::mod_value2:
	{
		const uint8_t id = m_tier_slots[m_open_slot].modifier_id;
		if (id == 0) break;
		const auto& row = m_game->m_modifier_catalog[id];
		const bool pair = row.is_pair();
		const bool second = (m_open_dropdown == dropdown_id::mod_value2);
		const int x = second ? layout::t_pair2_x : layout::t_value_x;
		const int w = pair ? layout::t_pair_w : layout::t_value_w;
		// Options start at the band's low end, not at raw 1, so the selected
		// row is the offset into the legal window rather than the raw value.
		const int low = legal_raw_range(row).min;
		anchor(sX + x, layout::t_slot_y + m_open_slot * layout::t_slot_pitch, w,
			(second ? m_tier_slots[m_open_slot].value2 : m_tier_slots[m_open_slot].value) - low);
		options = value_options(row);
		break;
	}

	default:
		break;
	}

	return options;
}

// ---------------------------------------------------------------------------
// SEARCH PAGE
// ---------------------------------------------------------------------------

void DialogBox_ItemCreator::draw_search_page(short sX, short sY, short size_x, short mouse_x, short mouse_y, short z)
{
	// Title
	hb::shared::text::draw_text_aligned(GameFont::Bitmap1,
		sX, sY + 8, size_x, 15,
		"Create Item",
		hb::shared::text::TextStyle::with_integrated_shadow(GameColors::UIWarningRed),
		hb::shared::text::Align::TopCenter);

	// Text input
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
			auto pkt = hb::net::make_common_command_str(CommonType::TesterItemSearch, player().m_player_x, player().m_player_y);
			std::snprintf(pkt.text, sizeof(pkt.text), "%s", m_search_text.empty() ? "" : m_search_text.c_str());
			send_game_packet(pkt);
		}
	}

	// Mouse wheel
	if (m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::ItemCreator && z != 0)
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

		auto color = hover ? GameColors::UIWhite : GameColors::UIMagicBlue;
		hb::shared::text::draw_text_aligned(GameFont::Default,
			sX + layout::content_x1 + 6, ry, layout::content_w - 12, 15,
			entry.name,
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

	// Close button (sprite)
	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) &&
		(mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 1);
	else
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 0);
}

// ---------------------------------------------------------------------------
// CONFIGURE PAGE — TIERED BODY
// ---------------------------------------------------------------------------

void DialogBox_ItemCreator::draw_tiered_body(short sX, short sY, short size_x, short mouse_x, short mouse_y)
{
	auto field = [&](dropdown_id id, int slot, int x, int y, int w, const std::string& text,
		const hb::shared::render::Color& color = GameColors::UIPaleYellow)
	{
		const bool open = (m_open_dropdown == id && m_open_slot == slot);
		const bool hover = !open && mouse_x >= x && mouse_x <= x + w
			&& mouse_y >= y && mouse_y < y + dropdown_h;
		draw_dropdown_field(x, y, w, text.c_str(), open, hover, color);
	};

	// --- TOP ROW: Tier / Upgrade / Count ---
	const int c1 = sX + layout::t_col1_x;
	const int c2 = sX + layout::t_col2_x;
	const int c3 = sX + layout::t_col3_x;

	put_string(c1 + 2, sY + layout::t_top_label_y, "Tier", GameColors::UIWhite);
	put_string(c2 + 2, sY + layout::t_top_label_y, "Upgrade", GameColors::UIWhite);
	put_string(c3 + 2, sY + layout::t_top_label_y, "Count", GameColors::UIWhite);

	const tier_presentation_entry* tier_row = item_name_formatter::get().tier_row(static_cast<uint8_t>(m_tier));
	const std::string tier_text = (m_tier == 0) ? "None"
		: (tier_row != nullptr && !tier_row->name.empty() ? tier_row->name : std::format("Tier {}", m_tier));
	field(dropdown_id::tier, 0, c1, sY + layout::t_top_sel_y, layout::t_col_w, tier_text,
		tier_row != nullptr ? tier_row->color : GameColors::UIPaleYellow);
	field(dropdown_id::upgrade, 0, c2, sY + layout::t_top_sel_y, layout::t_col_w,
		std::format("+{}", m_enchant_value));
	field(dropdown_id::count, 0, c3, sY + layout::t_top_sel_y, layout::t_col_w,
		std::to_string(m_item_count));

	// --- MODIFIER SLOTS: exactly `tier` of them (spec §3, count == tier) ---
	if (m_tier == 0)
	{
		put_aligned_string(sX + layout::col_left_x1, sX + layout::col_right_x2,
			sY + layout::t_slot_label_y + 10, "Plain item - no tier, no modifiers.", GameColors::UIWhite);
	}
	else
	{
		put_string(sX + layout::t_type_x + 2, sY + layout::t_slot_label_y, "Modifiers", GameColors::UIWhite);
		put_string(sX + layout::t_value_x + 2, sY + layout::t_slot_label_y, "Value", GameColors::UIWhite);

		for (int i = 0; i < m_tier; i++)
		{
			const int y = sY + layout::t_slot_y + i * layout::t_slot_pitch;
			const auto& slot = m_tier_slots[i];
			const std::string type_text = (slot.modifier_id == 0) ? "None"
				: catalog_label(m_game->m_modifier_catalog[slot.modifier_id], slot.modifier_id);
			field(dropdown_id::mod_type, i, sX + layout::t_type_x, y, layout::t_type_w, type_text);

			if (slot.modifier_id == 0) continue;

			// A value-less modifier (multiplier 0) has nothing to pick; a pair
			// splits the column into its two independent rolls.
			const auto& row = m_game->m_modifier_catalog[slot.modifier_id];
			if (row.multiplier == 0) continue;

			const bool pair = row.is_pair();
			field(dropdown_id::mod_value, i, sX + layout::t_value_x, y,
				pair ? layout::t_pair_w : layout::t_value_w,
				std::to_string(slot.value * row.multiplier));
			if (pair)
				field(dropdown_id::mod_value2, i, sX + layout::t_pair2_x, y, layout::t_pair_w,
					std::to_string(slot.value2 * row.multiplier));
		}
	}

	// --- PREVIEW: the requested instance, formatted like any other item ---
	const auto preview = build_preview_string();
	if (!preview.empty())
	{
		hb::shared::text::draw_text_aligned(GameFont::Default,
			sX, sY + layout::t_preview_y, size_x, 15,
			preview.c_str(),
			hb::shared::text::TextStyle::from_color(tier_row != nullptr ? tier_row->color : GameColors::UIPaleYellow),
			hb::shared::text::Align::TopCenter);
	}

	// --- SERVER REPLY: the Bands and the tier scope are the server's word,
	// so its rejection is the only place a GM learns them.
	if (!m_server_notice.empty())
	{
		const auto color = m_server_notice_ok ? GameColors::ChatEventGreen : GameColors::UIWarningRed;
		for (size_t pos = 0, line = 0; pos < m_server_notice.size() && line < 2; line++)
		{
			hb::shared::text::draw_text_aligned(GameFont::Default,
				sX, sY + layout::t_notice_y + static_cast<int>(line) * layout::t_notice_pitch, size_x, 15,
				m_server_notice.substr(pos, layout::t_notice_chars).c_str(),
				hb::shared::text::TextStyle::from_color(color),
				hb::shared::text::Align::TopCenter);
			pos += layout::t_notice_chars;
		}
	}
}

bool DialogBox_ItemCreator::on_click_tiered_body(short sX, short sY)
{
	const short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	const short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());

	auto try_open = [&](dropdown_id id, int slot, int x, int y, int w) -> bool
	{
		if (mouse_x < x || mouse_x > x + w || mouse_y < y || mouse_y >= y + dropdown_h)
			return false;
		m_open_dropdown = id;
		m_open_slot = slot;
		m_dropdown_scroll = 0;
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	};

	if (try_open(dropdown_id::tier, 0, sX + layout::t_col1_x, sY + layout::t_top_sel_y, layout::t_col_w))
		return true;
	if (try_open(dropdown_id::upgrade, 0, sX + layout::t_col2_x, sY + layout::t_top_sel_y, layout::t_col_w))
		return true;
	if (try_open(dropdown_id::count, 0, sX + layout::t_col3_x, sY + layout::t_top_sel_y, layout::t_col_w))
		return true;

	for (int i = 0; i < m_tier; i++)
	{
		const int y = sY + layout::t_slot_y + i * layout::t_slot_pitch;
		if (try_open(dropdown_id::mod_type, i, sX + layout::t_type_x, y, layout::t_type_w))
			return true;

		const uint8_t id = m_tier_slots[i].modifier_id;
		if (id == 0) continue;
		const auto& row = m_game->m_modifier_catalog[id];
		if (row.multiplier == 0) continue;

		const bool pair = row.is_pair();
		if (try_open(dropdown_id::mod_value, i, sX + layout::t_value_x, y,
			pair ? layout::t_pair_w : layout::t_value_w))
			return true;
		if (pair && try_open(dropdown_id::mod_value2, i, sX + layout::t_pair2_x, y, layout::t_pair_w))
			return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// CONFIGURE PAGE
// ---------------------------------------------------------------------------

void DialogBox_ItemCreator::draw_configure_page(short sX, short sY, short size_x, short mouse_x, short mouse_y, short z)
{
	// Title
	hb::shared::text::draw_text_aligned(GameFont::Bitmap1,
		sX, sY + 8, size_x, 15,
		"Configure Item",
		hb::shared::text::TextStyle::with_integrated_shadow(GameColors::UIWarningRed),
		hb::shared::text::Align::TopCenter);

	// Item name + category (single line)
	if (m_selected_index >= 0 && m_selected_index < m_result_count)
	{
		auto info_str = std::format("{} ({})", m_results[m_selected_index].name, category_name(m_category));
		hb::shared::text::draw_text_aligned(GameFont::Default,
			sX, sY + layout::item_info_y, size_x, 15,
			info_str.c_str(),
			hb::shared::text::TextStyle::from_color(GameColors::UIPaleYellow),
			hb::shared::text::Align::TopCenter);
	}

	// Absolute column positions
	int lx = sX + layout::col_left_x1;
	int rx = sX + layout::col_right_x1;

	// Tiered mode states an instance (Tier + slots); legacy mode states a
	// prefix and a secondary. The two pages share only the frame, the
	// buttons and the dropdown overlay.
	if (m_game->is_tiered_mode())
	{
		draw_tiered_body(sX, sY, size_x, mouse_x, mouse_y);
	}
	else if (m_category == item_category::none)
	{
		put_aligned_string(sX + layout::col_left_x1, sX + layout::col_right_x2, sY + layout::row1_label_y + 10, "No attributes for this type.", GameColors::UIWhite);
		put_aligned_string(sX + layout::col_left_x1, sX + layout::col_right_x2, sY + layout::row1_label_y + 30, "Item will be created plain.", GameColors::UIWhite);

		// Count dropdown (still available for plain items)
		put_string(rx + 4, sY + layout::row3_label_y, "Count:", GameColors::UIWhite);
		auto count_str = std::to_string(m_item_count);
		bool cnt_open = (m_open_dropdown == dropdown_id::count);
		bool cnt_hover = !cnt_open && (mouse_x >= rx && mouse_x <= rx + layout::col_right_w
			&& mouse_y >= sY + layout::row3_sel_y && mouse_y < sY + layout::row3_sel_y + dropdown_h);
		draw_dropdown_field(rx, sY + layout::row3_sel_y, layout::col_right_w, count_str.c_str(), cnt_open, cnt_hover);
	}
	else
	{
		// --- ROW 1: Prefix type (left) / Effect type (right) ---
		put_string(lx + 4, sY + layout::row1_label_y, "First Stat", GameColors::UIWhite);
		put_string(rx + 4, sY + layout::row1_label_y, "Second Stat", GameColors::UIWhite);

		const char* prefix_name = (m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
			? m_valid_prefixes[m_prefix_index].name : "None";
		bool pn_open = (m_open_dropdown == dropdown_id::prefix_type);
		bool pn_hover = !pn_open && (mouse_x >= lx && mouse_x <= lx + layout::col_left_w
			&& mouse_y >= sY + layout::row1_sel_y && mouse_y < sY + layout::row1_sel_y + dropdown_h);
		draw_dropdown_field(lx, sY + layout::row1_sel_y, layout::col_left_w, prefix_name, pn_open, pn_hover);

		const char* sec_name = (m_secondary_index < static_cast<int>(m_valid_secondaries.size()))
			? m_valid_secondaries[m_secondary_index].name : "None";
		bool sn_open = (m_open_dropdown == dropdown_id::effect_type);
		bool sn_hover = !sn_open && (mouse_x >= rx && mouse_x <= rx + layout::col_right_w
			&& mouse_y >= sY + layout::row1_sel_y && mouse_y < sY + layout::row1_sel_y + dropdown_h);
		draw_dropdown_field(rx, sY + layout::row1_sel_y, layout::col_right_w, sec_name, sn_open, sn_hover);

		// --- ROW 2: Prefix value (left) / Effect value (right) ---
		if (m_prefix_index > 0 && m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
		{
			put_string(lx + 4, sY + layout::row2_label_y, "Value:", GameColors::UIWhite);
			int real_val = m_prefix_value * m_valid_prefixes[m_prefix_index].multiplier;
			auto val_str = std::to_string(real_val);
			bool pv_open = (m_open_dropdown == dropdown_id::prefix_value);
			bool pv_hover = !pv_open && (mouse_x >= lx && mouse_x <= lx + layout::col_left_w
				&& mouse_y >= sY + layout::row2_sel_y && mouse_y < sY + layout::row2_sel_y + dropdown_h);
			draw_dropdown_field(lx, sY + layout::row2_sel_y, layout::col_left_w, val_str.c_str(), pv_open, pv_hover);
		}

		if (m_secondary_index > 0 && m_secondary_index < static_cast<int>(m_valid_secondaries.size()))
		{
			put_string(rx + 4, sY + layout::row2_label_y, "Value:", GameColors::UIWhite);
			int real_val = m_secondary_value * m_valid_secondaries[m_secondary_index].multiplier;
			auto val_str = std::to_string(real_val);
			bool sv_open = (m_open_dropdown == dropdown_id::effect_value);
			bool sv_hover = !sv_open && (mouse_x >= rx && mouse_x <= rx + layout::col_right_w
				&& mouse_y >= sY + layout::row2_sel_y && mouse_y < sY + layout::row2_sel_y + dropdown_h);
			draw_dropdown_field(rx, sY + layout::row2_sel_y, layout::col_right_w, val_str.c_str(), sv_open, sv_hover);
		}

		// --- ROW 3: Upgrade (left) / Count (right) ---
		put_string(lx + 4, sY + layout::row3_label_y, "Upgrade:", GameColors::UIWhite);
		auto enchant_str = std::format("+{}", m_enchant_value);
		bool enc_open = (m_open_dropdown == dropdown_id::upgrade);
		bool enc_hover = !enc_open && (mouse_x >= lx && mouse_x <= lx + layout::col_left_w
			&& mouse_y >= sY + layout::row3_sel_y && mouse_y < sY + layout::row3_sel_y + dropdown_h);
		draw_dropdown_field(lx, sY + layout::row3_sel_y, layout::col_left_w, enchant_str.c_str(), enc_open, enc_hover);

		put_string(rx + 4, sY + layout::row3_label_y, "Count:", GameColors::UIWhite);
		auto count_str = std::to_string(m_item_count);
		bool cnt_open = (m_open_dropdown == dropdown_id::count);
		bool cnt_hover = !cnt_open && (mouse_x >= rx && mouse_x <= rx + layout::col_right_w
			&& mouse_y >= sY + layout::row3_sel_y && mouse_y < sY + layout::row3_sel_y + dropdown_h);
		draw_dropdown_field(rx, sY + layout::row3_sel_y, layout::col_right_w, count_str.c_str(), cnt_open, cnt_hover);

		// --- PREVIEW ---
		auto preview = build_preview_string();
		if (!preview.empty())
		{
			hb::shared::text::draw_text_aligned(GameFont::Default,
				sX, sY + layout::preview_label_y, size_x, 15,
				"Preview:",
				hb::shared::text::TextStyle::from_color(GameColors::UIWhite),
				hb::shared::text::Align::TopCenter);
			hb::shared::text::draw_text_aligned(GameFont::Default,
				sX, sY + layout::preview_text_y, size_x, 15,
				preview.c_str(),
				hb::shared::text::TextStyle::from_color(GameColors::UIPaleYellow),
				hb::shared::text::Align::TopCenter);
		}
	}

	// --- BUTTONS ---
	int left_btn_x = sX + layout::col_left_x1;
	int right_btn_x = sX + layout::col_right_x2 - layout::btn_w;

	auto create_label = (m_item_count > 1) ? std::format("[Create x{}]", m_item_count) : std::string("[Create]");
	bool create_hover = (mouse_x >= left_btn_x && mouse_x <= left_btn_x + layout::btn_w
		&& mouse_y >= sY + layout::btn_y && mouse_y <= sY + layout::btn_y + 18);
	hb::shared::text::draw_text_aligned(GameFont::Default,
		left_btn_x, sY + layout::btn_y, layout::btn_w, 15,
		create_label.c_str(),
		hb::shared::text::TextStyle::from_color(create_hover ? GameColors::UIWhite : GameColors::UIMagicBlue),
		hb::shared::text::Align::TopCenter);

	bool back_hover = (mouse_x >= right_btn_x && mouse_x <= right_btn_x + layout::btn_w
		&& mouse_y >= sY + layout::btn_y && mouse_y <= sY + layout::btn_y + 18);
	hb::shared::text::draw_text_aligned(GameFont::Default,
		right_btn_x, sY + layout::btn_y, layout::btn_w, 15,
		"[<< Back]",
		hb::shared::text::TextStyle::from_color(back_hover ? GameColors::UIWhite : GameColors::UIMagicBlue),
		hb::shared::text::Align::TopCenter);

	// Close button (sprite)
	if ((mouse_x >= sX + ui_layout::right_btn_x) && (mouse_x <= sX + ui_layout::right_btn_x + ui_layout::btn_size_x) &&
		(mouse_y >= sY + ui_layout::btn_y) && (mouse_y <= sY + ui_layout::btn_y + ui_layout::btn_size_y))
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 1);
	else
		draw_new_dialog_box(InterfaceNdButton, sX + ui_layout::right_btn_x, sY + ui_layout::btn_y, 0);

	// --- DROPDOWN OVERLAY (drawn last, on top of everything) ---
	if (m_open_dropdown != dropdown_id::none)
	{
		int dd_x = 0, dd_y = 0, dd_w = 0, dd_selected = -1;
		const std::vector<std::string> options = open_dropdown_options(sX, sY, dd_x, dd_y, dd_w, dd_selected);
		const int dd_count = static_cast<int>(options.size());

		// Mouse wheel scrolls the open dropdown list
		if (m_game->get_dialog_box_manager().get_top_id() == DialogBoxId::ItemCreator && z != 0)
		{
			if (dd_count > dropdown_max_vis)
			{
				m_dropdown_scroll -= z / 60;
				int max_scroll = dd_count - dropdown_max_vis;
				m_dropdown_scroll = std::clamp(m_dropdown_scroll, 0, max_scroll);
			}
		}

		if (dd_count > 0)
		{
			int list_y = dd_y + dropdown_h;
			int vis = std::min(dd_count, static_cast<int>(dropdown_max_vis));
			int list_h = vis * dropdown_h;

			// Clamp scroll
			int max_scroll = std::max(0, dd_count - dropdown_max_vis);
			m_dropdown_scroll = std::clamp(m_dropdown_scroll, 0, max_scroll);

			// List background + border
			m_game->m_Renderer->draw_rect_filled(dd_x, list_y, dd_w, list_h, dd_style::list_bg);
			m_game->m_Renderer->draw_rect_outline(dd_x, list_y, dd_w, list_h, dd_style::list_border);

			// Draw each visible option
			for (int i = 0; i < vis; i++)
			{
				int idx = i + m_dropdown_scroll;
				if (idx >= dd_count) break;

				int iy = list_y + i * dropdown_h;
				bool item_hover = (mouse_x >= dd_x && mouse_x <= dd_x + dd_w
					&& mouse_y >= iy && mouse_y < iy + dropdown_h);

				if (item_hover)
					m_game->m_Renderer->draw_rect_filled(dd_x + 1, iy, dd_w - 2, dropdown_h, dd_style::item_hover);

				auto color = (idx == dd_selected) ? GameColors::UIPaleYellow
					: (item_hover ? GameColors::UIWhite : GameColors::UINearWhite);
				put_string(dd_x + 4, iy + 1, options[idx].c_str(), color);
			}

			// Scrollbar indicator if list is scrollable
			if (dd_count > dropdown_max_vis)
			{
				int bar_h = std::max(8, list_h * vis / dd_count);
				int bar_y = list_y + (list_h - bar_h) * m_dropdown_scroll / max_scroll;
				m_game->m_Renderer->draw_rect_filled(dd_x + dd_w - 4, bar_y, 3, bar_h, dd_style::scrollbar);
			}
		}
	}
}

void DialogBox_ItemCreator::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short z = static_cast<short>(hb::shared::input::get_mouse_wheel_delta());
	short sX = m_x;
	short sY = m_y;
	short size_x = m_size_x;

	draw_new_dialog_box(InterfaceNdGame2, sX, sY, 0);

	if (m_page == 0)
		draw_search_page(sX, sY, size_x, mouse_x, mouse_y, z);
	else
		draw_configure_page(sX, sY, size_x, mouse_x, mouse_y, z);
}

// ---------------------------------------------------------------------------
// CLICK HANDLERS
// ---------------------------------------------------------------------------

bool DialogBox_ItemCreator::on_click_search(short sX, short sY, short size_x)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	// Results list — clicking goes directly to configure
	for (int i = 0; i < layout::list_rows && (i + m_scroll_offset) < m_result_count; i++)
	{
		int idx = i + m_scroll_offset;
		int ry = sY + layout::list_y + i * layout::row_h;
		if (mouse_x >= sX + layout::content_x1 && mouse_x <= sX + layout::content_x2
			&& mouse_y >= ry && mouse_y <= ry + layout::row_h - 2)
		{
			m_selected_index = idx;
			text_input_manager::get().end_input();
			build_valid_options(m_results[idx].effect_type);
			m_prefix_index = 0;
			m_prefix_value = 1;
			m_secondary_index = 0;
			m_secondary_value = 1;
			m_enchant_value = 0;
			m_item_count = 1;
			m_open_dropdown = dropdown_id::none;
			reset_tier_state();
			m_page = 1;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
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

bool DialogBox_ItemCreator::on_click_configure(short sX, short sY, short size_x)
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	int lx = sX + layout::col_left_x1;
	int rx = sX + layout::col_right_x1;

	// --- STEP 1: Handle clicks when a dropdown list is open ---
	if (m_open_dropdown != dropdown_id::none)
	{
		// The same description the overlay drew — anchor, options, selection.
		int dd_x = 0, dd_y = 0, dd_w = 0, dd_selected = -1;
		const std::vector<std::string> dd_options =
			open_dropdown_options(sX, sY, dd_x, dd_y, dd_w, dd_selected);
		const int dd_count = static_cast<int>(dd_options.size());

		int list_y = dd_y + dropdown_h;
		int vis = std::min(dd_count, static_cast<int>(dropdown_max_vis));
		int list_h = vis * dropdown_h;

		// Click on the dropdown field itself → toggle closed
		if (mouse_x >= dd_x && mouse_x <= dd_x + dd_w
			&& mouse_y >= dd_y && mouse_y < dd_y + dropdown_h)
		{
			m_open_dropdown = dropdown_id::none;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}

		// Click inside the expanded list → select item
		if (mouse_x >= dd_x && mouse_x <= dd_x + dd_w
			&& mouse_y >= list_y && mouse_y < list_y + list_h)
		{
			int clicked_idx = (mouse_y - list_y) / dropdown_h + m_dropdown_scroll;
			if (clicked_idx >= 0 && clicked_idx < dd_count)
			{
				switch (m_open_dropdown)
				{
				case dropdown_id::prefix_type:
					m_prefix_index = clicked_idx;
					m_prefix_value = 1;
					break;
				case dropdown_id::effect_type:
					m_secondary_index = clicked_idx;
					m_secondary_value = 1;
					break;
				case dropdown_id::prefix_value:
					m_prefix_value = clicked_idx + 1;
					break;
				case dropdown_id::effect_value:
					m_secondary_value = clicked_idx + 1;
					break;
				case dropdown_id::upgrade:
					m_enchant_value = clicked_idx;
					break;
				case dropdown_id::count:
					m_item_count = clicked_idx + 1;
					break;

				// Tier index 0 is "None" (a plain mint), 1-4 are the tiers.
				case dropdown_id::tier:
					apply_tier_change(clicked_idx);
					break;
				case dropdown_id::mod_type:
				{
					const auto tier_opts = tier_options_for_slot(m_open_slot);
					if (clicked_idx < static_cast<int>(tier_opts.size()))
					{
						const uint8_t picked = tier_opts[clicked_idx].modifier_id;
						m_tier_slots[m_open_slot] = {};
						m_tier_slots[m_open_slot].modifier_id = picked;
						// Seed both halves at the band's low end. A fresh slot
						// used to hold value 0, which is out of band for every
						// modifier that has one — the GM had to open the value
						// picker before the mint could possibly be accepted.
						const int low = legal_raw_range(m_game->m_modifier_catalog[picked]).min;
						m_tier_slots[m_open_slot].value = static_cast<uint8_t>(low);
						m_tier_slots[m_open_slot].value2 = static_cast<uint8_t>(low);
					}
					break;
				}
				case dropdown_id::mod_value:
				case dropdown_id::mod_value2:
				{
					const uint8_t id = m_tier_slots[m_open_slot].modifier_id;
					if (id == 0) break;
					const int picked = clicked_idx + legal_raw_range(m_game->m_modifier_catalog[id]).min;
					if (m_open_dropdown == dropdown_id::mod_value2)
						m_tier_slots[m_open_slot].value2 = static_cast<uint8_t>(picked);
					else
						m_tier_slots[m_open_slot].value = static_cast<uint8_t>(picked);
					break;
				}

				default: break;
				}
			}
			m_open_dropdown = dropdown_id::none;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}

		// Click outside → close dropdown and fall through to normal handling
		m_open_dropdown = dropdown_id::none;
	}

	// --- STEP 2: Check clicks on dropdown fields (to open them) ---
	auto try_open_dropdown = [&](dropdown_id id, int x, int y, int w) -> bool
	{
		if (mouse_x >= x && mouse_x <= x + w
			&& mouse_y >= y && mouse_y < y + dropdown_h)
		{
			m_open_dropdown = id;
			m_open_slot = 0;
			m_dropdown_scroll = 0;
			audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
			return true;
		}
		return false;
	};

	if (m_game->is_tiered_mode())
	{
		if (on_click_tiered_body(sX, sY))
			return true;
	}
	else if (m_category == item_category::none)
	{
		// Count dropdown
		if (try_open_dropdown(dropdown_id::count, rx, sY + layout::row3_sel_y, layout::col_right_w))
			return true;
	}
	else
	{
		// Row 1: Prefix type / Effect type
		if (try_open_dropdown(dropdown_id::prefix_type, lx, sY + layout::row1_sel_y, layout::col_left_w))
			return true;
		if (try_open_dropdown(dropdown_id::effect_type, rx, sY + layout::row1_sel_y, layout::col_right_w))
			return true;

		// Row 2: Prefix value / Effect value (only if type is selected)
		if (m_prefix_index > 0)
		{
			if (try_open_dropdown(dropdown_id::prefix_value, lx, sY + layout::row2_sel_y, layout::col_left_w))
				return true;
		}
		if (m_secondary_index > 0)
		{
			if (try_open_dropdown(dropdown_id::effect_value, rx, sY + layout::row2_sel_y, layout::col_right_w))
				return true;
		}

		// Row 3: Upgrade / Count
		if (try_open_dropdown(dropdown_id::upgrade, lx, sY + layout::row3_sel_y, layout::col_left_w))
			return true;
		if (try_open_dropdown(dropdown_id::count, rx, sY + layout::row3_sel_y, layout::col_right_w))
			return true;
	}

	// --- STEP 3: Create / Back / Close buttons ---
	int left_btn_x = sX + layout::col_left_x1;
	if (mouse_x >= left_btn_x && mouse_x <= left_btn_x + layout::btn_w
		&& mouse_y >= sY + layout::btn_y && mouse_y <= sY + layout::btn_y + 18)
	{
		if (m_game->is_tiered_mode())
		{
			// count == tier is the one rule the pickers own outright, so an
			// empty slot is answered here instead of by a round trip.
			const auto missing = missing_tier_input();
			if (missing.empty())
			{
				send_tiered_mint();
			}
			else
			{
				m_server_notice = missing;
				m_server_notice_ok = false;
			}
		}
		else if (m_selected_index >= 0 && m_selected_index < m_result_count)
		{
			int item_id = m_results[m_selected_index].item_id;

			int prefix_type = (m_prefix_index < static_cast<int>(m_valid_prefixes.size()))
				? m_valid_prefixes[m_prefix_index].type : 0;
			int secondary_type = (m_secondary_index < static_cast<int>(m_valid_secondaries.size()))
				? m_valid_secondaries[m_secondary_index].type : 0;
			int pval = (prefix_type != 0) ? m_prefix_value : 0;
			int sval = (secondary_type != 0) ? m_secondary_value : 0;

			// Pack attributes into legacy uint32_t format for TesterCreateItem command
			uint32_t attr = 0;
			attr |= (static_cast<uint32_t>(sval) & 0x0F) << 8;
			attr |= (static_cast<uint32_t>(secondary_type) & 0x0F) << 12;
			attr |= (static_cast<uint32_t>(pval) & 0x0F) << 16;
			attr |= (static_cast<uint32_t>(prefix_type) & 0x0F) << 20;
			attr |= (static_cast<uint32_t>(m_enchant_value) & 0x0F) << 28;
			{
				auto pkt = hb::net::make_common_command(CommonType::TesterCreateItem, player().m_player_x, player().m_player_y);
				pkt.v1 = item_id;
				pkt.v2 = static_cast<int>(attr);
				pkt.v3 = m_item_count;
				send_game_packet(pkt);
			}
		}
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	// Back button
	int right_btn_x = sX + layout::col_right_x2 - layout::btn_w;
	if (mouse_x >= right_btn_x && mouse_x <= right_btn_x + layout::btn_w
		&& mouse_y >= sY + layout::btn_y && mouse_y <= sY + layout::btn_y + 18)
	{
		m_page = 0;
		m_open_dropdown = dropdown_id::none;
		text_input_manager::get().start_input(sX + 70, sY + layout::search_bar_y + 5, 20, m_search_text);
		m_last_sx = sX;
		m_last_sy = sY;
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
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

bool DialogBox_ItemCreator::on_click()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	short sX = m_x;
	short sY = m_y;
	short size_x = m_size_x;

	if (m_page == 0)
		return on_click_search(sX, sY, size_x);
	else
		return on_click_configure(sX, sY, size_x);
}

bool DialogBox_ItemCreator::on_enable(int type, int64_t v1, int v2, const char* string)
{
	if (is_enabled()) return true;
	int ic_x = LOGICAL_WIDTH() - 258 * 2 - 20;
	int ic_y = LOGICAL_HEIGHT() - 339 - ICON_PANEL_HEIGHT() - 10;
	m_x = ic_x;
	m_y = ic_y;
	return true;
}
#endif // TESTER_ONLY
