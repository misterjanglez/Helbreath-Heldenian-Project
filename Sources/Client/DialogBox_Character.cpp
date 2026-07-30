#include "DialogBox_Character.h"
#include "ConfigManager.h"
#include "CursorTarget.h"
#include "Game.h"
#include "GameFonts.h"
#include "PacketSendHelpers.h"
#include "TextLibExt.h"
#include "UITheme.h"

#include "InventoryManager.h"
#include "ItemNameFormatter.h"
#include "ItemSpriteMetadata.h"
#include "lan_eng.h"
#include "SharedCalculations.h"
#include "BalanceConstants.h"
#include <format>
#include <string>
#include "IInput.h"
#include "AudioManager.h"

using namespace hb::shared::net;
using namespace hb::shared::item;

using hb::shared::item::EquipPos;
using namespace hb::client::sprite_id;

// Caption for one value row, right-aligned to end just left of x_right.
//
// Drawn with the same box height and top alignment put_aligned_string uses for
// the values themselves, so a caption and its value share a baseline exactly
// rather than approximately. The captions used to be painted into the panel
// art; the flat panel has no lettering, so they are drawn here.
static void put_row_caption(int x_right, int y, const char* caption)
{
	constexpr int caption_box = 90;
	hb::shared::text::draw_text_aligned(GameFont::Default, x_right - caption_box, y, caption_box, 15,
		caption, hb::shared::text::TextStyle::from_color(hb::client::ui_theme::palette::label),
		hb::shared::text::Align::TopRight);
}

// Margin (in pixels) added around item sprites to make small items easier to click.
// Checks if any opaque pixel exists within this distance of the cursor.
constexpr int item_hit_margin = 8;

static bool check_item_collision(auto&& sprite, int sprite_x, int sprite_y,
	int frame, int point_x, int point_y, int margin)
{
	if (sprite->CheckCollision(sprite_x, sprite_y, frame, point_x, point_y))
		return true;

	if (margin <= 0)
		return false;

	for (int dy = -margin; dy <= margin; dy++)
	{
		for (int dx = -margin; dx <= margin; dx++)
		{
			if (dx == 0 && dy == 0) continue;
			if (dx * dx + dy * dy > margin * margin) continue;
			if (sprite->CheckCollision(sprite_x, sprite_y, frame, point_x + dx, point_y + dy))
				return true;
		}
	}
	return false;
}

// draw order: first entry drawn first (bottom layer), last entry drawn last (top layer).
// Collision checks iterate in reverse so topmost-drawn item has highest click priority.
static constexpr EquipSlotLayout MaleEquipSlots[] = {
	{ EquipPos::Back,        41,  137 },
	{ EquipPos::Leggings,      171,  290 },
	{ EquipPos::Arms,       171,  290 },
	{ EquipPos::Boots,   171,  290 },
	{ EquipPos::Body,       171,  290 },
	{ EquipPos::FullBody,   171,  290 },
	{ EquipPos::LeftHand,    90,  170 },
	{ EquipPos::RightHand,   57,  186 },
	{ EquipPos::TwoHand,     57,  186 },
	{ EquipPos::Neck,        35,  120 },
	{ EquipPos::RightFinger, 32,  193 },
	{ EquipPos::LeftFinger,  92,  174 },
	{ EquipPos::Head,        72,  135 },
};

static constexpr EquipSlotLayout FemaleEquipSlots[] = {
	{ EquipPos::Back,        45,  143 },
	{ EquipPos::Leggings,      171,  290 },
	{ EquipPos::Arms,       171,  290 },
	{ EquipPos::Boots,   171,  290 },
	{ EquipPos::Body,       171,  290 },
	{ EquipPos::FullBody,   171,  290 },
	{ EquipPos::LeftHand,    84,  175 },
	{ EquipPos::RightHand,   60,  191 },
	{ EquipPos::TwoHand,     60,  191 },
	{ EquipPos::Neck,        35,  120 },
	{ EquipPos::RightFinger, 32,  193 },
	{ EquipPos::LeftFinger,  92,  174 },
	{ EquipPos::Head,        72,  139 },
};

DialogBox_Character::DialogBox_Character(CGame* game)
	: IDialogBox(DialogBoxId::CharacterInfo, game)
{
	set_default_rect(30 , 30 , column_w + pane_w, panel_h);
}

