// Screen_OnGame.DrawObjects.cpp: Screen_OnGame partial implementation — draw_objects coordinator + dispatchers
//
// Moved from Game.DrawObjects.cpp and Game.cpp as part of Phase 5a render pipeline migration.
//
//////////////////////////////////////////////////////////////////////

#include "Screen_OnGame.h"
#include "Game.h"
#include "WeatherManager.h"
#include "ItemNameFormatter.h"
#include "ItemTooltip.h"
#include "Item/ItemInstanceData.h"
#include "ItemSpriteMetadata.h"
#include "RenderHelpers.h"
#include "EntityRenderState.h"
#include "ConfigManager.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "lan_eng.h"
#include "TeleportManager.h"
#include "DialogBox_Party.h"
#include "Packet/SharedPackets.h"
#include "PacketSendHelpers.h"
#include "NetMessages.h"

#include <algorithm>
#include <cmath>
#include <format>



namespace dynamic_object = hb::shared::dynamic_object;

using namespace hb::shared::action;
using namespace hb::shared::direction;
using namespace hb::shared::net;

using namespace hb::shared::item;
using namespace hb::client::config;
using namespace hb::client::sprite_id;

// Ground light-pillar for a tiered drop (Item Tiers spec §11).
//
// Venue pick: a procedural primitive column drawn from tile data on every frame,
// not a new EffectType. A ground item persists until someone takes it, while the
// effect list is built for transient animations — an effect would need lifetime
// tracking against tile state plus new art, and would drift out of sync whenever
// a tile changed off-screen. Drawn before the item sprite, so the item sits in
// front of its own light. Additive, so the column reads as light over whatever
// the tile already drew rather than as a painted rectangle.
//
// The pillar runs to the top of the screen. Drawing it here, inside the tile
// pass, is what makes that legible: a pillar painted at this row covers the rows
// already drawn above it (the ones behind it) and is painted over by the rows
// drawn after (the ones in front), so a shaft occludes and is occluded correctly
// with no separate pass or sort. Nothing clips it — the client sets no clip area
// — and the HUD draws after the world, so it passes under the UI.
//
// Built from gradient quads rather than stacked rectangles: the GPU interpolates
// each quad's corner colors, which buys a per-pixel falloff both along the pillar
// and across it. The alpha-zero outer edges are what stop it reading as a pixel
// bar — a filled rect has a hard 1px side at logical resolution, and the back
// buffer blits 1:1 in windowed mode, so that edge reaches the screen as authored.
namespace {

// Additive light desaturates toward white — `dst + src` raises every channel, so
// a color whose channels are all high adds brightness rather than hue. That is
// exactly why Legendary's gold (blue = 16) keeps its color while Epic's
// 150/160/225 blue reads as a washed-out white. Drop most of the shared white
// component and renormalize to full brightness, so the pillar adds *color*
// wherever the tier has any to add. An achromatic tier — Common's pure white —
// has no chroma to extract and passes through unchanged, which is what §11 asks
// of it anyway. The tier's own color is untouched: this is the light the beam
// emits, not the color the name renders in.
hb::shared::render::Color beam_light_color(const hb::shared::render::Color& tier)
{
	constexpr float white_strip = 0.8f;
	const float low = static_cast<float>(std::min({ tier.r, tier.g, tier.b }));
	const float high = static_cast<float>(std::max({ tier.r, tier.g, tier.b }));
	const float white = low * white_strip;
	const float peak = high - white;
	if (peak <= 1.0f) return tier;

	const float gain = 255.0f / peak;
	const auto lift = [white, gain](uint8_t channel)
	{
		return static_cast<uint8_t>(std::clamp((channel - white) * gain, 0.0f, 255.0f));
	};
	return { lift(tier.r), lift(tier.g), lift(tier.b) };
}

} // namespace

void Screen_OnGame::draw_tier_beam(int center_x, int base_y, uint8_t tier, uint32_t time) const
{
	using hb::shared::render::BlendMode;
	using hb::shared::render::Color;
	using hb::shared::render::GradientCorner;

	const tier_presentation_entry* row = item_name_formatter::get().tier_row(tier);
	// An unreplicated tier row demotes to no beam, exactly as it demotes to a
	// plain item name — one rule for missing presentation data everywhere.
	if (row == nullptr || row->name.empty()) return;

	// Column strength per tier: Common is a faint neutral marker (its color is
	// white), each tier above it brighter. Indexed tier-1.
	static constexpr uint8_t tier_alpha[hb::shared::item::tier_count] = { 55, 110, 150, 195 };
	constexpr float root_height = 48.0f;
	constexpr uint32_t pulse_period_ms = 2400;
	constexpr float pool_half_width = 17.0f;
	constexpr float pool_half_height = 3.0f;

	// Slow breathing pulse — keeps the column alive without animating art.
	const float phase = static_cast<float>(time % pulse_period_ms) / pulse_period_ms;
	const float pulse = 1.0f + 0.15f * std::sin(phase * 2.0f * 3.14159265f);
	const float alpha_base = tier_alpha[tier - 1] * pulse;
	const Color light = beam_light_color(row->color);

	// The color on the pillar's center line at a given fraction of full strength,
	// and the transparent color its outer edges fade to.
	const auto core = [&light, alpha_base](float strength)
	{
		return Color{ light.r, light.g, light.b,
			static_cast<uint8_t>(std::clamp(alpha_base * strength, 0.0f, 255.0f)) };
	};
	const Color edge{ light.r, light.g, light.b, 0 };

	// One quad pair per section, centre line at full strength fading to nothing at
	// both outer edges. `half_width` is the reach of the glow, not a hard border.
	const auto draw_section = [this, center_x, &core, &edge]
		(float top_y, float top_half_width, float top_strength,
		 float bottom_y, float bottom_half_width, float bottom_strength)
	{
		if (top_y >= bottom_y) return;   // nothing left of this section on screen

		const float cx = static_cast<float>(center_x);
		const Color top = core(top_strength);
		const Color bottom = core(bottom_strength);

		const GradientCorner left[4] = {
			{ cx - top_half_width,    top_y,    edge   },
			{ cx,                     top_y,    top    },
			{ cx,                     bottom_y, bottom },
			{ cx - bottom_half_width, bottom_y, edge   },
		};
		const GradientCorner right[4] = {
			{ cx,                     top_y,    top    },
			{ cx + top_half_width,    top_y,    edge   },
			{ cx + bottom_half_width, bottom_y, edge   },
			{ cx,                     bottom_y, bottom },
		};
		m_game->m_Renderer->draw_gradient_quad(left, BlendMode::Additive);
		m_game->m_Renderer->draw_gradient_quad(right, BlendMode::Additive);
	};

	const float base = static_cast<float>(base_y);

	// Ground pool: a soft wide glow that roots the pillar to the tile. Its flat
	// top and bottom sit under the item sprite, so only the faded sides show.
	draw_section(base - pool_half_height, pool_half_width, 0.55f,
	             base + pool_half_height, pool_half_width, 0.55f);

	// Root — bright and wide at the tile, tapering as it rises. Split in two so
	// the falloff curves instead of running straight to the shaft's strength.
	draw_section(base - root_height * 0.5f, 6.0f, 0.80f, base,                    9.0f, 1.00f);
	draw_section(base - root_height,        4.0f, 0.55f, base - root_height * 0.5f, 6.0f, 0.80f);

	// Shaft — the rest of the way to the top of the screen, dissipating as it
	// goes. One quad pair covers it: the gradient does what seven bands did.
	draw_section(0.0f, 3.0f, 0.16f, base - root_height, 4.0f, 0.55f);
}

