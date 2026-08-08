#include "DialogBox_Character.h"
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
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include "IInput.h"
#include "AudioManager.h"

using namespace hb::shared::net;
using namespace hb::shared::item;

using hb::shared::item::EquipPos;
using namespace hb::client::sprite_id;

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
	m_pane_scroll = 0;
}

// Show `which`, or fold the pane away when it is already the one showing.
bool DialogBox_Character::toggle_pane(pane which)
{
	set_pane(m_pane == which ? pane::none : which);
	audio_manager::get().play_game_sound(sound_type::effect, 14, 5);
	return true;
}

// One stat cell: the number, then its gear bonus beside it in its own color.
//
// These used to be one string in one color, so the BASE number turned blue
// because something else was boosting it — the colour said "modified" about the
// wrong half. The value now reads in the ordinary value colour and only the
// parenthesised delta is the modified one.
//
// Angelic keeps folding into the number, exactly as it always has: it is a
// property of one legacy pendant, every other surface folds it the same way,
// and max HP/MP/SP are derived from base+angelic too.
void DialogBox_Character::draw_stat(int x_right, int y, int base_stat, int angelic_bonus, int gear_bonus)
{
	namespace theme = hb::client::ui_theme;
	namespace text = hb::shared::text;

	// Right-aligned as a pair: the bonus takes the end of the column and the
	// value is measured back from it, so the two never overlap however wide
	// either grows.
	int right = x_right;
	if (gear_bonus != 0)
	{
		const auto bonus = std::format("({:+})", gear_bonus);
		theme::value(m_x, y, right, bonus.c_str(), GameColors::UIModifiedStat);
		right -= text::measure_text(GameFont::Default, bonus.c_str()).width + 4;
	}

	theme::value(m_x, y, right, std::format("{}", base_stat + angelic_bonus).c_str());
}