// The panel is as wide as the column plus whatever pane is unfolded. m_size_x is
// what drags, right-click-closes and the panel draw all read, so changing it here
// is the whole of "the dialog widens".
void DialogBox_Character::set_pane(pane which)
{
	m_pane = which;
	m_size_x = static_cast<short>(column_w + (which == pane::none ? 0 : pane_w));
}

// Show `which`, or fold the pane away when it is already the one showing.
bool DialogBox_Character::toggle_pane(pane which)
{
	set_pane(m_pane == which ? pane::none : which);
	audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
	return true;
}

// Helper: Display stat with optional angelic bonus (blue if boosted)
void DialogBox_Character::draw_stat(int x1, int x2, int y, int baseStat, int angelicBonus, int gearBonus)
{
	// Angelic keeps folding into the number, exactly as it always has — it is a
	// property of one legacy pendant and the display predates this. Gear
	// attributes render split ("18 (+3)") because the whole point is to show
	// what the equipped set is contributing, which a single total hides.
	const int shown = baseStat + angelicBonus;
	const auto buf = (gearBonus != 0)
		? std::format("{} (+{})", shown, gearBonus)
		: std::format("{}", shown);

	put_aligned_string(x1, x2, y, buf.c_str(),
		((angelicBonus != 0) || (gearBonus != 0)) ? GameColors::UIModifiedStat : GameColors::UILabel);
}

// Find the topmost equipped slot colliding with the mouse, using the given table.
// Returns the EquipPos of the topmost hit, or EquipPos::None if nothing collides.
static EquipPos FindHoverSlot(CGame* game, const EquipSlotLayout* slots, int slotCount,
	short sX, short sY, short mouse_x, short mouse_y, const char* equip_poi_status, int spriteOffset)
{
	bool is_female = (spriteOffset == 40);
	for (int i = slotCount - 1; i >= 0; i--)
	{
		int ep = static_cast<int>(slots[i].equipPos);
		int itemIdx = equip_poi_status[ep];
		if (itemIdx == -1) continue;

		CItem* cfg = game->get_item_config(game->m_player->m_item_list[itemIdx]->m_id_num);
		if (cfg == nullptr) continue;

		auto draw = game->get_item_draw(cfg->m_display_id, item_atlas::equip, is_female);
		if (check_item_collision(draw.sprite,
			sX + slots[i].offsetX, sY + slots[i].offsetY, draw.frame, mouse_x, mouse_y, item_hit_margin))
		{
			return slots[i].equipPos;
		}
	}
	return EquipPos::None;
}

// Helper: render equipped item with optional hover highlight
void DialogBox_Character::draw_equipped_item(hb::shared::item::EquipPos equipPos, int drawX, int drawY,
	const char* equip_poi_status, bool highlight, int spriteOffset)
{
	int itemIdx = equip_poi_status[static_cast<int>(equipPos)];
	if (itemIdx == -1) return;

	CItem* item = player().m_item_list[itemIdx].get();
	CItem* cfg = m_game->get_item_config(item->m_id_num);
	if (cfg == nullptr) return;

	bool disabled = inventory_manager::get().is_locked(itemIdx);

	bool is_female = (spriteOffset == 40);
	auto equip_draw = m_game->get_item_draw(cfg->m_display_id, item_atlas::equip, is_female);

	m_game->draw_item_sprite(equip_draw, drawX, drawY, item->m_instance.item_color, cfg,
		disabled ? item_draw_state::disabled : item_draw_state::normal);

	if (highlight)
		equip_draw.sprite->draw(drawX, drawY, equip_draw.frame, hb::shared::sprite::DrawParams::additive(0.35f));
}

void DialogBox_Character::build_equip_status_array(char (&equip_poi_status)[DEF_MAXITEMEQUIPPOS]) const
{
	std::memset(equip_poi_status, -1, sizeof(equip_poi_status));
	for (int i = 0; i < hb::shared::limits::MaxItems; i++)
	{
		if (player().m_item_list[i] != nullptr && m_game->m_is_item_equipped[i])
		{
			CItem* cfg = m_game->get_item_config(player().m_item_list[i]->m_id_num);
			if (cfg != nullptr)
				equip_poi_status[cfg->m_equip_pos] = i;
		}
	}
}