// --- DrawObject dispatcher functions ---

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_stop(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_stop(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_stop(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_move(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_move(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_move(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_run(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_run(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_run(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_attack(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_attack(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_attack(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_attack_move(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_attack_move(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_attack_move(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_magic(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_magic(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_magic(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_get_item(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_get_item(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_get_item(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_damage(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_damage(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_damage(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_damage_move(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_damage_move(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_damage_move(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_dying(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_dying(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_dying(indexX, indexY, sX, sY, trans, time);
}

hb::shared::sprite::BoundRect Screen_OnGame::draw_object_on_dead(int indexX, int indexY, int sX, int sY, bool trans, uint32_t time)
{
	if (m_game->m_entity_state.is_player())
		return m_player_renderer.draw_dead(indexX, indexY, sX, sY, trans, time);
	else
		return m_npc_renderer.draw_dead(indexX, indexY, sX, sY, trans, time);
}

// --- draw_objects coordinator ---

void Screen_OnGame::draw_objects(short pivot_x, short pivot_y, short div_x, short div_y, short mod_x, short mod_y, short mouse_x, short mouse_y)
{
	int ix, iy, indexX, indexY, dX, dY, dvalue;
	char item_color;
	bool is_player_drawed = false;
	bool ret = false;
	short obj_spr, obj_spr_frame, dynamic_object, dynamic_object_frame;
	char dynamic_object_data1, dynamic_object_data2, dynamic_object_data3, dynamic_object_data4;
	// Xmas tree bulb positions — static arrays shared across all trees (same bulb pattern each frame).
	// 100 entries matches the loop count at line ~725; indexed by j in [0..99].
	static int ix1[100];
	static int iy2[100];
	static int xmas_tree_bulb_delay = 76;
	int idelay = 75;

	// Item's desc on floor
	int item_selectedx, item_selectedy;
	short item_id;
	short item_selected_id = 0;
	hb::shared::item::item_instance_data item_selected;

	int res_x = LOGICAL_MAX_X();
	int res_y = LOGICAL_MAX_Y();
	int res_msy = LOGICAL_HEIGHT() - 49;

	if (div_y < 0 || div_x < 0) return;

	// initialize Picking system for this frame
	CursorTarget::begin_frame();

	uint32_t time = m_game->m_cur_time;

	// Pre-calculate map data bounds for efficient boundary checking
	const short mapMinX = m_game->m_map_data->m_pivot_x;
	const short mapMaxX = m_game->m_map_data->m_pivot_x + MapDataSizeX;
	const short mapMinY = m_game->m_map_data->m_pivot_y;
	const short mapMaxY = m_game->m_map_data->m_pivot_y + MapDataSizeY;

	// Tile-based loop bounds (much cleaner than pixel-based)
	// Buffer: 7 tiles around visible area for smooth object sliding
	// Extra 2 tiles on bottom for layering/depth sorting of tall objects
	constexpr int TILE_SIZE = 32;
	constexpr int BUFFER_TILES = 7;
	constexpr int EXTRA_BOTTOM_TILES = 2;  // For depth sorting of tall objects

	const int visibleTilesX = (res_x / TILE_SIZE) + 1;  // ~20 tiles
	const int visibleTilesY = (res_y / TILE_SIZE) + 1;  // ~15 tiles

	const int startTileX = -BUFFER_TILES;
	const int endTileX = visibleTilesX + BUFFER_TILES;
	const int startTileY = -BUFFER_TILES;
	const int endTileY = visibleTilesY + BUFFER_TILES + EXTRA_BOTTOM_TILES;

	// Visibility bounds in pixels (for culling non-visible tiles from detailed processing)
	const int visMinX = -mod_x;
	const int visMaxX = res_x + 16;
	const int visMinY = -mod_y;
	const int visMaxY = res_y + 32 + 16;

	// Loop over tiles, calculate pixel positions when needed
	for (int tileY = startTileY; tileY <= endTileY; tileY++)
	{
		indexY = div_y + pivot_y + tileY;
		iy = tileY * TILE_SIZE - mod_y;

		for (int tileX = startTileX; tileX <= endTileX; tileX++)
		{
			indexX = div_x + pivot_x + tileX;
			ix = tileX * TILE_SIZE - mod_x;

			dynamic_object = 0;
			ret = false;
			if ((ix >= visMinX) && (ix <= visMaxX) && (iy >= visMinY) && (iy <= visMaxY))
			{
				m_game->m_entity_state.m_object_id = m_game->m_entity_state.m_owner_type = 0; m_game->m_entity_state.m_status.clear();
				m_game->m_entity_state.m_appearance.clear();
				m_game->m_entity_state.m_dir = direction{}; m_game->m_entity_state.m_frame = 0;
				m_game->m_entity_state.m_effect_type = m_game->m_entity_state.m_effect_frame = m_game->m_entity_state.m_chat_index = 0;
				m_game->m_entity_state.m_name.fill('\0');
				if ((indexX < mapMinX) || (indexX > mapMaxX) ||
					(indexY < mapMinY) || (indexY > mapMaxY))
				{
					item_id = 0;
					ret = false;
					item_color = 0;
				}
				else
				{
					m_game->m_entity_state.m_data_x = dX = indexX - mapMinX;
					m_game->m_entity_state.m_data_y = dY = indexY - mapMinY;
					m_game->m_entity_state.m_object_id = m_game->m_map_data->m_data[dX][dY].m_dead_object_id;
					m_game->m_entity_state.m_owner_type = m_game->m_map_data->m_data[dX][dY].m_dead_owner_type;
					m_game->m_entity_state.m_npc_config_id = m_game->m_map_data->m_data[dX][dY].m_dead_npc_config_id;
					m_game->m_entity_state.m_dir = m_game->m_map_data->m_data[dX][dY].m_dead_dir;
					m_game->m_entity_state.m_appearance = m_game->m_map_data->m_data[dX][dY].m_dead_appearance;
					m_game->m_entity_state.m_frame = m_game->m_map_data->m_data[dX][dY].m_dead_owner_frame;
					m_game->m_entity_state.m_chat_index = m_game->m_map_data->m_data[dX][dY].m_dead_chat_msg;
					m_game->m_entity_state.m_status = m_game->m_map_data->m_data[dX][dY].m_deadStatus;
					std::snprintf(m_game->m_entity_state.m_name.data(), m_game->m_entity_state.m_name.size(), "%s", m_game->m_map_data->m_data[dX][dY].m_dead_owner_name.c_str());
					item_id = m_game->m_map_data->m_data[dX][dY].m_item_id;
					item_color = m_game->m_map_data->m_data[dX][dY].m_item.item_color;
					dynamic_object = m_game->m_map_data->m_data[dX][dY].m_dynamic_object_type;
					dynamic_object_frame = static_cast<short>(m_game->m_map_data->m_data[dX][dY].m_dynamic_object_frame);
					dynamic_object_data1 = m_game->m_map_data->m_data[dX][dY].m_dynamic_object_data_1;
					dynamic_object_data2 = m_game->m_map_data->m_data[dX][dY].m_dynamic_object_data_2;
					dynamic_object_data3 = m_game->m_map_data->m_data[dX][dY].m_dynamic_object_data_3;
					dynamic_object_data4 = m_game->m_map_data->m_data[dX][dY].m_dynamic_object_data_4;

					ret = true;
				}

				if ((ret == true) && (item_id != 0) && m_game->m_item_config_list[item_id] != 0)
				{
					CItem* ground_cfg = m_game->m_item_config_list[item_id].get();
					auto ground_draw = m_game->get_item_draw(ground_cfg->m_display_id, item_atlas::ground, ground_cfg->sprite_is_female());

					// Center ground item sprite on tile, offset by half-tile to align with tile center
					auto rect = ground_draw.sprite->GetFrameRect(ground_draw.frame);
					int cx = ix + (TILE_SIZE - rect.width) / 2 - (TILE_SIZE / 2);
					int cy = iy + (TILE_SIZE - rect.height) / 2 - (TILE_SIZE / 2);

					// Tier beam under the item — the sprites are centered on (ix, iy),
					// so that point is the tile center the column rises from.
					draw_tier_beam(ix, iy, m_game->m_map_data->m_data[dX][dY].m_item.get_tier(), time);

					m_game->draw_item_sprite(ground_draw, cx, cy, item_color, ground_cfg,
						item_draw_state::normal, /*on_ground=*/true);

					if (hb::shared::input::is_shift_down() && mouse_x >= ix - 16 && mouse_y >= iy - 16 && mouse_x <= ix + 16 && mouse_y <= iy + 16) {
						item_selected_id = m_game->m_map_data->m_data[dX][dY].m_item_id;
						item_selected = m_game->m_map_data->m_data[dX][dY].m_item;
						item_selectedx = ix;
						item_selectedy = iy;
					}

					// Test ground item with Picking system
					CursorTarget::test_ground_item(ix, iy, res_msy);
				}

				if ((ret == true) && (m_game->m_entity_state.m_object_id != 0))
				{
					auto dead_bounds = draw_object_on_dead(indexX, indexY, ix, iy, false, time);

					// Enable hover name for player corpses
					if (m_game->m_entity_state.is_player())
					{
						TargetObjectInfo info = {};
						info.m_object_id = m_game->m_entity_state.m_object_id;
						info.m_map_x = indexX;
						info.m_map_y = indexY;
						info.m_screen_x = ix;
						info.m_screen_y = iy;
						info.m_data_x = m_game->m_entity_state.m_data_x;
						info.m_data_y = m_game->m_entity_state.m_data_y;
						info.m_owner_type = m_game->m_entity_state.m_owner_type;
						info.m_npc_config_id = m_game->m_entity_state.m_npc_config_id;
						info.m_action = ObjectDead;
						info.m_direction = m_game->m_entity_state.m_dir;
						info.m_frame = m_game->m_entity_state.m_frame;
						info.m_name = m_game->m_entity_state.m_name.data();
						info.m_appearance = m_game->m_entity_state.m_appearance;
						info.m_status = m_game->m_entity_state.m_status;
						info.m_type = FocusedObjectType::DeadBody;
						CursorTarget::test_object(dead_bounds, info, iy, res_msy);
					}
				}

				m_game->m_entity_state.m_object_id = m_game->m_entity_state.m_owner_type = 0; m_game->m_entity_state.m_status.clear();
				m_game->m_entity_state.m_appearance.clear();
				m_game->m_entity_state.m_frame = 0; m_game->m_entity_state.m_dir = direction{};
				m_game->m_entity_state.m_effect_type = m_game->m_entity_state.m_effect_frame = m_game->m_entity_state.m_chat_index = 0;
				m_game->m_entity_state.m_name.fill('\0');

				if ((indexX < mapMinX) || (indexX > mapMaxX) ||
					(indexY < mapMinY) || (indexY > mapMaxY))
				{
					item_id = 0;
					ret = false;
				}
				else
				{
					m_game->m_entity_state.m_data_x = dX = indexX - mapMinX;
					m_game->m_entity_state.m_data_y = dY = indexY - mapMinY;
					m_game->m_entity_state.m_object_id = m_game->m_map_data->m_data[dX][dY].m_object_id;
					m_game->m_entity_state.m_owner_type = m_game->m_map_data->m_data[dX][dY].m_owner_type;
					m_game->m_entity_state.m_npc_config_id = m_game->m_map_data->m_data[dX][dY].m_npc_config_id;
					m_game->m_entity_state.m_action = m_game->m_map_data->m_data[dX][dY].m_animation.m_action;
					m_game->m_entity_state.m_status = m_game->m_map_data->m_data[dX][dY].m_status;
					m_game->m_entity_state.m_dir = m_game->m_map_data->m_data[dX][dY].m_animation.m_dir;
					m_game->m_entity_state.m_appearance = m_game->m_map_data->m_data[dX][dY].m_appearance;
					m_game->m_entity_state.m_frame = m_game->m_map_data->m_data[dX][dY].m_animation.m_current_frame;
					m_game->m_entity_state.m_chat_index = m_game->m_map_data->m_data[dX][dY].m_chat_msg;
					m_game->m_entity_state.m_effect_type = m_game->m_map_data->m_data[dX][dY].m_effect_type;
					m_game->m_entity_state.m_effect_frame = m_game->m_map_data->m_data[dX][dY].m_effect_frame;

					std::snprintf(m_game->m_entity_state.m_name.data(), m_game->m_entity_state.m_name.size(), "%s", m_game->m_map_data->m_data[dX][dY].m_owner_name.c_str());
					ret = true;

					if (m_ilusion_owner_h != 0)
					{
						if ((strcmp(m_game->m_entity_state.m_name.data(), m_game->m_player->m_player_name.c_str()) != 0) && (!hb::shared::owner::is_npc(m_game->m_entity_state.m_owner_type)))
						{
							m_game->m_entity_state.m_owner_type = m_ilusion_owner_type;
							m_game->m_entity_state.m_status = m_game->m_player->m_illusionStatus;
							m_game->m_entity_state.m_appearance = m_game->m_player->m_illusionAppearance;
						}
					}
				}

				if ((ret == true) && (m_game->m_entity_state.m_name[0] != '\0'))
				{
					m_game->m_entity_state.m_move_offset_x = 0;
					m_game->m_entity_state.m_move_offset_y = 0;
					hb::shared::sprite::BoundRect bounds = {0, -1, 0, 0};
					switch (m_game->m_entity_state.m_action) {
					case Type::stop:
						bounds = draw_object_on_stop(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Move:
						bounds = draw_object_on_move(indexX, indexY, ix, iy, false, time);
						break;

					case Type::DamageMove:
						bounds = draw_object_on_damage_move(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Run:
						bounds = draw_object_on_run(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Attack:
						bounds = draw_object_on_attack(indexX, indexY, ix, iy, false, time);
						break;

					case Type::AttackMove:
						bounds = draw_object_on_attack_move(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Magic:
						bounds = draw_object_on_magic(indexX, indexY, ix, iy, false, time);
						break;

					case Type::GetItem:
						bounds = draw_object_on_get_item(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Damage:
						bounds = draw_object_on_damage(indexX, indexY, ix, iy, false, time);
						break;

					case Type::Dying:
						bounds = draw_object_on_dying(indexX, indexY, ix, iy, false, time);
						break;
					}

					// Build picking info for living object
					TargetObjectInfo info = {};
					info.m_object_id = m_game->m_entity_state.m_object_id;
					info.m_map_x = indexX;
					info.m_map_y = indexY;
					info.m_screen_x = ix;
					info.m_screen_y = iy;
					info.m_data_x = m_game->m_entity_state.m_data_x;
					info.m_data_y = m_game->m_entity_state.m_data_y;
					info.m_owner_type = m_game->m_entity_state.m_owner_type;
					info.m_npc_config_id = m_game->m_entity_state.m_npc_config_id;
					info.m_action = m_game->m_entity_state.m_action;
					info.m_direction = m_game->m_entity_state.m_dir;
					info.m_frame = m_game->m_entity_state.m_frame;
					info.m_name = m_game->m_entity_state.m_name.data();
					info.m_appearance = m_game->m_entity_state.m_appearance;
					info.m_status = m_game->m_entity_state.m_status;
					// Determine type based on owner type
					info.m_type = (m_game->m_entity_state.is_player()) ?
						FocusedObjectType::Player : FocusedObjectType::NPC;
					CursorTarget::test_object(bounds, info, iy, res_msy);

					if (m_game->m_entity_state.m_object_id == m_game->m_player->m_player_object_id)
					{
						// Camera is now updated in on_render() before drawing, so we don't need to update it here
						// This ensures viewport and entity position use the same motion offset
						m_game->m_player_rect = m_game->m_body_rect;
						is_player_drawed = true;
					}
				}
			}

			// CLEROTH - Object sprites on tiles
			// Bounds check for tile array access (752x752)
			if (indexX >= 0 && indexX < 752 && indexY >= 0 && indexY < 752)
			{
				obj_spr = m_game->m_map_data->m_tile[indexX][indexY].m_sObjectSprite;
				obj_spr_frame = m_game->m_map_data->m_tile[indexX][indexY].m_sObjectSpriteFrame;
			}
			else
			{
				obj_spr = 0;
				obj_spr_frame = 0;
			}

			if (obj_spr != 0)
			{
				if ((obj_spr < 100) || (obj_spr >= 200))
				{
					switch (obj_spr) {
					case 200:
					case 223:
						m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame, hb::shared::sprite::DrawParams::shadow());
						break;

					case 224:
						switch (obj_spr_frame) {
						case hb::shared::owner::Tom:
						case hb::shared::owner::Dummy:
						case hb::shared::owner::EnergySphere:
						case hb::shared::owner::ArrowGuardTower:
						case hb::shared::owner::CannonGuardTower:
						case hb::shared::owner::ManaCollector:
							break;
						default:
							m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame, hb::shared::sprite::DrawParams::shadow());
							break;
						}
					}
					if (config_manager::get().get_detail_level() == 0) // Special Grass & Flowers
					{
						if ((obj_spr != 6) && (obj_spr != 9))
							m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame);
					}
					else
					{
						m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame);
					}

					switch (obj_spr) {
					case 223:
						if (obj_spr_frame == 4)
						{
							if (weather_manager::get().is_night()) //nuit
							{
								// Lamp fixture lights (the actual light sources on the lamp)
								m_game->m_effect_sprites[0]->draw(ix + 2, iy - 147, 1, hb::shared::sprite::DrawParams::additive_colored(255, 230, 180, 0.8f));
								m_game->m_effect_sprites[0]->draw(ix + 16, iy - 94, 1, hb::shared::sprite::DrawParams::additive_colored(255, 230, 180, 0.8f));
								m_game->m_effect_sprites[0]->draw(ix - 19, iy - 126, 1, hb::shared::sprite::DrawParams::additive_colored(255, 230, 180, 0.8f));
							}
						}
						break;

					case 370: // nuit
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 9) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 16 + 30, m_game->m_Camera.get_y() + iy - 16 - 334, 0, 0, 0, 0);
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 11) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 16 + 17, m_game->m_Camera.get_y() + iy - 16 - 300, 0, 0, 0, 0);
						break;

					case 374: // nuit
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 2) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 7, m_game->m_Camera.get_y() + iy - 122, 0, 0, 0, 0);
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 6) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 14, m_game->m_Camera.get_y() + iy - 321, 0, 0, 0, 0);
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 7) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix + 7, m_game->m_Camera.get_y() + iy - 356, 0, 0, 0, 0);
						break;

					case 376: // nuit
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 12) && (weather_manager::get().is_night())) {
							m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 16, m_game->m_Camera.get_y() + iy - 346, 0, 0, 0, 0);
							m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix + 11, m_game->m_Camera.get_y() + iy - 308, 0, 0, 0, 0);
						}
						break;

					case 378: // nuit
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 11) && (weather_manager::get().is_night())) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix, m_game->m_Camera.get_y() + iy - 91, 0, 0, 0, 0);
						break;

					case 382: // nuit
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 9) && (weather_manager::get().is_night())) {
							m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix + 73, m_game->m_Camera.get_y() + iy - 264, 0, 0, 0, 0);
							m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix + 23, m_game->m_Camera.get_y() + iy - 228, 0, 0, 0, 0);
						}
						break;

					case 429:
						if (((time - m_env_effect_time) > 400) && (obj_spr_frame == 2)) m_game->m_effect_manager->add_effect(EffectType::MS_CRUSADE_CASTING, m_game->m_Camera.get_x() + ix - 15, m_game->m_Camera.get_y() + iy - 224, 0, 0, 0, 0);
						break;
					}
				}
				else // sprites 100..199: Trees and tree shadows
				{
					m_game->m_tile_spr[obj_spr]->CalculateBounds(ix - 16, iy - 16, obj_spr_frame);
					if (config_manager::get().get_detail_level() == 0)
					{
						if (obj_spr < 100 + 11) m_game->m_tile_spr[100 + 4]->draw(ix - 16, iy - 16, obj_spr_frame);
						else if (obj_spr < 100 + 23) m_game->m_tile_spr[100 + 9]->draw(ix - 16, iy - 16, obj_spr_frame);
						else if (obj_spr < 100 + 32) m_game->m_tile_spr[100 + 23]->draw(ix - 16, iy - 16, obj_spr_frame);
						else m_game->m_tile_spr[100 + 32]->draw(ix - 16, iy - 16, obj_spr_frame);
					}
					else
					{
						// obj_spr is [100..199] here; obj_spr+50 is the tree shadow sprite [150..249].
						// SpriteCollection returns NullSprite for missing entries, so no crash if shadow is absent.
						if ((is_player_drawed == true) && (m_game->m_tile_spr[obj_spr]->GetBoundRect().top <= m_game->m_player_rect.Top()) && (m_game->m_tile_spr[obj_spr]->GetBoundRect().bottom >= m_game->m_player_rect.Bottom()) &&
							(config_manager::get().get_detail_level() >= 2) && (m_game->m_tile_spr[obj_spr]->GetBoundRect().left <= m_game->m_player_rect.Left()) && (m_game->m_tile_spr[obj_spr]->GetBoundRect().right >= m_game->m_player_rect.Right()))
						{
							m_game->m_tile_spr[obj_spr + 50]->draw(ix, iy, obj_spr_frame, hb::shared::sprite::DrawParams::fade());
							m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame, hb::shared::sprite::DrawParams::average());
						}
						else
						{
							// Normal rendering - draw shadow and tree opaque (matches original PutSpriteFast)
							m_game->m_tile_spr[obj_spr + 50]->draw(ix, iy, obj_spr_frame);
							m_game->m_tile_spr[obj_spr]->draw(ix - 16, iy - 16, obj_spr_frame);
						}
						if (m_is_xmas == true)
						{
							if (weather_manager::get().is_night()) // nuit
							{
								if (xmas_tree_bulb_delay < 0 || xmas_tree_bulb_delay > idelay + 1) xmas_tree_bulb_delay = 0;
								if (xmas_tree_bulb_delay > idelay)
								{
									for (int i = 0; i < 100; i++) {
										ix1[i] = 1 * (rand() % 400) - 200;
										iy2[i] = -1 * (rand() % 300);
									}
									xmas_tree_bulb_delay = 0;
								}
								else xmas_tree_bulb_delay++;

								for (int j = 0; j < 100; j++)
								{
									if (m_game->m_tile_spr[obj_spr]->CheckCollision(ix - 16, iy - 16, obj_spr_frame, ix + ix1[j], iy + iy2[j]))
									{
										m_game->m_effect_sprites[66 + (j % 6)]->draw(ix + ix1[j], iy + iy2[j], (xmas_tree_bulb_delay >> 2), hb::shared::sprite::DrawParams::alpha_blend(0.5f));
									}
								}
							}
						}
					}
				}
			}

			// Dynamic Object
			if ((ret == true) && (dynamic_object != 0))
			{
				switch (dynamic_object) {
				case dynamic_object::PCloudBegin:	// 10
					if (dynamic_object_frame >= 0)
						m_game->m_effect_sprites[23]->draw(ix + (rand() % 2), iy + (rand() % 2), dynamic_object_frame, hb::shared::sprite::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case dynamic_object::PCloudLoop:		// 11
					m_game->m_effect_sprites[23]->draw(ix + (rand() % 2), iy + (rand() % 2), dynamic_object_frame + 8, hb::shared::sprite::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case dynamic_object::PCloudEnd:		// 12
					m_game->m_effect_sprites[23]->draw(ix + (rand() % 2), iy + (rand() % 2), dynamic_object_frame + 16, hb::shared::sprite::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case dynamic_object::IceStorm:		// 8
					dvalue = (rand() % 5) * (-1);
					m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams::tinted_alpha(192 + 2 * dvalue, 192 + 2 * dvalue, 192 + 2 * dvalue, 0.7f));
					m_game->m_effect_sprites[13]->draw(ix, iy, dynamic_object_frame, hb::shared::sprite::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case dynamic_object::Fire:			// 1
				case dynamic_object::Fire3:			// 14
					switch (rand() % 3) {
					case 0: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.25f, 0, 0, 0, false}); break;
					case 1: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.5f, 0, 0, 0, false}); break;
					case 2: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.7f, 0, 0, 0, false}); break;
					}
					m_game->m_effect_sprites[9]->draw(ix, iy, dynamic_object_frame / 3, hb::shared::sprite::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case dynamic_object::Fire2:			// 13
					switch (rand() % 3) {
					case 0: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.25f, 0, 0, 0, false}); break;
					case 1: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.5f, 0, 0, 0, false}); break;
					case 2: m_game->m_effect_sprites[0]->draw(ix, iy, 1, hb::shared::sprite::DrawParams{0.7f, 0, 0, 0, false}); break;
					}
					break;

				case dynamic_object::Fish:			// 2
				{
					direction tmp_d_odir;
					char tmp_d_oframe;
					tmp_d_odir = CMisc::calc_direction(dynamic_object_data1, dynamic_object_data2, dynamic_object_data1 + dynamic_object_data3, dynamic_object_data2 + dynamic_object_data4);
					tmp_d_oframe = ((tmp_d_odir - 1) * 4) + (rand() % 4);
					m_game->m_sprite[ItemDynamicPivotPoint + 0]->draw(ix + dynamic_object_data1, iy + dynamic_object_data2, tmp_d_oframe, hb::shared::sprite::DrawParams::alpha_blend(0.25f));
				}
				break;

				case dynamic_object::Mineral1:		// 4
					if (config_manager::get().get_detail_level() != 0) m_game->m_sprite[ItemDynamicPivotPoint + 1]->draw(ix, iy, 0, hb::shared::sprite::DrawParams::shadow());
					m_game->m_sprite[ItemDynamicPivotPoint + 1]->draw(ix, iy, 0);
					CursorTarget::test_dynamic_object(m_game->m_sprite[ItemDynamicPivotPoint + 1]->GetBoundRect(), indexX, indexY, res_msy);
					break;

				case dynamic_object::Mineral2:		// 5
					if (config_manager::get().get_detail_level() != 0) m_game->m_sprite[ItemDynamicPivotPoint + 1]->draw(ix, iy, 1, hb::shared::sprite::DrawParams::shadow());
					m_game->m_sprite[ItemDynamicPivotPoint + 1]->draw(ix, iy, 1);
					CursorTarget::test_dynamic_object(m_game->m_sprite[ItemDynamicPivotPoint + 1]->GetBoundRect(), indexX, indexY, res_msy);
					break;

				case dynamic_object::Spike:			// 9
					m_game->m_effect_sprites[17]->draw(ix, iy, dynamic_object_frame, hb::shared::sprite::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case dynamic_object::AresdenFlag1:  // 6
					m_game->m_sprite[ItemDynamicPivotPoint + 2]->draw(ix, iy, dynamic_object_frame);
					break;

				case dynamic_object::ElvineFlag1: // 7
					m_game->m_sprite[ItemDynamicPivotPoint + 2]->draw(ix, iy, dynamic_object_frame);
					break;
				}
			}
		}
	}

	if ((time - m_env_effect_time) > 400) m_env_effect_time = time;

	// Finalize Picking system - determines cursor type
	EntityRelationship focusRelationship = CursorTarget::has_focused_object() ? CursorTarget::GetFocusStatus().relationship : EntityRelationship::Neutral;
	CursorTarget::end_frame(focusRelationship, m_point_command_type, m_game->m_player->m_Controller.is_command_available(), m_is_get_pointing_mode);

	// update legacy compatibility variables from Picking system
	m_game->m_mcx = CursorTarget::get_focused_map_x();
	m_game->m_mcy = CursorTarget::get_focused_map_y();
	m_game->m_mc_name = CursorTarget::get_focused_name();

	// draw focused object with highlight (transparency)
	if (CursorTarget::has_focused_object())
	{
		short focusSX, focusSY;
		uint16_t focusObjID;
		short focusOwnerType, focusNpcConfigId;
		char focusAction, focusFrame;
		direction focusDir;
		hb::shared::entity::PlayerAppearance focusAppearance;
		hb::shared::entity::PlayerStatus focusStatus;
		short focusDataX, focusDataY;

		if (CursorTarget::get_focus_highlight_data(focusSX, focusSY, focusObjID, focusOwnerType,
			focusNpcConfigId, focusAction, focusDir, focusFrame, focusAppearance,
			focusStatus, focusDataX, focusDataY))
		{
			// Set up temporary vars for drawing
			m_game->m_entity_state.m_object_id = focusObjID;
			m_game->m_entity_state.m_owner_type = focusOwnerType;
			m_game->m_entity_state.m_npc_config_id = focusNpcConfigId;
			m_game->m_entity_state.m_action = focusAction;
			m_game->m_entity_state.m_frame = focusFrame;
			m_game->m_entity_state.m_dir = focusDir;
			m_game->m_entity_state.m_appearance = focusAppearance;
			m_game->m_entity_state.m_status = focusStatus;
			m_game->m_entity_state.m_data_x = focusDataX;
			m_game->m_entity_state.m_data_y = focusDataY;
			m_game->m_entity_state.m_name.fill('\0');
			std::snprintf(m_game->m_entity_state.m_name.data(), m_game->m_entity_state.m_name.size(), "%s", CursorTarget::get_focused_name());

			if ((focusAction != ObjectDead) && (focusFrame < 0)) {
				// Skip drawing invalid frame
			} else {
				switch (focusAction) {
				case Type::stop:
					draw_object_on_stop(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;
				case Type::Move:
					switch (focusOwnerType) {
					case 1:
					case 2:
					case 3: // Human M
					case 4:
					case 5:
					case 6: // Human F

					case hb::shared::owner::Troll: // Troll.
					case hb::shared::owner::Ogre: // Ogre
					case hb::shared::owner::Liche: // Liche
					case hb::shared::owner::Demon: // DD
					case hb::shared::owner::Unicorn: // Uni
					case hb::shared::owner::WereWolf: // WW
					case hb::shared::owner::LightWarBeetle: // LWB
					case hb::shared::owner::GodsHandKnight: // GHK
					case hb::shared::owner::GodsHandKnightCK: // GHKABS
					case hb::shared::owner::TempleKnight: // TK
					case hb::shared::owner::BattleGolem: // BG
					case hb::shared::owner::Stalker: // SK
					case hb::shared::owner::HellClaw: // HC
					case hb::shared::owner::TigerWorm: // TW
					case hb::shared::owner::Catapult: // CP
					case hb::shared::owner::Gargoyle: // GG
					case hb::shared::owner::Beholder: // BB
					case hb::shared::owner::DarkElf: // DE
					case hb::shared::owner::Bunny: // Rabbit
					case hb::shared::owner::Cat: // Cat
					case hb::shared::owner::GiantFrog: // Frog
					case hb::shared::owner::MountainGiant: // MG
					case hb::shared::owner::Ettin: // Ettin
					case hb::shared::owner::CannibalPlant: // Plant
					case hb::shared::owner::Rudolph: // Rudolph
					case hb::shared::owner::DireBoar: // DireBoar
					case hb::shared::owner::Frost: // Frost
					case hb::shared::owner::IceGolem: // Ice-Golem
					case hb::shared::owner::Wyvern: // Wyvern
					case hb::shared::owner::Dragon: // Dragon..........Ajouts par Snoopy
					case hb::shared::owner::Centaur: // Centaur
					case hb::shared::owner::ClawTurtle: // ClawTurtle
					case hb::shared::owner::FireWyvern: // FireWyvern
					case hb::shared::owner::GiantCrayfish: // GiantCrayfish
					case hb::shared::owner::GiLizard: // Gi Lizard
					case hb::shared::owner::GiTree: // Gi Tree
					case hb::shared::owner::MasterOrc: // Master Orc
					case hb::shared::owner::Minaus: // Minaus
					case hb::shared::owner::Nizie: // Nizie
					case hb::shared::owner::Tentocle: // Tentocle
					case hb::shared::owner::Abaddon: // Abaddon
					case hb::shared::owner::Sorceress: // Sorceress
					case hb::shared::owner::ATK: // ATK
					case hb::shared::owner::MasterElf: // MasterElf
					case hb::shared::owner::DSK: // DSK
					case hb::shared::owner::HBT: // HBT
					case hb::shared::owner::CT: // CT
					case hb::shared::owner::Barbarian: // Barbarian
					case hb::shared::owner::AGC: // AGC
					case hb::shared::owner::Gate: // Gate
						break;

					default: // 10..27
						m_game->m_entity_state.m_frame = m_game->m_entity_state.m_frame * 2;
						break;
					}

					draw_object_on_move(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::DamageMove:
					draw_object_on_damage_move(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::Run:
					draw_object_on_run(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::Attack:
					draw_object_on_attack(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::AttackMove:
					draw_object_on_attack_move(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::Magic:
					draw_object_on_magic(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::Damage:
					draw_object_on_damage(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case Type::Dying:
					draw_object_on_dying(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;

				case ObjectDead:
					draw_object_on_dead(m_game->m_mcx, m_game->m_mcy, focusSX, focusSY, true, time);
					break;
				}
			}
		}
	}

	// Shift-hover on a pile on the floor. The tile carries the item's whole
	// instance POD, not just its id, so this can be the same tooltip the pack
	// shows rather than a bare name — nothing new has to come over the wire.
	if (item_selected_id != 0) {
		if (CItem* cfg = m_game->get_item_config(item_selected_id))
		{
			item_tooltip tooltip;
			build_item_tooltip(tooltip, item_selected_id, cfg, item_selected, /*in_inventory=*/false);
			tooltip.draw(mouse_x, mouse_y + 25);
		}
	}
}

void Screen_OnGame::draw_background(short div_x, short mod_x, short div_y, short mod_y)
{
	if (div_x < 0 || div_y < 0) return;

	// Tile-based loop constants
	constexpr int TILE_SIZE = 32;
	const int visibleTilesX = (LOGICAL_WIDTH() / TILE_SIZE) + 2;   // +2 for partial tiles on edges
	const int visibleTilesY = (LOGICAL_HEIGHT() / TILE_SIZE) + 2;

	// Map pivot for tile array access
	const short pivotX = m_game->m_map_data->m_pivot_x;
	const short pivotY = m_game->m_map_data->m_pivot_y;

	// draw tiles directly to backbuffer (no caching)
	for (int tileY = 0; tileY < visibleTilesY; tileY++)
	{
		int indexY = div_y + pivotY + tileY;
		int iy = tileY * TILE_SIZE - mod_y;

		for (int tileX = 0; tileX < visibleTilesX; tileX++)
		{
			int indexX = div_x + pivotX + tileX;
			int ix = tileX * TILE_SIZE - mod_x;

			// Bounds check for tile array (752x752)
			if (indexX >= 0 && indexX < 752 && indexY >= 0 && indexY < 752)
			{
				short spr = m_game->m_map_data->m_tile[indexX][indexY].m_sTileSprite;
				short spr_frame = m_game->m_map_data->m_tile[indexX][indexY].m_sTileSpriteFrame;
				m_game->m_tile_spr[spr]->draw(ix - 16, iy - 16, spr_frame, hb::shared::sprite::DrawParams::no_color_key());
			}
		}
	}

	if (m_is_crusade_mode)
	{
		if (m_game->m_player->m_construct_loc_x != -1) m_game->draw_new_dialog_box(InterfaceNdCrusade, m_game->m_player->m_construct_loc_x * 32 - m_game->m_Camera.get_x(), m_game->m_player->m_construct_loc_y * 32 - m_game->m_Camera.get_y(), 41);
		if (teleport_manager::get().get_loc_x() != -1) m_game->draw_new_dialog_box(InterfaceNdCrusade, teleport_manager::get().get_loc_x() * 32 - m_game->m_Camera.get_x(), teleport_manager::get().get_loc_y() * 32 - m_game->m_Camera.get_y(), 42);
	}
}

void Screen_OnGame::draw_top_msg()
{
	if (m_top_msg.size() == 0) return;
	m_game->m_Renderer->draw_rect_filled(0, 0, LOGICAL_MAX_X(), 30, hb::shared::render::Color::Black(128));

	if ((((GameClock::get_time_ms() - m_top_msg_time) / 250) % 2) == 0)
		hb::shared::text::draw_text_aligned(GameFont::Default, 0, 10, LOGICAL_MAX_X(), 15, m_top_msg.c_str(), hb::shared::text::TextStyle::from_color(GameColors::UITopMsgYellow), hb::shared::text::Align::TopCenter);

	if (GameClock::get_time_ms() > (m_top_msg_last_sec * 1000 + m_top_msg_time)) {
		m_top_msg.clear();
	}
}

// -----------------------------------------------------------------------
// Phase 5b: Methods moved from CGame to Screen_OnGame
// -----------------------------------------------------------------------

void Screen_OnGame::draw_character_body(CGame& game, short sX, short sY, short type)
{
	if (type <= 3)
	{
		game.m_sprite[ItemEquipPivotPoint + 0]->draw(sX, sY, type - 1);
		const auto& hcM = game.m_color_palette[game.m_entity_state.m_appearance.hair_color];
		game.m_sprite[ItemEquipPivotPoint + 18]->draw(sX, sY, game.m_entity_state.m_appearance.hair_style, hb::shared::sprite::DrawParams::tint(hcM.r, hcM.g, hcM.b));

		game.m_sprite[ItemEquipPivotPoint + 19]->draw(sX, sY, game.m_entity_state.m_appearance.underwear_type);
	}
	else
	{
		game.m_sprite[ItemEquipPivotPoint + 40]->draw(sX, sY, type - 4);
		const auto& hcF = game.m_color_palette[game.m_entity_state.m_appearance.hair_color];
		game.m_sprite[ItemEquipPivotPoint + 18 + 40]->draw(sX, sY, game.m_entity_state.m_appearance.hair_style, hb::shared::sprite::DrawParams::tint(hcF.r, hcF.g, hcF.b));
		game.m_sprite[ItemEquipPivotPoint + 19 + 40]->draw(sX, sY, game.m_entity_state.m_appearance.underwear_type);
	}
}

void Screen_OnGame::draw_object_foe(int ix, int iy, int frame)
{
	if (IsHostile(m_game->m_entity_state.m_status.relationship)) // red crusade circle
	{
		if (frame <= 4) m_game->m_effect_sprites[38]->draw(ix, iy, frame, hb::shared::sprite::DrawParams::alpha_blend(0.5f));
	}
}

void Screen_OnGame::draw_npc_name(short screen_x, short screen_y, short owner_type, const hb::shared::entity::PlayerStatus& status, short npc_config_id)
{
	std::string text, text2;

	auto npcName = [&]() -> const char* {
		if (npc_config_id >= 0)
			return m_game->get_npc_config_name_by_id(npc_config_id);
		return "Unknown";
		};

	// Crop subtypes override the base "Crop" name from config
	if (owner_type == hb::shared::owner::Crops) {
		static const char* cropNames[] = {
			"Crop", "WaterMelon", "Pumpkin", "Garlic", "Barley", "Carrot",
			"Radish", "Corn", "Chinese Bell Flower", "Melone", "Tomato",
			"Grapes", "Blue Grape", "Mushroom", "Ginseng"
		};
		int sub = m_game->m_entity_state.m_appearance.sub_type;
		if (sub >= 1 && sub <= 14)
			text = cropNames[sub];
		else
			text = npcName();
	}
	// Crusade structure kit suffix
	else if ((owner_type == hb::shared::owner::ArrowGuardTower || owner_type == hb::shared::owner::CannonGuardTower ||
		owner_type == hb::shared::owner::ManaCollector || owner_type == hb::shared::owner::Detector) &&
		m_game->m_entity_state.m_appearance.HasNpcSpecialState()) {
		text = std::format("{} Kit", npcName());
	}
	else {
		text = npcName();
	}
	if (status.berserk) text += DRAW_OBJECT_NAME50;//" Berserk"
	if (status.frozen) text += DRAW_OBJECT_NAME51;//" Frozen"
	hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y, text.c_str(), hb::shared::text::TextStyle::with_shadow(GameColors::UIWhite));
	if (m_is_observer_mode == true) hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14, text.c_str(), hb::shared::text::TextStyle::with_shadow(GameColors::NeutralNamePlate));
	else if (m_game->m_player->m_is_confusion || (m_ilusion_owner_h != 0))
	{
		text = DRAW_OBJECT_NAME87;//"(Unknown)"
		hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14, text.c_str(), hb::shared::text::TextStyle::with_shadow(GameColors::UIDisabled)); // v2.171
	}
	else
	{
		if (IsHostile(status.relationship))
			hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14, DRAW_OBJECT_NAME90, hb::shared::text::TextStyle::with_shadow(GameColors::UIRed)); // "(Enemy)"
		else if (IsFriendly(status.relationship))
			hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14, DRAW_OBJECT_NAME89, hb::shared::text::TextStyle::with_shadow(GameColors::FriendlyNamePlate)); // "(Friendly)"
		else
			hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14, DRAW_OBJECT_NAME88, hb::shared::text::TextStyle::with_shadow(GameColors::NeutralNamePlate)); // "(Neutral)"
	}
	switch (status.angel_percent) {
	case 0: break;
	case 1: text2 = DRAW_OBJECT_NAME52; break;//"Clairvoyant"
	case 2: text2 = DRAW_OBJECT_NAME53; break;//"Destruction of Magic Protection"
	case 3: text2 = DRAW_OBJECT_NAME54; break;//"Anti-Physical Damage"
	case 4: text2 = DRAW_OBJECT_NAME55; break;//"Anti-Magic Damage"
	case 5: text2 = DRAW_OBJECT_NAME56; break;//"Poisonous"
	case 6: text2 = DRAW_OBJECT_NAME57; break;//"Critical Poisonous"
	case 7: text2 = DRAW_OBJECT_NAME58; break;//"Explosive"
	case 8: text2 = DRAW_OBJECT_NAME59; break;//"Critical Explosive"
	}
	hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 28, text2.c_str(), hb::shared::text::TextStyle::with_shadow(GameColors::MonsterStatusEffect));

	// centu: no muestra la barra de hp de algunos npc
	switch (owner_type) {
	case hb::shared::owner::ShopKeeper:
	case hb::shared::owner::Gandalf:
	case hb::shared::owner::Howard:
	case hb::shared::owner::Tom:
	case hb::shared::owner::William:
	case hb::shared::owner::Kennedy:
	case hb::shared::owner::ManaStone:
	case hb::shared::owner::Bunny:
	case hb::shared::owner::Cat:
	case hb::shared::owner::McGaffin:
	case hb::shared::owner::Perry:
	case hb::shared::owner::Devlin:
	case hb::shared::owner::Crops:
	{
		switch (m_game->m_entity_state.m_appearance.sub_type) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case hb::shared::owner::Slime:
		case hb::shared::owner::Skeleton:
		case hb::shared::owner::StoneGolem:
		case hb::shared::owner::Cyclops:
		case hb::shared::owner::OrcMage:
		default:
			break;
		}
	}
	case hb::shared::owner::Gail:
		break;
	default:
		break;
	}
}

void Screen_OnGame::draw_object_name(short screen_x, short screen_y, const char* name, const hb::shared::entity::PlayerStatus& status, uint16_t object_id)
{
	std::string text, text2;
	uint8_t red = 0, green = 0, blue = 0;
	int y_offset = 0;
	bool is_pk = false, is_citizen = false, is_aresden = false, is_hunter = false;
	auto relationship = status.relationship;
	if (IsHostile(relationship))
	{
		red = 255; green = 0; blue = 0;
	}
	else if (IsFriendly(relationship))
	{
		red = 30; green = 200; blue = 30;
	}
	else
	{
		red = 50; green = 50; blue = 255;
	}

	if (m_ilusion_owner_h == 0)
	{
		if (m_is_crusade_mode == false) text = name;
		else
		{
			if (!hb::shared::object_id::is_player_id(m_game->m_entity_state.m_object_id)) text = "Barbarian";
			else
			{
				if (relationship == EntityRelationship::Enemy) text = std::format("{}", m_game->m_entity_state.m_object_id);
				else text = name;
			}
		}
		if (m_party_status != 0)
		{
			if (get_dialog_box_manager().get_dialog_as<DialogBox_Party>(DialogBoxId::Party)->is_party_member(name))
				text += BGET_NPC_NAME23; // ", Party Member"
		}
	}
	else text = "?????";

	if (status.berserk) text += DRAW_OBJECT_NAME50;//" Berserk"
	if (status.frozen) text += DRAW_OBJECT_NAME51;//" Frozen"

	hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y, text.c_str(), hb::shared::text::TextStyle::with_shadow(GameColors::UIWhite));

	// The guild line (#124): "(GuildName RankTitle)" under the name, resolved
	// through the lazy cache for everyone — own included, so the §3.1.2
	// gold-name flag has one source (the GuildSelf handler invalidates the
	// own entry to keep it honest; CPlayer state fills the frames before the
	// answer lands). The line itself is formatted once per answer, not per
	// frame — this runs for every visible player. During crusade a hostile's
	// name is anonymized to an object id and the guild line stays suppressed
	// with it — the original's rule, kept because a guild line under a
	// number would undo the anonymization; friendlies and the own line keep
	// theirs. An illusioned viewer sees "?????" and no guild line either
	// (the original leaked it).
	const bool own_line = object_id == m_game->m_player->m_player_object_id;
	if (m_ilusion_owner_h == 0
		&& (own_line || m_is_crusade_mode == false || !IsHostile(relationship)))
	{
		std::string_view guild_line;
		std::string own_fallback;
		bool gold_name = false;
		if (const auto* entry = m_guild_manager.lookup(name, object_id,
				m_game->m_cur_time); entry != nullptr)
		{
			guild_line = entry->overhead_line;
			gold_name = entry->at_max_level != 0;
		}
		else if (own_line && m_game->m_player->in_guild())
		{
			own_fallback = std::format(UI_GUILD_OVERHEAD_LINE,
				m_game->m_player->m_guild_name,
				m_game->m_player->m_guild_rank_title);
			guild_line = own_fallback;
		}
		if (!guild_line.empty())
		{
			const auto guild_color = gold_name
				? hb::shared::render::Color(255, 215, 0)
				: GameColors::InfoGrayLight;
			hb::shared::text::draw_text(GameFont::Default, screen_x,
				screen_y + 14, guild_line.data(),
				hb::shared::text::TextStyle::with_shadow(guild_color));
			y_offset = 14;
		}
	}

	if (own_line)
	{
		if (m_game->m_player->m_pk_count != 0)
		{
			is_pk = true;
			red = 255; green = 0; blue = 0;
		}
		else
		{
			is_pk = false;
			red = 30; green = 200; blue = 30;
		}
		is_citizen = m_game->m_player->m_citizen;
		is_aresden = m_game->m_player->m_aresden;
		is_hunter = m_game->m_player->m_hunter;
	}
	else
	{	// CLEROTH - CRASH BUG ( STATUS )
		is_pk = status.pk;
		is_citizen = status.citizen;
		is_aresden = status.aresden;
		is_hunter = status.hunter;
	}

	if (is_citizen == false)
	{
		text = DRAW_OBJECT_NAME60;// "Traveller"
		red = GameColors::NeutralNamePlate.r;
		green = GameColors::NeutralNamePlate.g;
		blue = GameColors::NeutralNamePlate.b;
	}
	else
	{
		if (is_aresden)
		{
			if (is_hunter == true) text = DEF_MSG_ARECIVIL; // "Aresden Civilian"
			else text = DEF_MSG_ARESOLDIER; // "Aresden Combatant"
		}
		else
		{
			if (is_hunter == true) text = DEF_MSG_ELVCIVIL;// "Elvine Civilian"
			else text = DEF_MSG_ELVSOLDIER;	// "Elvine Combatant"
		}
	}
	if (is_pk == true)
	{
		if (is_citizen == false) text = DEF_MSG_PK;	//"Criminal"
		else
		{
			if (is_aresden) text = DEF_MSG_AREPK;// "Aresden Criminal"
			else text = DEF_MSG_ELVPK;  // "Elvine Criminal"
		}
	}
	hb::shared::text::draw_text(GameFont::Default, screen_x, screen_y + 14 + y_offset, text.c_str(), hb::shared::text::TextStyle::with_shadow(hb::shared::render::Color(red, green, blue)));
}

void Screen_OnGame::draw_angel(int sprite, short sX, short sY, char frame, uint32_t time)
{
	if (m_game->m_entity_state.m_status.invisibility)
	{
		if (m_game->m_entity_state.m_status.angel_str)
			m_game->m_sprite[TutelaryAngelsPivotPoint + sprite]->draw(sX, sY, frame, hb::shared::sprite::DrawParams::alpha_blend(0.5f));  //AngelicPendant(STR)
		else if (m_game->m_entity_state.m_status.angel_dex)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 1) + sprite]->draw(sX, sY, frame, hb::shared::sprite::DrawParams::alpha_blend(0.5f)); //AngelicPendant(DEX)
		else if (m_game->m_entity_state.m_status.angel_int)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 2) + sprite]->draw(sX, sY - 15, frame, hb::shared::sprite::DrawParams::alpha_blend(0.5f));//AngelicPendant(INT)
		else if (m_game->m_entity_state.m_status.angel_mag)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 3) + sprite]->draw(sX, sY - 15, frame, hb::shared::sprite::DrawParams::alpha_blend(0.5f));//AngelicPendant(MAG)
	}
	else
	{
		if (m_game->m_entity_state.m_status.angel_str)
			m_game->m_sprite[TutelaryAngelsPivotPoint + sprite]->draw(sX, sY, frame);  //AngelicPendant(STR)
		else if (m_game->m_entity_state.m_status.angel_dex)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 1) + sprite]->draw(sX, sY, frame); //AngelicPendant(DEX)
		else if (m_game->m_entity_state.m_status.angel_int)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 2) + sprite]->draw(sX, sY - 15, frame);//AngelicPendant(INT)
		else if (m_game->m_entity_state.m_status.angel_mag)
			m_game->m_sprite[TutelaryAngelsPivotPoint + (50 * 3) + sprite]->draw(sX, sY - 15, frame);//AngelicPendant(MAG)
	}
}