// The three accessory wells, drawn before the paperdoll so a worn ring's icon
// sits inside its well rather than behind it.
void DialogBox_Character::draw_accessory_slots(int sX, int sY) const
{
	namespace theme = hb::client::ui_theme;
	for (const ui_rect& slot : { slot_neck, slot_ring_r, slot_ring_l })
		theme::content_frame(sX + slot.x, sY + slot.y, slot.w, slot.h);
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

// A line of wrapped text as a whole number of slots. Measured rather than
// assumed: the Gear tab's footer wraps to two lines at this width, and a line
// that claimed one slot would be half-drawn when it landed at the bottom of the
// pane. Headings and notes differ only in colour, so they share this.
DialogBox_Character::pane_line DialogBox_Character::make_text(pane_line::kind type, const char* text)
{
	const int h = hb::shared::text::measure_wrapped_text_height(
		GameFont::Default, text, pane_content_w);
	pane_line line;
	line.type = type;
	line.caption = text;
	line.slots = std::max((h + pane_pitch - 1) / pane_pitch, 1);
	return line;
}

// A replicated bucket name as a heading a player would write.
//
// The catalog stores them as SCREAMING_SNAKE keys — DAMAGE, SET_AXIS,
// COMBAT_UTILITY — because that is what they are on the server: identifiers. On
// screen they sat in the middle of a panel whose every other heading is ordinary
// sentence case, shouting. Cased here rather than in the data because the key is
// the thing the two sides agree on and the wording is nobody's business but this
// panel's.
static std::string display_case(std::string_view key)
{
	// One name is not merely cased but replaced. SET_AXIS is the spec's term for
	// the axis a set bonus sits on — accurate about the catalog and meaningless
	// to the player wearing one, who has Sets.
	if (key == "SET_AXIS") return "Sets";

	std::string out;
	out.reserve(key.size());
	for (const char c : key)
		out += (c == '_') ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (!out.empty())
		out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
	return out;
}

std::vector<DialogBox_Character::pane_line> DialogBox_Character::build_combat_lines(
	const hb::client::combat_summary& summary) const
{
	std::vector<pane_line> lines;

	const auto heading = [&lines](const char* text)
	{
		lines.push_back(make_text(pane_line::kind::heading, text));
	};
	const auto row = [&lines](const char* caption, std::string value, bool notable = false)
	{
		lines.push_back({ pane_line::kind::row, caption, std::move(value), notable, 1 });
	};
	const auto rule = [&lines]()
	{
		lines.push_back({ pane_line::kind::rule, {}, {}, false, 1 });
	};

	heading(UI_COMBAT_HEAD_ATTACK);

	if (summary.has_weapon)
	{
		row(UI_COMBAT_DAMAGE_SMALL,
			std::format("{}-{}", summary.damage_small.min, summary.damage_small.max), true);
		// Large-target dice only differ on weapons configured for it; a weapon
		// with the same numbers on both sides would just print the row twice.
		if (summary.damage_large.max != summary.damage_small.max
			|| summary.damage_large.min != summary.damage_small.min)
		{
			row(UI_COMBAT_DAMAGE_LARGE,
				std::format("{}-{}", summary.damage_large.min, summary.damage_large.max));
		}
	}
	else
	{
		row(UI_COMBAT_DAMAGE_SMALL, UI_COMBAT_NO_WEAPON);
	}

	// attack_delay is the 0-15 delay unit the status broadcast carries, not a
	// duration — printed with "ms" after it, a 1.2-second swing read as "6 ms".
	// swing_time is the arithmetic that turns one into the other.
	row(UI_COMBAT_ATTACK_DELAY,
		std::format("{} ms", hb::shared::calc::swing_time(summary.attack_delay)));

	// The equipment half of the hit roll, server-side total. Still a bonus and
	// not a ratio: what it lands on depends on the weapon's own mastery and is
	// clamped at both ends, so a single "hit ratio" would be a fiction.
	if (const int hit = player().m_derived.hit_bonus; hit != 0)
		row(UI_COMBAT_HIT_BONUS, std::format("+{}", hit));

	if (const int bonus = player().m_derived.physical_damage; bonus != 0)
		row(UI_COMBAT_PHYS_DAMAGE, std::format("+{}", bonus));
	if (const int bonus = player().m_derived.magical_damage; bonus != 0)
		row(UI_COMBAT_MAGIC_DAMAGE, std::format("+{}", bonus));

	// Derived on the client rather than sent, unlike the three rows above it.
	// Every term is in the item configs the client already caches — get_mana_cost
	// has totalled exactly this at every cast since long before the panel
	// existed — so the wire field the wire gap seemed to call for was never
	// needed. Capped at 80 in build_combat_summary, as the equip pass caps it.
	if (summary.mana_save_pct != 0)
		row(UI_COMBAT_MANA_SAVE, std::format("{}%", summary.mana_save_pct));

	rule();
	heading(UI_COMBAT_HEAD_DEFENCE);

	// Real totals now, off Notify::DerivedStats — these four were the rows the
	// server owned and the client could only guess at, so they used to carry the
	// gear contribution and apologise for it at the foot of the pane.
	const auto& derived = player().m_derived;

	row(UI_COMBAT_DEFENCE_RATIO, std::format("{}", derived.defense_ratio), true);
	row(UI_COMBAT_ARMOUR, std::format("+{}%", summary.armour_defence));

	// Averaged, because absorption is per hit zone and the zones are
	// alternatives rather than addends — a blow lands on exactly one. Summing
	// them would overstate it about fourfold, which is why this is the one
	// defensive figure that has to be labelled as a mean.
	row(UI_COMBAT_PHYS_ABSORB,
		std::format("{}%", hb::shared::calc::physical_absorption_average(
			derived.absorb_body, derived.absorb_legs, derived.absorb_arms, derived.absorb_head)));

	// The shield is not a zone: it stacks on top of whichever one was struck,
	// but only when the parry roll comes in under Shield mastery, so it is its
	// own row rather than folded into the average.
	if (derived.absorb_shield != 0)
		row(UI_COMBAT_SHIELD_ABSORB, std::format("{}%", derived.absorb_shield));

	row(UI_COMBAT_MAGIC_RESIST, std::format("{}", derived.magic_resistance));
	row(UI_COMBAT_MAGIC_ABSORB, std::format("{}%", derived.magic_absorb));

	// Not a percentage, though it shipped as one last cycle. Poison resistance
	// is added to the Poison Resistance mastery and the sum is rolled against
	// dice(1,100) to shake the poison off — a score, exactly like the magic
	// resistance row above it, which is already drawn bare. It also has a
	// threshold rather than a ceiling: at 500 the server stops poison landing
	// at all, and "500%" said nothing about that.
	row(UI_COMBAT_POISON_RESIST, std::format("{}", derived.poison_resistance));

	// Elemental absorption only earns rows when something provides it — four
	// zeroes is a longer pane saying nothing.
	const struct { const char* caption; int value; } elemental[] = {
		{ UI_COMBAT_ABSORB_FIRE,  derived.absorb_fire },
		{ UI_COMBAT_ABSORB_ICE,   derived.absorb_water },
		{ UI_COMBAT_ABSORB_LIGHT, derived.absorb_air },
		{ UI_COMBAT_ABSORB_EARTH, derived.absorb_earth },
	};
	for (const auto& line : elemental)
		if (line.value != 0)
			row(line.caption, std::format("{}%", line.value));

	rule();
	heading(UI_COMBAT_HEAD_RECOVERY);

	// The per-tick roll window, off the same DB formulas RegenManager evaluates:
	// three rolls of dice(1, ceiling) floored at base_floor, so what can land is
	// base_floor..ceiling, then scaled by the recovery percentage the worn set
	// adds.
	//
	// BASE vit and mag, deliberately — the server passes p->m_vit and p->m_mag,
	// not effective_*(), so gear +VIT raises max HP without raising regen. A
	// panel quietly using the effective values here would promise recovery the
	// server never pays.
	const auto& engine = m_game->m_formula_engine;
	const auto vit = hb::shared::calc::vit{ static_cast<double>(player().m_vit) };
	const auto mag = hb::shared::calc::mag{ static_cast<double>(player().m_mag) };
	const auto angelic_mag = hb::shared::calc::angelic_mag{ static_cast<double>(player().m_angelic_mag) };

	const struct { const char* caption; int floor_roll; int ceiling; int bonus_pct; } recovery[] = {
		{ UI_COMBAT_RECOVER_HP, hb::shared::calc::hp_regen_min_roll(engine, vit),
		  hb::shared::calc::hp_regen_max_roll(engine, vit), summary.hp_recovery_pct },
		{ UI_COMBAT_RECOVER_MP, hb::shared::calc::mp_regen_min_roll(engine, mag, angelic_mag),
		  hb::shared::calc::mp_regen_max_roll(engine, mag, angelic_mag), summary.mp_recovery_pct },
		{ UI_COMBAT_RECOVER_SP, hb::shared::calc::sp_regen_min_roll(engine, vit),
		  hb::shared::calc::sp_regen_max_roll(engine, vit), summary.sp_recovery_pct },
	};
	for (const auto& line : recovery)
	{
		// A non-positive ceiling is the server's own "regenerates nothing" exit.
		if (line.ceiling <= 0)
		{
			row(line.caption, "0");
			continue;
		}
		row(line.caption,
			std::format("{} - {}",
				hb::shared::calc::apply_percent_bonus(line.floor_roll, line.bonus_pct),
				hb::shared::calc::apply_percent_bonus(line.ceiling, line.bonus_pct)));
	}
	lines.push_back(make_text(pane_line::kind::note, UI_COMBAT_PER_TICK));

	return lines;
}

std::vector<DialogBox_Character::pane_line> DialogBox_Character::build_gear_lines(
	const hb::client::combat_summary& summary) const
{
	std::vector<pane_line> out;

	const auto heading = [&out](const char* text)
	{
		out.push_back(make_text(pane_line::kind::heading, text));
	};
	const auto row = [&out](std::string caption, std::string value)
	{
		out.push_back({ pane_line::kind::row, std::move(caption), std::move(value), false, 1 });
	};

	// No pane-level heading: the tab already says Gear, and the bucket titles
	// below are the headings that carry information. "From Equipment" was one
	// more line of chrome in a pane whose whole problem is height.
	//
	// Every rolled line the set carries, labelled AND formatted from the
	// replicated catalog so the wording and the unit both match the item's own
	// tooltip.
	//
	// This printed std::format("+{}", total) for every row regardless of what
	// the catalog said that row was. Nine kinds of percentage came out as bare
	// numbers, six gained a "+" their format never had, and the two reduction
	// rows — Light and Cast Time — came out with the sign inverted, a reduction
	// displayed as a gain.
	//
	// Three deliberate omissions. Attribute rows: the stat block already shows
	// them from the server's own packet, and the copy here would be a half-truth,
	// because add_rolled_lines attributes only the first half of a pair — the
	// catalog never names the paired attribute. The three recovery rows: they
	// get their own group below, as real totals rather than as rolled lines.
	// Fixed storage: the pane redraws every frame it is open and the list is
	// bounded by the catalog itself, so there is no reason to reach the heap.
	namespace mod = hb::shared::item::modifier_id;
	struct catalog_line { uint8_t sort_order; uint8_t id; };
	std::array<catalog_line, mod::move_speed + 1> lines{};
	std::size_t line_count = 0;
	bool has_attributes = false;

	for (uint8_t id = 1; id < summary.gear.size(); id++)
	{
		if (summary.gear[id] == 0) continue;

		if (id >= mod::attr_str && id <= mod::attr_all_stats) { has_attributes = true; continue; }
		if (id == mod::hp_recovery || id == mod::mp_recovery || id == mod::sp_recovery) continue;

		const auto& entry = m_game->m_modifier_catalog[id];
		if (!entry.present) continue;
		lines[line_count++] = { entry.bucket_sort_order, id };
	}
	const auto first = lines.begin();
	const auto last = lines.begin() + static_cast<std::ptrdiff_t>(line_count);

	const bool has_recovery = summary.hp_recovery_pct != 0
		|| summary.mp_recovery_pct != 0 || summary.sp_recovery_pct != 0;

	if (line_count == 0 && !has_attributes && !has_recovery)
	{
		out.push_back(make_text(pane_line::kind::note, UI_COMBAT_NO_GEAR));
		return out;
	}

	// Bucket order is the catalog's own sort_order, replicated for exactly this
	// and never read here before — the list only looked grouped because the seed
	// happens to keep bucket_id and sort_order equal. The bucket NAMES are not
	// replicated, so the groups are ruled apart rather than titled.
	std::sort(first, last, [](const catalog_line& a, const catalog_line& b)
	{
		return a.sort_order != b.sort_order ? a.sort_order < b.sort_order : a.id < b.id;
	});

	// A fully Legendary set rolls more lines than the pane is tall. That used to
	// be met with a bottom cut-off and a "+N more" tally — better than dropping
	// them silently, but still a list the player could not read to the end. The
	// pane scrolls now, so every line is built and none is discarded.
	uint8_t bucket = 0;
	bool in_group = false;

	for (auto it = first; it != last; ++it)
	{
		const catalog_line& line = *it;
		const auto& entry = m_game->m_modifier_catalog[line.id];

		// Groups are titled now that the bucket name replicates. The rule goes
		// between groups rather than before the first, and a row whose bucket
		// never replicated still gets its group, just no heading — a title is
		// the one thing better omitted than invented.
		if (!in_group || line.sort_order != bucket)
		{
			if (in_group)
				out.push_back({ pane_line::kind::rule, {}, {}, false, 1 });
			bucket = line.sort_order;
			in_group = true;

			if (!entry.bucket_name.empty())
				heading(display_case(entry.bucket_name).c_str());
		}

		// The prefix word when the row has one, else its tooltip label. A label
		// can be a whole-line format with a "{}" in it, which has no value to
		// substitute here — the total goes in the value column instead.
		std::string caption = entry.display_name.empty() ? entry.effect_label : entry.display_name;
		if (const auto brace = caption.find("{}"); brace != std::string::npos)
			caption.erase(brace, 2);
		while (!caption.empty() && caption.back() == ' ') caption.pop_back();
		if (caption.empty()) caption = std::format("Modifier {}", static_cast<int>(line.id));

		// A row with no format of its own is a label-only row; the bare total is
		// the only honest thing to put beside it.
		const int total = summary.gear[line.id];
		row(std::move(caption), entry.effect_format.empty()
			? std::to_string(total)
			: format_catalog_value(entry.effect_format, static_cast<uint32_t>(total)));
	}

	// Recovery, as real totals rather than gear contribution — see combat_summary.
	if (has_recovery)
	{
		out.push_back({ pane_line::kind::rule, {}, {}, false, 1 });
		heading(UI_COMBAT_RECOVERY_RATE);

		row(UI_COMBAT_RECOVER_HP, std::format("{:+}%", summary.hp_recovery_pct));
		row(UI_COMBAT_RECOVER_MP, std::format("{:+}%", summary.mp_recovery_pct));
		row(UI_COMBAT_RECOVER_SP, std::format("{:+}%", summary.sp_recovery_pct));
	}

	// A note rather than a label: the pane's usable width is 180px and this
	// sentence does not fit in it, so it is measured and wrapped like any other.
	if (has_attributes)
		out.push_back(make_text(pane_line::kind::note, UI_COMBAT_ATTRS_ELSEWHERE));

	return out;
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

	const int x = sX + column_w + pane_margin;
	const int y = sY + pane_top;

	const auto summary = hb::client::build_combat_summary(*m_game);
	draw_pane_lines(x, y, pane_content_w, m_pane == pane::combat
		? build_combat_lines(summary)
		: build_gear_lines(summary));
}

// Draw the visible window of a built pane, and run its scrolling.
//
// Row-indexed rather than pixel-scrolled, which is the pattern the six list
// dialogs already use — and here it is not merely convention. IRenderer's
// set_clip_area stores a rect and clips nothing (SFMLRenderer.cpp says so in its
// own comment, and no caller anywhere passes through it), so there is no scissor
// to draw against: anything scrolled past the edge would paint over the panel
// and the world behind it. Advancing by whole lines means nothing is ever drawn
// outside the pane in the first place.
void DialogBox_Character::draw_pane_lines(int x, int y, int w,
	const std::vector<pane_line>& lines)
{
	namespace theme = hb::client::ui_theme;

	// The first index from which everything remaining fits. Walked backwards
	// from the end so a two-slot note at the bottom is accounted for exactly,
	// rather than approximated from an average line height — get that wrong and
	// the last line is unreachable, which is the failure the player notices.
	int max_scroll = static_cast<int>(lines.size());
	for (int i = static_cast<int>(lines.size()) - 1, used = 0; i >= 0; i--)
	{
		used += lines[i].slots * pane_pitch;
		if (used > pane_content_h) break;
		max_scroll = i;
	}

	const bool scrollable = max_scroll > 0;
	handle_vscroll(pane_scroll_bar, max_scroll, m_pane_scroll);

	int row = y;
	for (std::size_t i = static_cast<std::size_t>(m_pane_scroll); i < lines.size(); i++)
	{
		const pane_line& line = lines[i];
		const int height = line.slots * pane_pitch;
		if (row + height > y + pane_content_h) break;

		switch (line.type)
		{
		case pane_line::kind::heading:
			// Wrapped like a note, coloured like a tab: the Gear tab's headings
			// are replicated bucket names and nothing bounds their length.
			theme::wrapped_label(x, row, w, line.caption.c_str(), theme::palette::tab_active);
			break;
		case pane_line::kind::row:
			pane_row(x, row, w, line.caption.c_str(), line.value, line.notable);
			break;
		case pane_line::kind::rule:
			// Centred in its own slot, which is exactly the air the half-pitch
			// above and below used to give it.
			theme::separator(x, row + pane_pitch / 2, w);
			break;
		case pane_line::kind::note:
			theme::wrapped_label(x, row, w, line.caption.c_str(), theme::palette::dim);
			break;
		}
		row += height;
	}

	if (!scrollable) return;

	// Thumb sized by how much of the list is showing, so its length says how much
	// there is left — a fixed thumb tells the player nothing about a list that
	// might be two lines over or forty.
	int total_slots = 0;
	for (const pane_line& line : lines) total_slots += line.slots;

	const int shown = pane_content_h / pane_pitch;
	const int thumb = std::clamp(pane_content_h * shown / std::max(total_slots, 1),
		pane_pitch, pane_content_h);

	theme::scrollbar(m_x + pane_scroll_bar.x, m_y + pane_scroll_bar.y, pane_scroll_bar.w,
		pane_content_h, (pane_content_h - thumb) * m_pane_scroll / max_scroll, thumb);
}

void DialogBox_Character::on_draw()
{
	short mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());
	short mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());
	if (!m_game->ensure_item_configs_loaded()) return;
	short sX = m_x;
	short sY = m_y;
	char collison = -1;

	// This dialog draws its own column instead of going through the seam, which
	// is what lets it be 320 wide.
	//
	// draw_new_dialog_box flattens a panel to the ART FRAME's rect — deliberately,
	// so a converted dialog's hand-measured child offsets stay valid without being
	// touched — and InterfaceNdText frame 0 is 270x376. Routing through it pinned
	// the background at the old size while every offset above moved, which is
	// exactly the mismatch the seam's own comment promises to avoid for dialogs
	// that have NOT been re-laid-out. Once one has, it has to draw its own.
	namespace theme = hb::client::ui_theme;
	theme::panel(sX, sY, column_w, panel_h);
	draw_pane(sX, sY);

	// Chrome the panel art used to carry.
	theme::title(sX, sY + 4, column_w, UI_CHARACTER_TITLE);
	theme::separator(sX + 12, sY + rule_top_y, column_w - 24);
	theme::separator(sX + 12, sY + rule_stat_y, column_w - 24);

	// Player name. The PK count and the contribution used to be appended here as
	// prose — which is why it read "janglez : 10 Contribution" — and they are
	// rows of their own now, so the name is just a name.
	put_aligned_string(sX + 24, sX + column_w - 24, sY + 52,
		player().m_player_name.c_str(), GameColors::UIDarkRed);

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
	put_aligned_string(sX, sX + column_w, sY + 69, statusBuf.c_str(), GameColors::UILabel);

	// Guild identity (#124): the original appended "(Guild Guildmaster)" to
	// the citizenship line; a 20-char guild name walks that off the panel, so
	// it sits on its own row — with the held Title, whose v1 display surfaces
	// are exactly this dialog and the roster (§3.1.1).
	if (player().in_guild())
	{
		std::string guild_line = std::format(UI_GUILD_OVERHEAD_LINE,
			player().m_guild_name, player().m_guild_rank_title);
		if (player().m_guild_title != 0)
			guild_line += std::format(" — {}",
				hb::shared::guild::guild_title_display_name(player().m_guild_title));
		put_aligned_string(sX, sX + column_w, sY + 84, guild_line.c_str(),
			GameColors::UILabel);
	}

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

	// Every row's value, in row_index order. The captions live in one table and
	// the values in another indexed the same way, so the two cannot drift the
	// way eight separate calls ninety lines from their captions could.
	std::string value[row_count];
	value[row_level]        = std::format("{}", player().m_level);
	value[row_exp]          = m_game->format_comma_number(player().m_exp);
	value[row_next_exp]     = m_game->format_comma_number(m_game->get_level_exp(player().m_level + 1));
	value[row_health]       = std::format("{} / {}", player().m_hp, max_hp);
	value[row_mana]         = std::format("{} / {}", player().m_mp, max_mp);
	value[row_stamina]      = std::format("{} / {}", player().m_sp, max_sp);
	value[row_max_load]     = std::format("{:.2f} / {:.2f}",
		CItem::weight_to_stones(inventory_manager::get().calc_total_weight()),
		CItem::weight_to_stones(max_load));
	value[row_ek_count]     = std::format("{}", player().m_enemy_kill_count);
	value[row_pk_count]     = std::format("{}", player().m_pk_count);
	value[row_reputation]   = std::format("{:+}", player().m_rating);
	value[row_contribution] = std::format("{}", player().m_contribution);
	value[row_hunger]       = std::format("{}%", player().m_hunger_status);

	for (int i = 0; i < row_count; i++)
		theme::label_value(sX + caption_x, sX + caption_x, value_right - caption_x,
			sY + row_y[i], row_caption[i], value[i].c_str());

	// Stat block: three columns of two, each caption left and value right in its
	// own column. Vit and Chr have no angelic pendant, but gear rolls both, so
	// they route through the same drawer — otherwise a +VIT roll would be the
	// one bonus that stayed invisible.
	namespace tier_attribute = hb::shared::item::tier_attribute;
	const struct { int column; int row; const char* caption; int base; int angelic; int gear; } stats[] = {
		{ 0, 0, UI_CHARACTER_STR, player().m_str,      player().m_angelic_str, player().m_gear_attribute[tier_attribute::strength] },
		{ 1, 0, UI_CHARACTER_INT, player().m_int,      player().m_angelic_int, player().m_gear_attribute[tier_attribute::intelligence] },
		{ 2, 0, UI_CHARACTER_VIT, player().m_vit,      0,                      player().m_gear_attribute[tier_attribute::vitality] },
		{ 0, 1, UI_CHARACTER_DEX, player().m_dex,      player().m_angelic_dex, player().m_gear_attribute[tier_attribute::dexterity] },
		{ 1, 1, UI_CHARACTER_MAG, player().m_mag,      player().m_angelic_mag, player().m_gear_attribute[tier_attribute::magic] },
		{ 2, 1, UI_CHARACTER_CHR, player().m_charisma, 0,                      player().m_gear_attribute[tier_attribute::charisma] },
	};
	for (const auto& stat : stats)
	{
		const int row_top = sY + stat_row_y[stat.row];
		theme::label(sX + stat_caption_x[stat.column], row_top, stat.caption);
		draw_stat(stat_value_right[stat.column], row_top, stat.base, stat.angelic, stat.gear);
	}

	// Build equipment status array
	char equip_poi_status[DEF_MAXITEMEQUIPPOS];
	build_equip_status_array(equip_poi_status);

	// Under the paperdoll, so a worn ring's icon lands inside its own well.
	draw_accessory_slots(sX, sY);

	// draw character model based on gender
	if (player().m_player_type >= 1 && player().m_player_type <= 3)
	{
		draw_male_character(sX, sY, mouse_x, mouse_y, equip_poi_status, collison);
	}
	else if (player().m_player_type >= 4 && player().m_player_type <= 6)
	{
		draw_female_character(sX, sY, mouse_x, mouse_y, equip_poi_status, collison);
	}

	// draw buttons (Quest, Party, Guild, LevelUp)
	draw_button(sX, sY, btn_quest, UI_BTN_QUEST);
	draw_button(sX, sY, btn_party, UI_BTN_PARTY);
	draw_button(sX, sY, btn_guild, UI_BTN_GUILD);
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
	// Guild button (#124)
	if (mouse_in(btn_guild)) {
		enable_dialog_box(DialogBoxId::GuildMenu, 0, 0, 0);
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

	// The pane's scrollbar, before anything else: without this the drag moves the
	// whole dialog instead of the thumb, which is what every other scrolling
	// dialog uses ScrollClaimed to prevent.
	if (m_pane != pane::none && mouse_in(hb::client::ui_theme::grab_area(pane_scroll_bar)))
		return PressResult::ScrollClaimed;

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