char DialogBox_Character::find_equip_item_at_point(short mouse_x, short mouse_y, short sX, short sY,
	const char* equip_poi_status) const
{
	const EquipSlotLayout* slots = nullptr;
	int slotCount = 0;
	int spriteOffset = 0;

	if (player().m_player_type >= 1 && player().m_player_type <= 3)
	{
		slots = MaleEquipSlots;
		slotCount = static_cast<int>(std::size(MaleEquipSlots));
	}
	else if (player().m_player_type >= 4 && player().m_player_type <= 6)
	{
		slots = FemaleEquipSlots;
		slotCount = static_cast<int>(std::size(FemaleEquipSlots));
		spriteOffset = 40;
	}

	// Iterate in reverse: topmost drawn item gets highest click priority
	for (int i = slotCount - 1; i >= 0; i--)
	{
		int ep = static_cast<int>(slots[i].equipPos);
		int itemIdx = equip_poi_status[ep];
		if (itemIdx == -1) continue;

		CItem* cfg = m_game->get_item_config(player().m_item_list[itemIdx]->m_id_num);
		if (cfg == nullptr) continue;

		bool is_female = (spriteOffset == 40);
		auto draw = m_game->get_item_draw(cfg->m_display_id, item_atlas::equip, is_female);
		if (check_item_collision(draw.sprite,
			sX + slots[i].offsetX, sY + slots[i].offsetY, draw.frame, mouse_x, mouse_y, item_hit_margin))
		{
			return static_cast<char>(itemIdx);
		}
	}

	return -1;
}

int DialogBox_Character::pane_row(int x, int y, int w, const char* caption,
	const std::string& value, bool notable) const
{
	namespace theme = hb::client::ui_theme;
	return theme::label_value(x, x, w, y, caption, value.c_str(),
		notable ? theme::palette::value_hi : theme::palette::value);
}

void DialogBox_Character::draw_combat_pane(int x, int y, int w,
	const hb::client::combat_summary& summary)
{
	namespace theme = hb::client::ui_theme;
	namespace mod = hb::shared::item::modifier_id;

	int row = y;
	theme::label(x, row, UI_COMBAT_HEAD_ATTACK, theme::palette::tab_active);
	row += theme::metrics::row_pitch;

	if (summary.has_weapon)
	{
		row = pane_row(x, row, w, UI_COMBAT_DAMAGE_SMALL,
			std::format("{}-{}", summary.damage_small.min, summary.damage_small.max), true);
		// Large-target dice only differ on weapons configured for it; a weapon
		// with the same numbers on both sides would just print the row twice.
		if (summary.damage_large.max != summary.damage_small.max
			|| summary.damage_large.min != summary.damage_small.min)
		{
			row = pane_row(x, row, w, UI_COMBAT_DAMAGE_LARGE,
				std::format("{}-{}", summary.damage_large.min, summary.damage_large.max));
		}
	}
	else
	{
		row = pane_row(x, row, w, UI_COMBAT_DAMAGE_SMALL, UI_COMBAT_NO_WEAPON);
	}

	row = pane_row(x, row, w, UI_COMBAT_ATTACK_DELAY,
		std::format("{} ms", summary.attack_delay));

	if (const int hit = summary.rolled(mod::hitting_probability); hit != 0)
		row = pane_row(x, row, w, UI_COMBAT_HIT_BONUS, std::format("+{}%", hit));

	row += theme::metrics::row_pitch / 2;
	theme::separator(x, row, w);
	row += theme::metrics::row_pitch / 2;

	theme::label(x, row, UI_COMBAT_HEAD_DEFENCE, theme::palette::tab_active);
	row += theme::metrics::row_pitch;

	row = pane_row(x, row, w, UI_COMBAT_DEFENCE_RATIO,
		std::format("{}", summary.defense_ratio), true);
	row = pane_row(x, row, w, UI_COMBAT_ARMOUR, std::format("+{}%", summary.armour_defence));

	// These four are the rows the server owns the totals for, so they carry the
	// gear contribution and say as much at the foot of the pane.
	struct { uint8_t id; const char* caption; } const rolled_defence[] = {
		{ mod::physical_absorb, UI_COMBAT_PHYS_ABSORB },
		{ mod::magic_resist,    UI_COMBAT_MAGIC_RESIST },
		{ mod::magic_absorb,    UI_COMBAT_MAGIC_ABSORB },
		{ mod::poison_resist,   UI_COMBAT_POISON_RESIST },
	};
	for (const auto& line : rolled_defence)
		row = pane_row(x, row, w, line.caption, std::format("+{}%", summary.rolled(line.id)));

	// Wrapped, because the pane is 180px of usable width and this sentence is the
	// one thing in it that must not be clipped.
	hb::shared::text::draw_text_wrapped(GameFont::Default, x, y + panel_h - 90, w, 54,
		UI_COMBAT_GEAR_ONLY, hb::shared::text::TextStyle::from_color(theme::palette::dim),
		hb::shared::text::Align::TopLeft);
}