void Screen_OnGame::dk_glare(int weapon_color, int16_t weapon_item_id, int* weapon_glare)
{
	if (weapon_color != 9) return;
	if (weapon_item_id <= 0) return;
	if (weapon_item_id == 745) // Dark Knight Templar
	{
		*weapon_glare = 3;
	}
	else if (weapon_item_id == 746) // Dark Mage Dragon Wand
	{
		*weapon_glare = 2;
	}
}

void Screen_OnGame::abaddon_corpse(int sX, int sY)
{
	int ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX + 30, 0, sX + 30, sY - 10, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX + 30, 0, sX + 30, sY - 10, ir + 2, ir, 2);
	weather_manager::get().draw_thunder_effect(sX + 30, 0, sX + 30, sY - 10, ir - 2, ir, 2);
	ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX - 20, 0, sX - 20, sY - 35, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX - 20, 0, sX - 20, sY - 35, ir + 2, ir, 2);
	weather_manager::get().draw_thunder_effect(sX - 20, 0, sX - 20, sY - 35, ir - 2, ir, 2);
	ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX - 10, 0, sX - 10, sY + 30, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX - 10, 0, sX - 10, sY + 30, ir + 2, ir + 2, 2);
	weather_manager::get().draw_thunder_effect(sX - 10, 0, sX - 10, sY + 30, ir - 2, ir + 2, 2);
	ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX + 50, 0, sX + 50, sY + 35, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX + 50, 0, sX + 50, sY + 35, ir + 2, ir + 2, 2);
	weather_manager::get().draw_thunder_effect(sX + 50, 0, sX + 50, sY + 35, ir - 2, ir + 2, 2);
	ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX + 65, 0, sX + 65, sY - 5, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX + 65, 0, sX + 65, sY - 5, ir + 2, ir + 2, 2);
	weather_manager::get().draw_thunder_effect(sX + 65, 0, sX + 65, sY - 5, ir - 2, ir + 2, 2);
	ir = (rand() % 20) - 10;
	weather_manager::get().draw_thunder_effect(sX + 45, 0, sX + 45, sY - 50, ir, ir, 1);
	weather_manager::get().draw_thunder_effect(sX + 45, 0, sX + 45, sY - 50, ir + 2, ir + 2, 2);
	weather_manager::get().draw_thunder_effect(sX + 45, 0, sX + 45, sY - 50, ir - 2, ir + 2, 2);

	for (int x = sX - 50; x <= sX + 100; x += rand() % 35)
	{
		for (int y = sY - 30; y <= sY + 50; y += rand() % 45)
		{
			ir = (rand() % 20) - 10;
			weather_manager::get().draw_thunder_effect(x, 0, x, y, ir, ir, 2);
		}
	}
}

// Full-screen lightning storm on the abaddon map (Notify::AbaddonThunder,
// retail notify 0x0BE5): bolt clusters sweep the whole viewport from the
// sky to jittered ground points, re-randomized every frame while the
// latch runs. Thinner than the original ThunderEffectAbaddonMap's 32x6
// sweep — each bolt costs ~56 unbatched draw_line calls here, so ~10
// clusters of 3 keeps a storm frame under the abaddon_corpse effect.
void Screen_OnGame::draw_abaddon_storm()
{
	if (m_game->m_cur_time >= m_abaddon_storm_until) return;

	for (int x = 20; x < LOGICAL_WIDTH(); x += 60 + (rand() % 40))
	{
		int ir = (rand() % 10) - 5;
		int ground_y = LOGICAL_HEIGHT() - 35 - (rand() % 15);
		weather_manager::get().draw_thunder_effect(x, 0, x, ground_y, ir, ir, 2);
		weather_manager::get().draw_thunder_effect(x, 0, x, ground_y, ir + 2, ir + 3, 2);
		weather_manager::get().draw_thunder_effect(x, 0, x, ground_y, ir - 1, ir - 4, 2);
	}
}

