#include "MagicCastingSystem.h"
#include "CombatSummary.h"
#include "Game.h"
#include "PacketSendHelpers.h"

#include "lan_eng.h"
#include "Screen_OnGame.h"

using namespace hb::shared::net;
using namespace hb::shared::action;

magic_casting_system& magic_casting_system::get()
{
	static magic_casting_system instance;
	return instance;
}

void magic_casting_system::set_game(CGame* game)
{
	m_game = game;
}

int magic_casting_system::get_mana_cost(int magic_no)
{
	if (!m_game) return 1;
	if (magic_no < 0 || magic_no >= 100) return 1;
	if (!m_game->m_magic_cfg_list[magic_no]) return 1;

	// Totalled and capped in one place now — the character panel shows the same
	// figure, and two walks of the worn set were free to disagree about it.
	const int mana_save = hb::client::equipped_mana_save(*m_game);

	int mana_cost = m_game->m_magic_cfg_list[magic_no]->m_value_1;
	if (m_game->m_player->m_is_safe_attack_mode) mana_cost = mana_cost * 140 / 100;
	if (mana_save > 0)
	{
		double v1 = static_cast<double>(mana_save);
		double v2 = (double)(v1 / 100.0f);
		double v3 = static_cast<double>(mana_cost);
		v1 = v2 * v3;
		v2 = v3 - v1;
		mana_cost = static_cast<int>(v2);
	}
	if (mana_cost < 1) mana_cost = 1;
	return mana_cost;
}

void magic_casting_system::begin_cast(int magic_no)
{
	if (!m_game) return;
	if (!m_game->ensure_magic_configs_loaded()) return;
	if (magic_no < 0 || magic_no >= 100) return;
	if ((m_game->m_player->m_magic_mastery[magic_no] == 0) || (m_game->m_magic_cfg_list[magic_no] == 0)) return;

	// INT requirement check — covers hotkey/shortcut paths that bypass the dialog click handler
	int int_req = m_game->m_magic_cfg_list[magic_no]->m_value_2;
	int player_int = m_game->m_player->m_int + m_game->m_player->m_angelic_int;
	if (int_req > player_int)
	{
		m_game->add_event_list(DLGBOX_CLICK_MAGIC3, 10);
		return;
	}

	// Casting
	if (m_game->m_player->m_hp <= 0) return;
	if (m_game->on_game()->m_is_get_pointing_mode == true) return;
	if (get_mana_cost(magic_no) > m_game->m_player->m_mp) return;
	if (m_game->is_item_on_hand() == true)
	{
		m_game->add_event_list(DLGBOX_CLICK_MAGIC1, 10);
		return;
	}
	if (m_game->on_game()->m_skill_using_status == true)
	{
		m_game->add_event_list(DLGBOX_CLICK_MAGIC2, 10);
		return;
	}
	if (!m_game->m_player->m_playerAppearance.is_walking)
		m_game->send_game_packet(hb::net::make_common_command(CommonType::ToggleCombatMode, m_game->m_player->m_player_x, m_game->m_player->m_player_y));
	m_game->m_player->m_Controller.set_command(Type::Magic);
	m_game->m_casting_magic_type = magic_no;
	m_game->m_magic_short_cut = magic_no;
	m_game->m_recent_short_cut = magic_no + 100;
	m_game->on_game()->m_point_command_type = magic_no + 100;
	m_game->get_dialog_box_manager().disable_dialog_box(DialogBoxId::Magic);
}