void DialogBox_Character::draw_gear_pane(int x, int y, int w,
	const hb::client::combat_summary& summary)
{
	namespace theme = hb::client::ui_theme;

	int row = y;
	theme::label(x, row, UI_COMBAT_HEAD_GEAR, theme::palette::tab_active);
	row += theme::metrics::row_pitch;

	if (!summary.has_any_rolled())
	{
		hb::shared::text::draw_text_wrapped(GameFont::Default, x, row, w, 40,
			UI_COMBAT_NO_GEAR, hb::shared::text::TextStyle::from_color(theme::palette::dim),
			hb::shared::text::Align::TopLeft);
		return;
	}

	// Every rolled line the set carries, labelled from the replicated catalog so
	// the wording matches the item's own tooltip. Walking the catalog in id order
	// groups the buckets the way the id blocks are laid out.
	//
	// A fully Legendary set can roll more lines than the pane is tall, so the
	// overflow is counted and stated rather than dropped — a list that silently
	// stops reads as "that is all of it".
	const int bottom = y + panel_h - 80;
	int dropped = 0;
	for (uint8_t id = 1; id < summary.gear.size(); id++)
	{
		const int total = summary.gear[id];
		if (total == 0) continue;

		const auto& entry = m_game->m_modifier_catalog[id];
		if (!entry.present) continue;

		if (row > bottom) { dropped++; continue; }

		// The prefix word when the row has one, else its tooltip label. A label
		// can be a whole-line format with a "{}" in it, which has no value to
		// substitute here — the total goes in the value column instead.
		std::string caption = entry.display_name.empty() ? entry.effect_label : entry.display_name;
		if (const auto brace = caption.find("{}"); brace != std::string::npos)
			caption.erase(brace, 2);
		while (!caption.empty() && caption.back() == ' ') caption.pop_back();
		if (caption.empty()) caption = std::format("Modifier {}", static_cast<int>(id));

		row = pane_row(x, row, w, caption.c_str(), std::format("+{}", total));
	}

	if (dropped > 0)
		theme::label(x, row, std::format("+{} more", dropped).c_str(), theme::palette::dim);
}

void DialogBox_Character::draw_pane(int sX, int sY)
{
	namespace theme = hb::client::ui_theme;

	// Folded away: only the handle that opens it again shows, on the column edge.
	if (m_pane == pane::none)
	{
		theme::tab_bar(sX + tab_open.x, sY + tab_open.y, tab_open.w, tab_open.h);
		theme::tab(sX + tab_open.x, sY + tab_open.y, tab_open.w, tab_open.h, ">", false);
		return;
	}

	theme::panel(sX + column_w, sY, pane_w, panel_h);
	theme::tab_bar(sX + column_w, sY, pane_w, theme::metrics::tab_height);
	theme::tab(sX + tab_combat.x, sY + tab_combat.y, tab_combat.w, tab_combat.h,
		UI_COMBAT_TAB_COMBAT, m_pane == pane::combat);
	theme::tab(sX + tab_gear.x, sY + tab_gear.y, tab_gear.w, tab_gear.h,
		UI_COMBAT_TAB_GEAR, m_pane == pane::gear);

	const int x = sX + column_w + theme::metrics::margin;
	const int w = pane_w - theme::metrics::margin * 2;
	const int y = sY + theme::metrics::tab_height + theme::metrics::margin;

	const auto summary = hb::client::build_combat_summary(*m_game);
	if (m_pane == pane::combat) draw_combat_pane(x, y, w, summary);
	else                        draw_gear_pane(x, y, w, summary);
}

void DialogBox_Character::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	if (!m_game->ensure_item_configs_loaded()) return;
	short sX = m_x;
	short sY = m_y;
	char collison = -1;
	const bool dialogTrans = config_manager::get().is_dialog_transparency_enabled();

	// The panel sprite is only 270 wide, so it paints the column; the pane draws
	// its own surface beside it.
	draw_new_dialog_box(InterfaceNdText, sX, sY, 0, false, dialogTrans);
	draw_pane(sX, sY);

	// Chrome the panel art used to carry. The value column starts at sX + 180,
	// so captions are right-aligned to sX + 176 — the gap the original art left
	// between its label column and its value boxes.
	namespace theme = hb::client::ui_theme;
	constexpr int caption_right = 176;
	theme::title(sX, sY + 4, column_w, UI_CHARACTER_TITLE);
	theme::separator(sX + 12, sY + 96, column_w - 24);
	theme::separator(sX + 12, sY + 277, column_w - 24);

	// The caption and the value of a row share one y. They used to be two lists
	// of the same eight numbers ninety lines apart, which is exactly the drift
	// the flat panel exists to end. The pitch is irregular because the art's was.
	for (int i = 0; i < row_count; i++)
		put_row_caption(sX + caption_right, sY + row_y[i], row_caption[i]);

	// Stat block: three columns of two. Each caption sits immediately left of
	// its value's own aligned box (lefts at 44, 131, 214).
	put_row_caption(sX + 42, sY + stat_row_y[0], UI_CHARACTER_STR);
	put_row_caption(sX + 42, sY + stat_row_y[1], UI_CHARACTER_DEX);
	put_row_caption(sX + 129, sY + stat_row_y[0], UI_CHARACTER_INT);
	put_row_caption(sX + 129, sY + stat_row_y[1], UI_CHARACTER_MAG);
	put_row_caption(sX + 212, sY + stat_row_y[0], UI_CHARACTER_VIT);
	put_row_caption(sX + 212, sY + stat_row_y[1], UI_CHARACTER_CHR);

	// Player name and PK/contribution
	std::string txt2;
	std::string infoBuf = player().m_player_name + " : ";

	if (player().m_pk_count > 0) {
		txt2 = std::format(DRAW_DIALOGBOX_CHARACTER1, player().m_pk_count);
		infoBuf += txt2;
	}
	txt2 = std::format(DRAW_DIALOGBOX_CHARACTER2, player().m_contribution);
	infoBuf += txt2;
	put_aligned_string(sX + 24, sX + 252, sY + 52, infoBuf.c_str(), GameColors::UIDarkRed);

	// Citizenship status
	std::string statusBuf;
	if (!player().m_citizen)
	{
		statusBuf = DRAW_DIALOGBOX_CHARACTER7;
	}
	else
	{
		statusBuf = player().m_hunter
			? (player().m_aresden ? DEF_MSG_ARECIVIL : DEF_MSG_ELVCIVIL)
			: (player().m_aresden ? DEF_MSG_ARESOLDIER : DEF_MSG_ELVSOLDIER);
	}
	put_aligned_string(sX, sX + 275, sY + 69, statusBuf.c_str(), GameColors::UILabel);

	// Level, Exp, Next Exp
	std::string statBuf;
	statBuf = std::format("{}", player().m_level);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_level], statBuf.c_str(), GameColors::UILabel);

	statBuf = m_game->format_comma_number(player().m_exp);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_exp], statBuf.c_str(), GameColors::UILabel);

	statBuf = m_game->format_comma_number(m_game->get_level_exp(player().m_level + 1));
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_next_exp], statBuf.c_str(), GameColors::UILabel);

	// Calculate max stats. Gear attributes feed these exactly as they do in
	// CGame::get_max_hp / get_max_mp / get_max_sp / calc_max_load — the client
	// re-derives the same formulas, so it has to read the same inputs or the
	// bars and the carry limit disagree with the server's.
	int max_hp = hb::shared::calc::max_hp(m_game->m_formula_engine,
		hb::shared::calc::vit{(double)player().effective_vit()}, hb::shared::calc::level{(double)player().m_level},
		hb::shared::calc::str{(double)player().effective_str()}, hb::shared::calc::angelic_str{(double)player().m_angelic_str});
	int max_mp = hb::shared::calc::max_mp(m_game->m_formula_engine,
		hb::shared::calc::mag{(double)player().effective_mag()}, hb::shared::calc::angelic_mag{(double)player().m_angelic_mag},
		hb::shared::calc::level{(double)player().m_level}, hb::shared::calc::intel{(double)player().effective_int()},
		hb::shared::calc::angelic_int{(double)player().m_angelic_int});
	int max_sp = hb::shared::calc::max_sp(m_game->m_formula_engine,
		hb::shared::calc::str{(double)player().effective_str()}, hb::shared::calc::angelic_str{(double)player().m_angelic_str},
		hb::shared::calc::level{(double)player().m_level});
	int max_load = hb::shared::calc::max_load(m_game->m_formula_engine,
		hb::shared::calc::str{(double)player().effective_str()}, hb::shared::calc::angelic_str{(double)player().m_angelic_str},
		hb::shared::calc::level{(double)player().m_level});

	// HP, MP, SP
	std::string valueBuf;
	valueBuf = std::format("{}/{}", player().m_hp, max_hp);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_health], valueBuf.c_str(), GameColors::UILabel);

	valueBuf = std::format("{}/{}", player().m_mp, max_mp);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_mana], valueBuf.c_str(), GameColors::UILabel);

	valueBuf = std::format("{}/{}", player().m_sp, max_sp);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_stamina], valueBuf.c_str(), GameColors::UILabel);

	// Max load
	int total_weight = inventory_manager::get().calc_total_weight();
	valueBuf = std::format("{:.2f}/{:.2f}", CItem::weight_to_stones(total_weight), CItem::weight_to_stones(max_load));
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_max_load], valueBuf.c_str(), GameColors::UILabel);

	// Enemy Kills
	valueBuf = std::format("{}", player().m_enemy_kill_count);
	put_aligned_string(sX + 180, sX + 250, sY + row_y[row_ek_count], valueBuf.c_str(), GameColors::UILabel);

	// Stats with angelic and gear bonuses
	namespace tier_attribute = hb::shared::item::tier_attribute;
	draw_stat(sX + 44, sX + 86, sY + stat_row_y[0], player().m_str, player().m_angelic_str,
		player().m_gear_attribute[tier_attribute::strength]);       // Str
	draw_stat(sX + 48, sX + 86, sY + stat_row_y[1], player().m_dex, player().m_angelic_dex,
		player().m_gear_attribute[tier_attribute::dexterity]);      // Dex
	draw_stat(sX + 131, sX + 171, sY + stat_row_y[0], player().m_int, player().m_angelic_int,
		player().m_gear_attribute[tier_attribute::intelligence]);   // Int
	draw_stat(sX + 131, sX + 171, sY + stat_row_y[1], player().m_mag, player().m_angelic_mag,
		player().m_gear_attribute[tier_attribute::magic]);          // Mag

	// Vit and Chr have no angelic pendant, but gear rolls both, so they route
	// through the same drawer instead of the two hand-rolled lines that were
	// here — otherwise a +VIT roll would be the one bonus that stayed invisible.
	draw_stat(sX + 214, sX + 255, sY + stat_row_y[0], player().m_vit, 0,
		player().m_gear_attribute[tier_attribute::vitality]);       // Vit
	draw_stat(sX + 214, sX + 255, sY + stat_row_y[1], player().m_charisma, 0,
		player().m_gear_attribute[tier_attribute::charisma]);       // Chr

	// Build equipment status array
	char equip_poi_status[DEF_MAXITEMEQUIPPOS];
	build_equip_status_array(equip_poi_status);

	// draw character model based on gender
	if (player().m_player_type >= 1 && player().m_player_type <= 3)
	{
		draw_male_character(sX, sY, mouse_x, mouse_y, equip_poi_status, collison);
	}
	else if (player().m_player_type >= 4 && player().m_player_type <= 6)
	{
		draw_female_character(sX, sY, mouse_x, mouse_y, equip_poi_status, collison);
	}

	// draw buttons (Quest, Party, LevelUp)
	draw_button(sX, sY, btn_quest, UI_BTN_QUEST);
	draw_button(sX, sY, btn_party, UI_BTN_PARTY);
	draw_button(sX, sY, btn_levelup, UI_CHARACTER_BTN_LEVELUP);
}

void DialogBox_Character::draw_male_character(short sX, short sY, short mouse_x, short mouse_y,
	const char* equip_poi_status, char& collison)
{
	// Base body
	m_game->m_sprite[ItemEquipPivotPoint + 0]->draw(sX + 171, sY + 290, player().m_player_type - 1);

	// Hair (if no helmet)
	if (equip_poi_status[to_int(EquipPos::Head)] == -1)
	{
		const auto& hc = m_game->m_color_palette[player().m_playerAppearance.hair_color];
		m_game->m_sprite[ItemEquipPivotPoint + 18]->draw(sX + 171, sY + 290, player().m_playerAppearance.hair_style, hb::shared::sprite::DrawParams::tint(hc.r, hc.g, hc.b));
	}

	// Underwear
	m_game->m_sprite[ItemEquipPivotPoint + 19]->draw(sX + 171, sY + 290, player().m_playerAppearance.underwear_type);

	// Find topmost hovered slot (reverse scan) before drawing
	EquipPos hoverSlot = FindHoverSlot(m_game, MaleEquipSlots, static_cast<int>(std::size(MaleEquipSlots)),
		sX, sY, mouse_x, mouse_y, equip_poi_status, 0);
	if (hoverSlot != EquipPos::None)
		collison = static_cast<char>(hoverSlot);

	// Equipment slots (draw order from table)
	for (const auto& slot : MaleEquipSlots)
	{
		draw_equipped_item(slot.equipPos, sX + slot.offsetX, sY + slot.offsetY,
			equip_poi_status, slot.equipPos == hoverSlot);
	}

}

void DialogBox_Character::draw_female_character(short sX, short sY, short mouse_x, short mouse_y,
	const char* equip_poi_status, char& collison)
{
	// Base body (female uses +40 offset from male sprites)
	m_game->m_sprite[ItemEquipPivotPoint + 40]->draw(sX + 171, sY + 290, player().m_player_type - 4);

	// Hair (if no helmet) - female hair is at +18+40 = +58
	if (equip_poi_status[to_int(EquipPos::Head)] == -1)
	{
		const auto& hc = m_game->m_color_palette[player().m_playerAppearance.hair_color];
		m_game->m_sprite[ItemEquipPivotPoint + 18 + 40]->draw(sX + 171, sY + 290, player().m_playerAppearance.hair_style, hb::shared::sprite::DrawParams::tint(hc.r, hc.g, hc.b));
	}

	// Underwear - female underwear is at +19+40 = +59
	m_game->m_sprite[ItemEquipPivotPoint + 19 + 40]->draw(sX + 171, sY + 290, player().m_playerAppearance.underwear_type);

	// Check for skirt in pants slot (sprite 12, frame 0 = skirt)
	bool skirt = false;
	if (equip_poi_status[to_int(EquipPos::Leggings)] != -1)
	{
		CItem* cfg = m_game->get_item_config(player().m_item_list[equip_poi_status[to_int(EquipPos::Leggings)]]->m_id_num);
		if (cfg != nullptr && player().m_item_list[equip_poi_status[to_int(EquipPos::Leggings)]]->m_id_num == 479) // Skirt (W)
			skirt = true;
	}

	// Find topmost hovered slot (reverse scan) before drawing
	EquipPos hoverSlot = FindHoverSlot(m_game, FemaleEquipSlots, static_cast<int>(std::size(FemaleEquipSlots)),
		sX, sY, mouse_x, mouse_y, equip_poi_status, 40);
	if (hoverSlot != EquipPos::None)
		collison = static_cast<char>(hoverSlot);

	// If wearing skirt, pre-draw boots under the skirt
	if (skirt)
		draw_equipped_item(EquipPos::Boots, sX + 171, sY + 290, equip_poi_status, hoverSlot == EquipPos::Boots, 40);

	// Equipment slots (draw order from table)
	for (const auto& slot : FemaleEquipSlots)
	{
		if (skirt && slot.equipPos == EquipPos::Boots) continue; // already drawn
		draw_equipped_item(slot.equipPos, sX + slot.offsetX, sY + slot.offsetY,
			equip_poi_status, slot.equipPos == hoverSlot, 40);
	}

}

bool DialogBox_Character::on_click()
{
	// Side pane tabs. Picking the tab that is already showing folds the pane away,
	// which is what makes it a slide-out rather than a second column.
	if (m_pane == pane::none && mouse_in(tab_open)) return toggle_pane(pane::combat);
	if (m_pane != pane::none && mouse_in(tab_combat)) return toggle_pane(pane::combat);
	if (m_pane != pane::none && mouse_in(tab_gear)) return toggle_pane(pane::gear);

	// Quest button
	if (mouse_in(btn_quest)) {
		enable_dialog_box(DialogBoxId::Quest, 1, 0, 0);
		disable_this_dialog();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}
	// Party button
	if (mouse_in(btn_party)) {
		enable_dialog_box(DialogBoxId::Party, 0, 0, 0);
		disable_this_dialog();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}
	// LevelUp button
	if (mouse_in(btn_levelup)) {
		enable_dialog_box(DialogBoxId::LevelUpSetting, 0, 0, 0);
		disable_this_dialog();
		audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
		return true;
	}

	return false;
}

bool DialogBox_Character::on_double_click()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	if (m_game->get_dialog_box_manager().is_enabled(DialogBoxId::ItemDropExternal))
		return false;

	short sX = m_x;
	short sY = m_y;

	// Build equipment position status array
	char equip_poi_status[DEF_MAXITEMEQUIPPOS];
	build_equip_status_array(equip_poi_status);

	// Find clicked item
	char item_id = find_equip_item_at_point(mouse_x, mouse_y, sX, sY, equip_poi_status);
	if (item_id == -1 || player().m_item_list[item_id] == nullptr)
		return false;

	CItem* item = player().m_item_list[item_id].get();
	CItem* cfg = m_game->get_item_config(item->m_id_num);
	if (cfg == nullptr)
		return false;

	// Skip consumables, arrows, and stacked items
	if (cfg->get_item_type() == hb::shared::item::item_type::consumable ||
		cfg->get_item_sub_type() == hb::shared::item::item_sub_type::ammo ||
		item->m_instance.count > 1)
		return false;

	// Check if at repair shop
	if (m_game->get_dialog_box_manager().is_enabled(DialogBoxId::SaleMenu) &&
		!m_game->get_dialog_box_manager().is_enabled(DialogBoxId::SellOrRepair) &&
		m_game->get_dialog_box_manager().m_give_item.action_type == 24)
	{
		{
			auto pkt = hb::net::make_common_command_str(CommonType::ReqRepairItem, player().m_player_x, player().m_player_y);
			pkt.v1 = item_id;
			pkt.v2 = m_game->get_dialog_box_manager().m_give_item.action_type;
			std::snprintf(pkt.text, sizeof(pkt.text), "%s", cfg->m_name);
			pkt.v4 = m_game->get_dialog_box_manager().m_give_item.object_id;
			send_game_packet(pkt);
		}
	}
	else
	{
		// Release (unequip) the item — server will send Notify::ItemReleased with message + sound
		if (m_game->m_is_item_equipped[item_id])
		{
			// Remove Angelic Stats
			if (cfg->get_equip_pos() >= EquipPos::LeftFinger &&
				cfg->get_item_type() == hb::shared::item::item_type::equipment)
			{
				if (item->m_id_num == hb::shared::item::ItemId::AngelicPendantSTR)
					player().m_angelic_str = 0;
				else if (item->m_id_num == hb::shared::item::ItemId::AngelicPendantDEX)
					player().m_angelic_dex = 0;
				else if (item->m_id_num == hb::shared::item::ItemId::AngelicPendantINT)
					player().m_angelic_int = 0;
				else if (item->m_id_num == hb::shared::item::ItemId::AngelicPendantMAG)
					player().m_angelic_mag = 0;
			}

			{
				auto pkt = hb::net::make_common_command(CommonType::ReleaseItem, player().m_player_x, player().m_player_y);
				pkt.v1 = item_id;
				send_game_packet(pkt);
			}
			m_game->m_is_item_equipped[item_id] = false;
			m_game->m_item_equipment_status[cfg->m_equip_pos] = -1;
			CursorTarget::clear_selection();
		}
	}

	return true;
}

PressResult DialogBox_Character::on_press()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	if (m_game->get_dialog_box_manager().is_enabled(DialogBoxId::ItemDropExternal))
		return PressResult::Normal;

	short sX = m_x;
	short sY = m_y;

	char equip_poi_status[DEF_MAXITEMEQUIPPOS];
	build_equip_status_array(equip_poi_status);

	char itemIdx = find_equip_item_at_point(mouse_x, mouse_y, sX, sY, equip_poi_status);
	if (itemIdx != -1)
	{
		CursorTarget::set_selection(SelectedObjectType::Item, static_cast<short>(itemIdx), 0, 0);
		return PressResult::ItemSelected;
	}

	return PressResult::Normal;
}

bool DialogBox_Character::on_item_drop()
{
	inventory_manager::get().equip_item(static_cast<char>(CursorTarget::get_selected_id()));
	return true;
}
