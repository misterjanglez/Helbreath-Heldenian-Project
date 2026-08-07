#include "WarManager.h"
#include "Game.h"
#include "StatusEffectManager.h"
#include <filesystem>
#include "Item.h"
#include "CombatManager.h"
#include "EntityManager.h"
#include "DynamicObjectManager.h"
#include "DelayEventManager.h"
#include "ItemManager.h"
#include "MagicManager.h"
#include "SkillManager.h"
#include "QuestManager.h"
#include "LootManager.h"
#include "Packet/SharedPackets.h"
#include "ObjectIDRange.h"
#include "Skill.h"
#include "GameConfigSqliteStore.h"
#include "TeleportLoc.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include "StringCompat.h"
#include "TimeUtils.h"

using namespace hb::shared::net;

using hb::log_channel;
using namespace hb::shared::action;
using namespace hb::server::net;
using namespace hb::server::config;
using namespace hb::shared::item;
using namespace hb::shared::direction;
namespace sock = hb::shared::net::socket;
namespace dynamic_object = hb::shared::dynamic_object;
namespace smap = hb::server::map;
namespace sdelay = hb::server::delay_event;
using namespace hb::server::npc;
using namespace hb::server::skill;

extern char G_cTxt[512];
extern char G_cData50000[50000];

void WarManager::crusade_war_starter()
{
	hb::time::local_time SysTime{};
	

	if (m_game->m_is_crusade_mode) return;
	if (m_game->m_is_crusade_war_starter == false) return;

	SysTime = hb::time::local_time::now();

	for(int i = 0; i < MaxSchedule; i++)
		if ((m_game->m_crusade_war_schedule[i].day == SysTime.day_of_week) &&
			(m_game->m_crusade_war_schedule[i].hour == SysTime.hour) &&
			(m_game->m_crusade_war_schedule[i].minute == SysTime.minute)) {
			hb::logger::log("Automated crusade initiating");
			global_start_crusade_mode();
			return;
		}
}

void WarManager::global_start_crusade_mode()
{
	uint32_t crusade_guid;
	hb::time::local_time SysTime{};

	SysTime = hb::time::local_time::now();
	if (m_game->m_latest_crusade_day_of_week != -1) {
		if (m_game->m_latest_crusade_day_of_week == SysTime.day_of_week) return;
	}
	else m_game->m_latest_crusade_day_of_week = SysTime.day_of_week;

	crusade_guid = GameClock::GetTimeMS();

	local_start_crusade_mode(crusade_guid);
}

void WarManager::local_start_crusade_mode(uint32_t crusade_guid)
{
	

	if (m_game->m_is_crusade_mode) return;
	m_game->m_is_crusade_mode = true;
	m_game->m_crusade_winner_side = 0;

	if (crusade_guid != 0) {
		// GUID  .
		create_crusade_guid(crusade_guid, 0);
		m_game->m_crusade_guid = crusade_guid;
	}

	for(int i = 1; i < MaxClients; i++)
		if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
			m_game->m_client_list[i]->m_crusade_duty = 0;
			m_game->m_client_list[i]->m_construction_point = 0;
			m_game->m_client_list[i]->m_crusade_guid = m_game->m_crusade_guid;
			m_game->send_notify_msg(0, i, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, m_game->m_client_list[i]->m_crusade_duty, 0, 0);
		}

	for(int i = 0; i < MaxMaps; i++)
		if (m_game->m_map_list[i] != 0) m_game->m_map_list[i]->restore_strike_points();

	create_crusade_structures();

	hb::logger::log("Crusade mode enabled");
}

void WarManager::local_end_crusade_mode(int winner_side)
{
	

	//testcode
	hb::logger::log("local_end_crusade_mode({})", winner_side);

	if (m_game->m_is_crusade_mode == false) return;
	m_game->m_is_crusade_mode = false;

	hb::logger::log("Crusade mode disabled");

	remove_crusade_structures();

	remove_crusade_npcs();

	create_crusade_guid(m_game->m_crusade_guid, winner_side);
	m_game->m_crusade_winner_side = winner_side;
	m_game->m_last_crusade_winner = winner_side;

	for(int i = 1; i < MaxClients; i++)
		if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
			m_game->m_client_list[i]->m_crusade_duty = 0;
			m_game->m_client_list[i]->m_construction_point = 0;
			m_game->send_notify_msg(0, i, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, 0, 0, 0, m_game->m_crusade_winner_side);
		}
	remove_crusade_recall_time();

	if (m_game->m_middleland_map_index != -1) {
		//send_msg_to_ls(0x3D00123C, 0, true, 0);
	}
}

void WarManager::manual_end_crusade_mode(int winner_side)
{

	if (m_game->m_is_crusade_mode == false) return;

	local_end_crusade_mode(winner_side);
}

void WarManager::create_crusade_structures()
{
	int z, tX, tY, naming_value;
	char name[6], npc_name[hb::shared::limits::NpcNameLen], npc_way_point[11];

	std::memset(name, 0, sizeof(name));
	std::memset(npc_name, 0, sizeof(npc_name));
	std::memset(npc_way_point, 0, sizeof(npc_way_point));

	for(int i = 0; i < hb::shared::limits::MaxCrusadeStructures; i++)
		if (m_game->m_crusade_structures[i].type != 0) {
			for (z = 0; z < MaxMaps; z++)
				if ((m_game->m_map_list[z] != 0) && (strcmp(m_game->m_map_list[z]->m_name, m_game->m_crusade_structures[i].map_name) == 0)) {
					naming_value = m_game->m_map_list[z]->get_empty_naming_value();
					if (naming_value == -1) {
						// NPC  .     .
					}
					else {
						// NPC .
						std::snprintf(name, sizeof(name), "XX%d", naming_value);
						name[0] = '_';
						name[1] = z + 65;

						switch (m_game->m_crusade_structures[i].type) {
						case 36:
							if (strcmp(m_game->m_map_list[z]->m_name, "aresden") == 0)
								strcpy(npc_name, "AGT-Aresden");
							else if (strcmp(m_game->m_map_list[z]->m_name, "elvine") == 0)
								strcpy(npc_name, "AGT-Elvine");
							break;

						case 37:
							if (strcmp(m_game->m_map_list[z]->m_name, "aresden") == 0)
								strcpy(npc_name, "CGT-Aresden");
							else if (strcmp(m_game->m_map_list[z]->m_name, "elvine") == 0)
								strcpy(npc_name, "CGT-Elvine");
							break;

						case 40:
							if (strcmp(m_game->m_map_list[z]->m_name, "aresden") == 0)
								strcpy(npc_name, "ESG-Aresden");
							else if (strcmp(m_game->m_map_list[z]->m_name, "elvine") == 0)
								strcpy(npc_name, "ESG-Elvine");
							break;

						case 41:
							if (strcmp(m_game->m_map_list[z]->m_name, "aresden") == 0)
								strcpy(npc_name, "GMG-Aresden");
							else if (strcmp(m_game->m_map_list[z]->m_name, "elvine") == 0)
								strcpy(npc_name, "GMG-Elvine");
							break;

						case 42:
							strcpy(npc_name, "ManaStone");
							break;

						default:
							strcpy(npc_name, "Unknown");
							break;
						}

						tX = (int)m_game->m_crusade_structures[i].x;
						tY = (int)m_game->m_crusade_structures[i].y;
						int npc_config_id = m_game->get_npc_config_id_by_name(npc_name);
						if (m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[z]->m_name, 0, 0, MoveType::Random,
							&tX, &tY, npc_way_point, 0, 0, -1, false) == 0) {
							// NameValue .
							m_game->m_map_list[z]->set_naming_value_empty(naming_value);
						}
						else {
							hb::logger::log("Creating Crusade Structure({}) at {}({}, {})", npc_name, m_game->m_crusade_structures[i].map_name, tX, tY);
						}
					}
				}
		}
}

void WarManager::remove_crusade_structures()
{
	

	for(int i = 0; i < MaxNpcs; i++)
		if (m_game->m_npc_list[i] != 0) {
			switch (m_game->m_npc_list[i]->m_type) {
			case 36:
			case 37:
			case 38:
			case 39:
			case 40:
			case 41:
			case 42:
				// Use EntityManager for NPC deletion
				if (m_game->m_entity_manager != NULL)
					m_game->m_entity_manager->delete_entity(i);
				break;
			}
		}
}

void WarManager::remove_crusade_npcs(void)
{
	for(int i = 0; i < MaxNpcs; i++) {
		if (m_game->m_npc_list[i] != 0) {
			if ((m_game->m_npc_list[i]->m_type >= 43 && m_game->m_npc_list[i]->m_type <= 47) || m_game->m_npc_list[i]->m_type == 51) {
				m_game->m_entity_manager->on_entity_killed(i, 0, 0, 0);
			}
		}
	}
}

void WarManager::remove_crusade_recall_time(void)
{
	for(int i = 1; i < MaxClients; i++) {
		if (m_game->m_client_list[i] != 0) {
			if (m_game->m_client_list[i]->m_is_war_location &&
				m_game->m_client_list[i]->m_is_player_civil &&
				m_game->m_client_list[i]->m_is_init_complete) {
				m_game->m_client_list[i]->m_time_left_force_recall = 0;
			}
		}
	}
}

void WarManager::sync_middleland_map_info()
{
	

	if (m_game->m_middleland_map_index != -1) {
		for(int i = 0; i < hb::shared::limits::MaxCrusadeStructures; i++) {
			m_game->m_middle_crusade_structure_info[i].type = 0;
			m_game->m_middle_crusade_structure_info[i].side = 0;
			m_game->m_middle_crusade_structure_info[i].x = 0;
			m_game->m_middle_crusade_structure_info[i].y = 0;
		}
		m_game->m_total_middle_crusade_structures = m_game->m_map_list[m_game->m_middleland_map_index]->m_total_crusade_structures;
		for(int i = 0; i < m_game->m_total_middle_crusade_structures; i++) {
			m_game->m_middle_crusade_structure_info[i].type = m_game->m_map_list[m_game->m_middleland_map_index]->m_crusade_structure_info[i].type;
			m_game->m_middle_crusade_structure_info[i].side = m_game->m_map_list[m_game->m_middleland_map_index]->m_crusade_structure_info[i].side;
			m_game->m_middle_crusade_structure_info[i].x = m_game->m_map_list[m_game->m_middleland_map_index]->m_crusade_structure_info[i].x;
			m_game->m_middle_crusade_structure_info[i].y = m_game->m_map_list[m_game->m_middleland_map_index]->m_crusade_structure_info[i].y;

			/**cp = m_game->m_middle_crusade_structure_info[i].type;
			cp++;
			*cp = m_game->m_middle_crusade_structure_info[i].side;
			cp++;
			sp = (short *)cp;
			*sp = (short)m_game->m_middle_crusade_structure_info[i].x;
			cp += 2;
			sp = (short *)cp;
			*sp = (short)m_game->m_middle_crusade_structure_info[i].y;
			cp += 2;*/
		}

		if (m_game->m_total_middle_crusade_structures != 0) {
			//testcode
			//std::snprintf(G_cTxt, sizeof(G_cTxt), "m_game->m_total_middle_crusade_structures: %d", m_game->m_total_middle_crusade_structures);
			//PutLogList(G_cTxt);
		}
	}
}

void WarManager::select_crusade_duty_handler(int client_h, int duty)
{

	if (m_game->m_client_list[client_h] == 0) return;

	if (m_game->m_last_crusade_winner == m_game->m_client_list[client_h]->m_side &&
		m_game->m_client_list[client_h]->m_crusade_guid == 0 && duty == 3) {
		m_game->m_client_list[client_h]->m_construction_point = 3000;
	}
	m_game->m_client_list[client_h]->m_crusade_duty = duty;

	m_game->send_notify_msg(0, client_h, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, m_game->m_client_list[client_h]->m_crusade_duty, 0, 0);
}

void WarManager::check_crusade_result_calculation(int client_h)
{
	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_var == 1) return;

	if ((m_game->m_is_crusade_mode == false) && (m_game->m_client_list[client_h]->m_crusade_guid != 0)) {
		if (m_game->m_client_list[client_h]->m_war_contribution > m_game->m_max_war_contribution) m_game->m_client_list[client_h]->m_war_contribution = m_game->m_max_war_contribution;
		if (m_game->m_client_list[client_h]->m_crusade_guid == m_game->m_crusade_guid) {
			if (m_game->m_crusade_winner_side == 0) {
				m_game->m_client_list[client_h]->m_exp_stock += (m_game->m_client_list[client_h]->m_war_contribution / 6);
				m_game->send_notify_msg(0, client_h, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, 0, m_game->m_client_list[client_h]->m_war_contribution, 0);
			}
			else {
				if (m_game->m_crusade_winner_side == m_game->m_client_list[client_h]->m_side) {
					if (m_game->m_client_list[client_h]->m_level <= 80) {
						m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level * 100;
					}
					else if (m_game->m_client_list[client_h]->m_level <= 100) {
						m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level * 40;
					}
					else m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level;
					m_game->m_client_list[client_h]->m_exp_stock += m_game->m_client_list[client_h]->m_war_contribution;
					m_game->send_notify_msg(0, client_h, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, 0, m_game->m_client_list[client_h]->m_war_contribution, 0);
				}
				else if (m_game->m_crusade_winner_side != m_game->m_client_list[client_h]->m_side) {
					if (m_game->m_client_list[client_h]->m_level <= 80) {
						m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level * 100;
					}
					else if (m_game->m_client_list[client_h]->m_level <= 100) {
						m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level * 40;
					}
					else m_game->m_client_list[client_h]->m_war_contribution += m_game->m_client_list[client_h]->m_level;
					m_game->m_client_list[client_h]->m_exp_stock += m_game->m_client_list[client_h]->m_war_contribution / 10;
					m_game->send_notify_msg(0, client_h, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, 0, -1 * m_game->m_client_list[client_h]->m_war_contribution, 0);
				}
			}
		}
		else {
			m_game->send_notify_msg(0, client_h, Notify::Crusade, (uint32_t)m_game->m_is_crusade_mode, 0, 0, 0, -1);
		}
		m_game->m_client_list[client_h]->m_crusade_duty = 0;
		m_game->m_client_list[client_h]->m_war_contribution = 0;
		m_game->m_client_list[client_h]->m_crusade_guid = 0;
		m_game->m_client_list[client_h]->m_speed_hack_check_time = GameClock::GetTimeMS();
		m_game->m_client_list[client_h]->m_speed_hack_check_exp = m_game->m_client_list[client_h]->m_exp;
	}
}

bool WarManager::read_crusade_guid_file(const char* fn)
{
	FILE* file;
	uint32_t  file_size;
	char* cp, * token, read_mode;
	char seps[] = "= \t\r\n";

	read_mode = 0;

	std::error_code ec;
	auto fsize = std::filesystem::file_size(fn, ec);
	file_size = ec ? 0 : static_cast<uint32_t>(fsize);

	file = fopen(fn, "rt");
	if (file == 0) {
		return false;
	}
	else {
		cp = new char[file_size + 2];
		std::memset(cp, 0, file_size + 2);
		if (fread(cp, file_size, 1, file) != 1)
			hb::logger::warn("Short read on guid file");

		token = strtok(cp, seps);

		while (token != 0) {

			if (read_mode != 0) {
				switch (read_mode) {
				case 1:
					m_game->m_crusade_guid = atoi(token);
					hb::logger::log("CrusadeGUID = {}", m_game->m_crusade_guid);
					read_mode = 0;
					break;

				case 2:
					// New 13/05/2004 Changed
					m_game->m_last_crusade_winner = atoi(token);
					hb::logger::log("CrusadeWinnerSide = {}", m_game->m_last_crusade_winner);
					read_mode = 0;
					break;
				}
			}
			else {
				if (memcmp(token, "CrusadeGUID", 11) == 0) read_mode = 1;
				if (memcmp(token, "winner-side", 11) == 0) read_mode = 2;
			}

			token = strtok(NULL, seps);
		}

		delete cp;
	}
	if (file != 0) fclose(file);

	return true;
}

void WarManager::create_crusade_guid(uint32_t crusade_guid, int winner_side)
{
	char* cp, txt[256], fn[256], temp[1024];
	FILE* file;

	std::filesystem::create_directories("GameData");
	std::memset(fn, 0, sizeof(fn));

	strcat(fn, "GameData");
	strcat(fn, "/");
	strcat(fn, "/");
	strcat(fn, "CrusadeGUID.Txt");

	file = fopen(fn, "wt");
	if (file == 0) {
		hb::logger::log("Cannot create CrusadeGUID({}) file", crusade_guid);
	}
	else {
		std::memset(temp, 0, sizeof(temp));

		std::memset(txt, 0, sizeof(txt));
		std::snprintf(txt, sizeof(txt), "CrusadeGUID = %d\n", crusade_guid);
		strcat(temp, txt);

		std::memset(txt, 0, sizeof(txt));
		std::snprintf(txt, sizeof(txt), "winner-side = %d\n", winner_side);
		strcat(temp, txt);

		cp = (char*)temp;
		fwrite(cp, strlen(cp), 1, file);

		hb::logger::log("CrusadeGUID({}) file created", crusade_guid);
	}
	if (file != 0) fclose(file);
}

void WarManager::check_commander_construction_point(int client_h)
{
	

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_is_crusade_mode == false) return;
	if (m_game->m_client_list[client_h]->m_construction_point <= 0) return;

	switch (m_game->m_client_list[client_h]->m_crusade_duty) {
	case 1:
	case 2:
		for(int i = 0; i < MaxClients; i++)
			if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_crusade_duty == 3) &&
				(m_game->m_client_list[i]->m_side == m_game->m_client_list[client_h]->m_side)) {
				m_game->m_client_list[i]->m_construction_point += m_game->m_client_list[client_h]->m_construction_point;
				m_game->m_client_list[i]->m_war_contribution += (m_game->m_client_list[client_h]->m_construction_point / 10);

				if (m_game->m_client_list[i]->m_construction_point > m_game->m_max_construction_points)
					m_game->m_client_list[i]->m_construction_point = m_game->m_max_construction_points;

				if (m_game->m_client_list[i]->m_war_contribution > m_game->m_max_war_contribution)
					m_game->m_client_list[i]->m_war_contribution = m_game->m_max_war_contribution;

				m_game->send_notify_msg(0, i, Notify::ConstructionPoint, m_game->m_client_list[i]->m_construction_point, m_game->m_client_list[i]->m_war_contribution, 0, 0);
				m_game->m_client_list[client_h]->m_construction_point = 0;
				return;
			}

		m_game->m_client_list[client_h]->m_construction_point = 0;
		break;

	case 3:

		break;
	}
}

bool WarManager::set_construction_kit(int map_index, int dX, int dY, int type, int time_cost, int client_h)
{
	int naming_value, tX, tY;
	char npc_name[hb::shared::limits::NpcNameLen], name[hb::shared::limits::NpcNameLen], npc_waypoint[11], owner_type;
	short owner_h;

	if ((m_game->m_is_crusade_mode == false) || (m_game->m_client_list[client_h]->m_crusade_duty != 2)) return false;
	if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_total_crusade_structures >= hb::shared::limits::MaxCrusadeStructures) {
		m_game->send_notify_msg(0, client_h, Notify::NoMoreCrusadeStructure, 0, 0, 0, 0);
		return false;
	}

	// NPC .
	naming_value = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_empty_naming_value();
	if (naming_value == -1) {
		// NPC  .     .
	}
	else {

		for(int ix = dX - 3; ix <= dX + 5; ix++)
			for(int iy = dY - 3; iy <= dX + 5; iy++) {
				m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, ix, iy);
				if ((owner_h != 0) && (owner_type == hb::shared::owner_class::Npc) && (m_game->m_npc_list[owner_h]->m_action_limit == 5)) return false;
			}

		// NPC .
		std::memset(npc_name, 0, sizeof(npc_name));
		if (m_game->m_client_list[client_h]->m_side == 1) {
			switch (type) {
			case 1: strcpy(npc_name, "AGT-Aresden"); break;
			case 2: strcpy(npc_name, "CGT-Aresen"); break;
			case 3: strcpy(npc_name, "MS-Aresden"); break;
			case 4: strcpy(npc_name, "DT-Aresden"); break;
			}
		}
		else if (m_game->m_client_list[client_h]->m_side == 2) {
			switch (type) {
			case 1: strcpy(npc_name, "AGT-Elvine"); break;
			case 2: strcpy(npc_name, "CGT-Elvine"); break;
			case 3: strcpy(npc_name, "MS-Elvine"); break;
			case 4: strcpy(npc_name, "DT-Elvine"); break;
			}
		}
		else return false;

		std::memset(name, 0, sizeof(name));
		std::snprintf(name, sizeof(name), "XX%d", naming_value);
		name[0] = '_';
		name[1] = m_game->m_client_list[client_h]->m_map_index + 65;

		std::memset(npc_waypoint, 0, sizeof(npc_waypoint));

		tX = (int)dX;
		tY = (int)dY;
		int npc_config_id = m_game->get_npc_config_id_by_name(npc_name);
		if (m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name, 0, (rand() % 9),
			MoveType::Random, &tX, &tY, npc_waypoint, 0, 0, -1, false, false) == 0) {
			// NameValue .
			m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_naming_value_empty(naming_value);
		}
		else {
			hb::logger::log("Structure({}) construction begin({},{})!", npc_name, tX, tY);
			return true;
		}
	}

	return false;
}

void WarManager::meteor_strike_handler(int map_index)
{
	int dX, dY, index, target_index, total_esg, effect;
	int target_array[smap::MaxStrikePoints];
	short owner_h;
	char  owner_type;
	uint32_t time = GameClock::GetTimeMS();

	hb::logger::log("Beginning meteor strike procedure");

	if (map_index == -1) {
		hb::logger::error("Meteor strike error: map index is -1");
		return;
	}

	if (m_game->m_map_list[map_index] == 0) {
		hb::logger::error("Meteor strike error: null map");
		return;
	}

	if (m_game->m_map_list[map_index]->m_total_strike_points == 0) {
		hb::logger::error("Meteor strike error: no strike points");
		return;
	}

	for(int i = 0; i < smap::MaxStrikePoints; i++) target_array[i] = -1;

	index = 0;
	for(int i = 1; i <= m_game->m_map_list[map_index]->m_total_strike_points; i++) {
		if (m_game->m_map_list[map_index]->m_strike_point[i].hp > 0) {
			target_array[index] = i;
			index++;
		}
	}

	//testcode
	hb::logger::log("Map({}) has {} available strike points", m_game->m_map_list[map_index]->m_name, index);

	m_game->m_meteor_strike_result.casualties = 0;
	m_game->m_meteor_strike_result.crashed_structure_num = 0;
	m_game->m_meteor_strike_result.structure_damage_amount = 0;

	if (index == 0) {
		hb::logger::log("No strike points available");
		m_game->m_delay_event_manager->register_delay_event(sdelay::Type::CalcMeteorStrikeEffect, 0, time + 6000, 0, 0, map_index, 0, 0, 0, 0, 0);
	}
	else {

		for(int i = 1; i < MaxClients; i++)
			if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete) && (m_game->m_client_list[i]->m_map_index == map_index)) {
				m_game->send_notify_msg(0, i, Notify::MeteorStrikeHit, 0, 0, 0, 0);
			}

		for(int i = 0; i < index; i++) {
			target_index = target_array[i];

			if (target_index == -1) {
				hb::logger::error("Strike point error: map index is -1");
				continue;
			}

			dX = m_game->m_map_list[map_index]->m_strike_point[target_index].x;
			dY = m_game->m_map_list[map_index]->m_strike_point[target_index].y;

			// dX, dY    2  Energy Shield Generator    .  1   HP .
			// NPC       .
			total_esg = 0;
			for(int ix = dX - 10; ix <= dX + 10; ix++)
				for(int iy = dY - 10; iy <= dY + 10; iy++) {
					m_game->m_map_list[map_index]->get_owner(&owner_h, &owner_type, ix, iy);
					if ((owner_type == hb::shared::owner_class::Npc) && (m_game->m_npc_list[owner_h] != 0) && (m_game->m_npc_list[owner_h]->m_type == 40)) {
						total_esg++;
					}
				}

			// testcode
			hb::logger::log("Meteor Strike Target({}, {}) ESG({})", dX, dY, total_esg);

			if (total_esg < 2) {

				m_game->m_map_list[map_index]->m_strike_point[target_index].hp -= (2 - total_esg);
				if (m_game->m_map_list[map_index]->m_strike_point[target_index].hp <= 0) {
					m_game->m_map_list[map_index]->m_strike_point[target_index].hp = 0;
					m_game->m_map_list[m_game->m_map_list[map_index]->m_strike_point[target_index].map_index]->m_is_disabled = true;
					m_game->m_meteor_strike_result.crashed_structure_num++;
				}
				else {
					m_game->m_meteor_strike_result.structure_damage_amount += (2 - total_esg);
					effect = m_game->dice(1, 5) - 1;
					m_game->m_dynamic_object_manager->add_dynamic_object_list(0, hb::shared::owner_class::PlayerIndirect, dynamic_object::Fire2, map_index,
						static_cast<short>(m_game->m_map_list[map_index]->m_strike_point[target_index].effect_x[effect] + (m_game->dice(1, 3) - 2)),
						static_cast<short>(m_game->m_map_list[map_index]->m_strike_point[target_index].effect_y[effect] + (m_game->dice(1, 3) - 2)),
						60 * 1000 * 50);
				}
			}
		}

		m_game->m_delay_event_manager->register_delay_event(sdelay::Type::DoMeteorStrikeDamage, 0, time + 1000, 0, 0, map_index, 0, 0, 0, 0, 0);
		m_game->m_delay_event_manager->register_delay_event(sdelay::Type::DoMeteorStrikeDamage, 0, time + 4000, 0, 0, map_index, 0, 0, 0, 0, 0);
		m_game->m_delay_event_manager->register_delay_event(sdelay::Type::CalcMeteorStrikeEffect, 0, time + 6000, 0, 0, map_index, 0, 0, 0, 0, 0);
	}
}

void WarManager::meteor_strike_msg_handler(char attacker_side)
{
	
	uint32_t time = GameClock::GetTimeMS();

	switch (attacker_side) {
	case 1:
		if (m_game->m_elvine_map_index != -1) {
			for(int i = 1; i < MaxClients; i++)
				if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
					if (strcmp(m_game->m_map_list[m_game->m_client_list[i]->m_map_index]->m_location_name, "elvine") == 0) {
						m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 1, 0, 0, 0);
					}
					else {
						m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 2, 0, 0, 0);
					}
				}
			m_game->m_delay_event_manager->register_delay_event(sdelay::Type::MeteorStrike, 0, time + 5000, 0, 0, m_game->m_elvine_map_index, 0, 0, 0, 0, 0);
		}
		else {
			for(int i = 1; i < MaxClients; i++)
				if (m_game->m_client_list[i] != 0) {
					m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 2, 0, 0, 0);
				}
		}
		break;

	case 2:
		if (m_game->m_aresden_map_index != -1) {
			for(int i = 1; i < MaxClients; i++)
				if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
					if (strcmp(m_game->m_map_list[m_game->m_client_list[i]->m_map_index]->m_location_name, "aresden") == 0) {
						m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 3, 0, 0, 0);
					}
					else {
						m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 4, 0, 0, 0);
					}
				}
			m_game->m_delay_event_manager->register_delay_event(sdelay::Type::MeteorStrike, 0, time + 1000 * 5, 0, 0, m_game->m_aresden_map_index, 0, 0, 0, 0, 0);
		}
		else {
			for(int i = 1; i < MaxClients; i++)
				if (m_game->m_client_list[i] != 0) {
					m_game->send_notify_msg(0, i, Notify::meteor_strike_coming, 4, 0, 0, 0);
				}
		}
		break;
	}
}

void WarManager::calc_meteor_strike_effect_handler(int map_index)
{
	int active_structure, structure_hp[smap::MaxStrikePoints];
	char winner_side, temp_data[120];

	if (m_game->m_is_crusade_mode == false) return;

	for(int i = 0; i < smap::MaxStrikePoints; i++)
		structure_hp[i] = 0;

	active_structure = 0;
	for(int i = 1; i <= m_game->m_map_list[map_index]->m_total_strike_points; i++) {
		if (m_game->m_map_list[map_index]->m_strike_point[i].hp > 0) {
			active_structure++;
			structure_hp[i] = m_game->m_map_list[map_index]->m_strike_point[i].hp;
		}
	}

	//testcode
	hb::logger::log("ActiveStructure:{} MapIndex:{} AresdenMap:{} ElvineMap:{}", active_structure, map_index, m_game->m_aresden_map_index, m_game->m_elvine_map_index);

	if (active_structure == 0) {
		if (map_index == m_game->m_aresden_map_index) {
			winner_side = 2;
			local_end_crusade_mode(2);
		}
		else if (map_index == m_game->m_elvine_map_index) {
			winner_side = 1;
			local_end_crusade_mode(1);
		}
		else {
			winner_side = 0;
			local_end_crusade_mode(0);
		}

	}
	else {
		std::memset(temp_data, 0, sizeof(temp_data));
		auto& meteorHdr = *reinterpret_cast<hb::net::MeteorStrikeHeader*>(temp_data);
		meteorHdr.total_points = static_cast<uint16_t>(m_game->m_map_list[map_index]->m_total_strike_points);

		auto* hpEntries = reinterpret_cast<uint16_t*>(temp_data + sizeof(hb::net::MeteorStrikeHeader));
		for (int i = 1; i <= m_game->m_map_list[map_index]->m_total_strike_points; i++) {
			hpEntries[i - 1] = static_cast<uint16_t>(structure_hp[i]);
		}

		grand_magic_result_handler(m_game->m_map_list[map_index]->m_name, m_game->m_meteor_strike_result.crashed_structure_num, m_game->m_meteor_strike_result.structure_damage_amount, m_game->m_meteor_strike_result.casualties, active_structure, m_game->m_map_list[map_index]->m_total_strike_points, temp_data);
	}

	m_game->m_meteor_strike_result.casualties = 0;
	m_game->m_meteor_strike_result.crashed_structure_num = 0;
	m_game->m_meteor_strike_result.structure_damage_amount = 0;
}

void WarManager::do_meteor_strike_damage_handler(int map_index)
{
	int damage;

	for(int i = 1; i < MaxClients; i++)
		if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_side != 0) && (m_game->m_client_list[i]->m_map_index == map_index)) {
			if (m_game->m_client_list[i]->m_level < 80)
				damage = m_game->m_client_list[i]->m_level + m_game->dice(1, 10);
			else damage = m_game->m_client_list[i]->m_level * 2 + m_game->dice(1, 10);
			damage = m_game->dice(1, m_game->m_client_list[i]->m_level) + m_game->m_client_list[i]->m_level;
			// 255   .
			if (damage > 255) damage = 255;

			if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::Protect] == 2) { //magic cut in half
				damage = (damage / 2) - 2;
			}

			if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::Protect] == 5) {
				damage = 0;
			}

			m_game->m_client_list[i]->m_hp -= damage;
			if (m_game->m_client_list[i]->m_hp <= 0) {
				m_game->m_combat_manager->client_killed_handler(i, 0, 0, damage);
				m_game->m_meteor_strike_result.casualties++;
			}
			else {
				if (damage > 0) {
					m_game->send_notify_msg(0, i, Notify::Hp, 0, 0, 0, 0);
					m_game->send_event_to_near_client_type_a(i, hb::shared::owner_class::Player, MsgId::EventMotion, Type::Damage, damage, 0, 0);

					if (m_game->m_client_list[i]->m_skill_using_status[19] != true) {
						m_game->m_map_list[m_game->m_client_list[i]->m_map_index]->clear_owner(0, i, hb::shared::owner_class::Player, m_game->m_client_list[i]->m_x, m_game->m_client_list[i]->m_y);
						m_game->m_map_list[m_game->m_client_list[i]->m_map_index]->set_owner(i, hb::shared::owner_class::Player, m_game->m_client_list[i]->m_x, m_game->m_client_list[i]->m_y);
					}

					if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject] != 0) {
						// Hold-Person    .     .
						// 1: Hold-Person 
						// 2: Paralize
						m_game->send_notify_msg(0, i, Notify::MagicEffectOff, hb::shared::magic::HoldObject, m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject], 0, 0);

						m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject] = 0;
						m_game->m_delay_event_manager->remove_from_delay_event_list(i, hb::shared::owner_class::Player, hb::shared::magic::HoldObject);
					}
				}
			}
		}
}

void WarManager::link_strike_point_map_index()
{
	int z, x;

	for(int i = 0; i < MaxMaps; i++)
		if ((m_game->m_map_list[i] != 0) && (m_game->m_map_list[i]->m_total_strike_points != 0)) {
			for (z = 0; z < smap::MaxStrikePoints; z++)
				if (strlen(m_game->m_map_list[i]->m_strike_point[z].related_map_name) != 0) {
					for (x = 0; x < MaxMaps; x++)
						if ((m_game->m_map_list[x] != 0) && (strcmp(m_game->m_map_list[x]->m_name, m_game->m_map_list[i]->m_strike_point[z].related_map_name) == 0)) {
							m_game->m_map_list[i]->m_strike_point[z].map_index = x;
							//testcode
							hb::logger::log("{}", G_cTxt);

							break;
						}
				}
		}
}

void WarManager::grand_magic_launch_msg_send(int type, char attacker_side)
{}

void WarManager::grand_magic_result_handler(char* map_name, int crashed_structure_num, int structure_damage_amount, int casualities, int active_structure, int total_strike_points, char* data)
{
	

	for(int i = 1; i < MaxClients; i++)
		if (m_game->m_client_list[i] != 0) {
			m_game->send_notify_msg(0, i, Notify::grand_magic_result, crashed_structure_num, structure_damage_amount, casualities, map_name, active_structure, 0, 0, 0, 0, total_strike_points, data);
		}
}

void WarManager::collected_mana_handler(uint16_t aresden_mana, uint16_t elvine_mana)
{
	if (m_game->m_aresden_map_index != -1) {
		m_game->m_aresden_mana += aresden_mana;
		//testcode
		if (aresden_mana > 0) {
			hb::logger::log("Aresden Mana: {} Total:{}", aresden_mana, m_game->m_aresden_mana);
		}
	}

	if (m_game->m_elvine_map_index != -1) {
		m_game->m_elvine_mana += elvine_mana;
		//testcode
		if (elvine_mana > 0) {
			hb::logger::log("Elvine Mana: {} Total:{}", elvine_mana, m_game->m_elvine_mana);
		}
	}
}

void WarManager::send_collected_mana()
{

	if ((m_game->m_collected_mana[1] == 0) && (m_game->m_collected_mana[2] == 0)) return;

	//testcode
	hb::logger::log("Sending Collected Mana: {} {}", m_game->m_collected_mana[1], m_game->m_collected_mana[2]);

	collected_mana_handler(m_game->m_collected_mana[1], m_game->m_collected_mana[2]);

	m_game->m_collected_mana[0] = 0;
	m_game->m_collected_mana[1] = 0;
	m_game->m_collected_mana[2] = 0;
}

void WarManager::send_map_status(int client_h)
{
	char data[hb::shared::limits::MaxCrusadeStructures * sizeof(hb::net::CrusadeStructureEntry) + sizeof(hb::net::CrusadeMapStatusHeader)];
	std::memset(data, 0, sizeof(data));

	auto& hdr = *reinterpret_cast<hb::net::CrusadeMapStatusHeader*>(data);
	std::memcpy(hdr.map_name, m_game->m_client_list[client_h]->m_sending_map_name, sizeof(hdr.map_name));
	hdr.send_point = static_cast<int16_t>(m_game->m_client_list[client_h]->m_crusade_info_send_point);

	if (m_game->m_client_list[client_h]->m_crusade_info_send_point == 0)
		m_game->m_client_list[client_h]->m_is_sending_map_status = true;

	auto* entries = reinterpret_cast<hb::net::CrusadeStructureEntry*>(data + sizeof(hb::net::CrusadeMapStatusHeader));
	int entryCount = 0;

	bool end_of_data = false;
	for (int i = 0; i < 100; i++) {
		if (m_game->m_client_list[client_h]->m_crusade_info_send_point >= hb::shared::limits::MaxCrusadeStructures) { end_of_data = true; break; }
		if (m_game->m_client_list[client_h]->m_crusade_structure_info[m_game->m_client_list[client_h]->m_crusade_info_send_point].type == 0) { end_of_data = true; break; }

		entries[entryCount].type = m_game->m_client_list[client_h]->m_crusade_structure_info[m_game->m_client_list[client_h]->m_crusade_info_send_point].type;
		entries[entryCount].x = m_game->m_client_list[client_h]->m_crusade_structure_info[m_game->m_client_list[client_h]->m_crusade_info_send_point].x;
		entries[entryCount].y = m_game->m_client_list[client_h]->m_crusade_structure_info[m_game->m_client_list[client_h]->m_crusade_info_send_point].y;
		entries[entryCount].side = m_game->m_client_list[client_h]->m_crusade_structure_info[m_game->m_client_list[client_h]->m_crusade_info_send_point].side;

		entryCount++;
		m_game->m_client_list[client_h]->m_crusade_info_send_point++;
	}

	hdr.count = static_cast<uint8_t>(entryCount);

	if (end_of_data) {
		m_game->send_notify_msg(0, client_h, Notify::MapStatusLast, static_cast<int>(sizeof(hb::net::CrusadeMapStatusHeader) + entryCount * sizeof(hb::net::CrusadeStructureEntry)), 0, 0, data);
		m_game->m_client_list[client_h]->m_is_sending_map_status = false;
	}
	else {
		m_game->send_notify_msg(0, client_h, Notify::MapStatusNext, static_cast<int>(sizeof(hb::net::CrusadeMapStatusHeader) + entryCount * sizeof(hb::net::CrusadeStructureEntry)), 0, 0, data);
	}
}

void WarManager::map_status_handler(int client_h, int mode, const char* map_name)
{
	

	if (m_game->m_client_list[client_h] == 0) return;

	switch (mode) {
	case 1:
		// Guild teleport locations removed
		break;

	case 3:
		//if (m_game->m_client_list[client_h]->m_crusade_duty != 3) return;
		for(int i = 0; i < hb::shared::limits::MaxCrusadeStructures; i++) {
			m_game->m_client_list[client_h]->m_crusade_structure_info[i].type = 0;
			m_game->m_client_list[client_h]->m_crusade_structure_info[i].side = 0;
			m_game->m_client_list[client_h]->m_crusade_structure_info[i].x = 0;
			m_game->m_client_list[client_h]->m_crusade_structure_info[i].y = 0;
		}
		m_game->m_client_list[client_h]->m_crusade_info_send_point = 0;
		std::memset(m_game->m_client_list[client_h]->m_sending_map_name, 0, sizeof(m_game->m_client_list[client_h]->m_sending_map_name));

		if (strcmp(map_name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name) == 0) {
			for(int i = 0; i < m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_total_crusade_structures; i++) {
				if (m_game->m_client_list[client_h]->m_crusade_duty == 3)
				{
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].type = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].type;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].side = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].side;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].x = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].x;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].y = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].y;
				}
				else if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].type == 42)
				{
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].type = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].type;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].side = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].side;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].x = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].x;
					m_game->m_client_list[client_h]->m_crusade_structure_info[i].y = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_crusade_structure_info[i].y;
				}
			}
			memcpy(m_game->m_client_list[client_h]->m_sending_map_name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name, 10);
		}
		else {
			if (strcmp(map_name, "middleland") == 0) {
				for(int i = 0; i < m_game->m_total_middle_crusade_structures; i++) {
					if (m_game->m_client_list[client_h]->m_crusade_duty == 3)
					{
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].type = m_game->m_middle_crusade_structure_info[i].type;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].side = m_game->m_middle_crusade_structure_info[i].side;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].x = m_game->m_middle_crusade_structure_info[i].x;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].y = m_game->m_middle_crusade_structure_info[i].y;
					}
					else if (m_game->m_middle_crusade_structure_info[i].type == 42)
					{
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].type = m_game->m_middle_crusade_structure_info[i].type;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].side = m_game->m_middle_crusade_structure_info[i].side;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].x = m_game->m_middle_crusade_structure_info[i].x;
						m_game->m_client_list[client_h]->m_crusade_structure_info[i].y = m_game->m_middle_crusade_structure_info[i].y;
					}
				}
				strcpy(m_game->m_client_list[client_h]->m_sending_map_name, "middleland");
			}
			else {
			}
		}

		send_map_status(client_h);
		break;
	}
}

void WarManager::request_summon_war_unit_handler(int client_h, int dX, int dY, char type, char num, char mode)
{
	char name[6], npc_name[hb::shared::limits::NpcNameLen], map_name[11], npc_way_point[11], owner_type;
	int x;
	int naming_value, tX, tY;
	int ret;
	short owner_h;
	uint32_t time = GameClock::GetTimeMS();

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	//hbest - crusade units summon mapcheck
	if (((strcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "toh3") == 0) || (strcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "icebound") == 0))) {
		return;
	}

	std::memset(npc_way_point, 0, sizeof(npc_way_point));
	std::memset(npc_name, 0, sizeof(npc_name));
	std::memset(map_name, 0, sizeof(map_name));

	if (type < 0) return;
	if (type >= MaxNpcTypes) return;
	if (num > 10) return;

	if (m_game->m_client_list[client_h]->m_construction_point < m_game->m_npc_construction_point[type]) return;
	if ((m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index] != 0) && (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_is_fixed_day_mode)) return;

	num = 1;

	// ConstructionPoint     .
	for (x = 1; x <= num; x++) {
		naming_value = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_empty_naming_value();
		if (naming_value == -1) {
			// NPC  .     .
		}
		else {
			// NPC .
			std::memset(name, 0, sizeof(name));
			std::snprintf(name, sizeof(name), "XX%d", naming_value);
			name[0] = '_';
			name[1] = m_game->m_client_list[client_h]->m_map_index + 65;

			switch (type) {
			case 43: // Light War Beetle
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "LWB-Aresden"); break;
				case 2: strcpy(npc_name, "LWB-Elvine"); break;
				}
				break;

			case 36: // Arrow Guard Tower
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "AGT-Aresden"); break;
				case 2: strcpy(npc_name, "AGT-Elvine"); break;
				}
				break;

			case 37: // Cannon Guard Tower
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "CGT-Aresden"); break;
				case 2: strcpy(npc_name, "CGT-Elvine"); break;
				}
				break;

			case 38: // Mana Collector
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "MS-Aresden"); break;
				case 2: strcpy(npc_name, "MS-Elvine"); break;
				}
				break;

			case 39: // Detector
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "DT-Aresden"); break;
				case 2: strcpy(npc_name, "DT-Elvine"); break;
				}
				break;

			case 51: // Catapult
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "CP-Aresden"); break;
				case 2: strcpy(npc_name, "CP-Elvine"); break;
				}
				break;

			case 44:
				strcpy(npc_name, "GHK");
				break;

			case 45:
				strcpy(npc_name, "GHKABS");
				break;

			case 46:
				strcpy(npc_name, "TK");
				break;

			case 47:
				strcpy(npc_name, "BG");
				break;

			case 82:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "Sor-Aresden"); break;
				case 2: strcpy(npc_name, "Sor-Elvine"); break;
				}
				break;

			case 83:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "ATK-Aresden"); break;
				case 2: strcpy(npc_name, "ATK-Elvine"); break;
				}
				break;

			case 84:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "Elf-Aresden"); break;
				case 2: strcpy(npc_name, "Elf-Elvine"); break;
				}
				break;

			case 85:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "DSK-Aresden"); break;
				case 2: strcpy(npc_name, "DSK-Elvine"); break;
				}
				break;

			case 86:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "HBT-Aresden"); break;
				case 2: strcpy(npc_name, "HBT-Elvine"); break;
				}
				break;

			case 87:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "CT-Aresden"); break;
				case 2: strcpy(npc_name, "CT-Elvine"); break;
				}
				break;

			case 88:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "Bar-Aresden"); break;
				case 2: strcpy(npc_name, "Bar-Elvine"); break;
				}
				break;

			case 89:
				switch (m_game->m_client_list[client_h]->m_side) {
				case 1: strcpy(npc_name, "AGC-Aresden"); break;
				case 2: strcpy(npc_name, "AGC-Elvine"); break;
				}
				break;
			}

			//testcode
			hb::logger::log("Request Summon War Unit ({}) ({})", type, npc_name);

			tX = (int)dX;
			tY = (int)dY;

			ret = false;
			switch (type) {
			case 36:
			case 37:
			case 38:
			case 39:
				// Guild construct location checks removed
				break;
			case 43:
			case 44:
			case 45:
			case 46:
			case 47:
			case 51:
				break;

			case 40:
			case 41:
			case 42:
			case 48:
			case 49:
			case 50:
				break;
			}

			ret = false;
			switch (type) {
			case 36:
			case 37:
				for(int ix = tX - 2; ix <= tX + 2; ix++)
					for(int iy = tY - 2; iy <= tY + 2; iy++) {
						m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, ix, iy);
						if ((owner_h != 0) && (owner_type == hb::shared::owner_class::Npc)) {
							switch (m_game->m_npc_list[owner_h]->m_type) {
							case 36:
							case 37:
								ret = true;
								break;
							}
						}
					}

				if ((dY <= 32) || (dY >= 783)) ret = true;
				break;
			}

			if (ret) {
				m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_naming_value_empty(naming_value);
				m_game->send_notify_msg(0, client_h, Notify::cannot_construct, 1, 0, 0, 0);
				return;
			}

			int npc_config_id = m_game->get_npc_config_id_by_name(npc_name);
			if (mode == 0) {
				ret = m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name, 0, 0, MoveType::Follow, &tX, &tY, npc_way_point, 0, 0, -1, false, false, false, false);
				if (m_game->m_entity_manager != 0) m_game->m_entity_manager->set_npc_follow_mode(name, m_game->m_client_list[client_h]->m_char_name, hb::shared::owner_class::Player);
			}
			else ret = m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name, 0, 0, MoveType::Guard, &tX, &tY, npc_way_point, 0, 0, -1, false, false, false, false);

			if (ret == 0) {
				// NameValue .
				m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_naming_value_empty(naming_value);
			}
			else {
				m_game->m_client_list[client_h]->m_construction_point -= m_game->m_npc_construction_point[type];
				if (m_game->m_client_list[client_h]->m_construction_point < 0) m_game->m_client_list[client_h]->m_construction_point = 0;
				m_game->send_notify_msg(0, client_h, Notify::ConstructionPoint, m_game->m_client_list[client_h]->m_construction_point, m_game->m_client_list[client_h]->m_war_contribution, 0, 0);
			}
		}
	}
}

void WarManager::request_guild_teleport_handler(int client_h)
{
	// Guild teleport system removed
}

void WarManager::request_set_guild_teleport_loc_handler(int, int, int, int, const char*)
{
	// Guild teleport system removed
}

void WarManager::request_set_guild_construct_loc_handler(int, int, int, int, const char*)
{
	// Guild construct loc system removed
}

void WarManager::set_heldenian_mode()
{
	hb::time::local_time SysTime{};

	SysTime = hb::time::local_time::now();
	m_game->m_heldenian_start_hour = SysTime.hour;
	m_game->m_heldenian_start_minute = SysTime.minute;
}

void WarManager::global_start_heldenian_mode()
{
	uint32_t time = GameClock::GetTimeMS();
	local_start_heldenian_mode(m_game->m_heldenian_mode_type, m_game->m_last_heldenian_winner, time);

}

void WarManager::local_start_heldenian_mode(short v1, short v2, uint32_t heldenian_guid)
{
	int x, z, naming_value;
	char name[hb::shared::limits::CharNameLen], npc_waypoint_index[10], side;
	int ret;
	int dX, dY;

	if (m_game->m_is_heldenian_mode) return;

	if ((m_game->m_heldenian_mode_type == -1) || (m_game->m_heldenian_mode_type != v1)) {
		m_game->m_heldenian_mode_type = static_cast<char>(v1);
	}
	if ((m_game->m_last_heldenian_winner != -1) && (m_game->m_last_heldenian_winner == v2)) {
		hb::logger::log<log_channel::events>("Heldenian Mode : {} , Heldenian Last Winner : {}", m_game->m_heldenian_mode_type, m_game->m_last_heldenian_winner);
	}

	if (heldenian_guid != 0) {
		// Persist the new war's guid; keep the standing winner so a mid-war crash doesn't erase the title.
		create_heldenian_guid(heldenian_guid, m_game->m_last_heldenian_winner);
		m_game->m_heldenian_guid = heldenian_guid;
	}
	m_game->m_heldenian_aresden_left_tower = 0;
	m_game->m_heldenian_elvine_left_tower = 0;
	m_game->m_heldenian_aresden_dead = 0;
	m_game->m_heldenian_elvine_dead = 0;
	// The previous winner holds the title until the war produces a new one
	// (type 2: an attacker's flag-plant; type 1: the end-of-war tie-break ladder).
	m_game->m_heldenian_victory_type = m_game->m_last_heldenian_winner;

	// These sweeps ran with break-instead-of-continue in the original (and in #96's
	// defect list): the first non-matching entry aborted the whole scan, so stray
	// mobs and players survived on the war maps and occupied the door tiles the
	// gate spawns need. Full scans now — #95's clean-siege acceptance depends on it.
	for(int i = 0; i < MaxClients; i++) {
		if (m_game->m_client_list[i] != 0) {
			if (m_game->m_client_list[i]->m_is_init_complete != true) continue;
			m_game->m_client_list[i]->m_var = 2;
			// Enroll everyone online for the payout pass. The original never stamped
			// the guid at war start, so check_heldenian_result_calculation could only
			// ever match (and pay) mid-war logins (#96).
			m_game->m_client_list[i]->m_heldenian_guid = m_game->m_heldenian_guid;
			m_game->send_notify_msg(0, i, Notify::HeldenianTeleport, 0, 0, 0, 0);
			m_game->m_client_list[i]->m_war_contribution = 0;
			m_game->m_client_list[i]->m_construction_point = (m_game->m_client_list[i]->m_charisma * 300);
			if (m_game->m_client_list[i]->m_construction_point > 12000) m_game->m_client_list[i]->m_construction_point = 12000;
			m_game->send_notify_msg(0, i, Notify::ConstructionPoint, m_game->m_client_list[i]->m_construction_point, m_game->m_client_list[i]->m_war_contribution, 1, 0);
		}
	}

	for (x = 0; x < MaxMaps; x++) {
		if (m_game->m_map_list[x] == 0) continue;
		if (m_game->m_map_list[x]->m_is_heldenian_map) {
			for(int i = 0; i < MaxClients; i++) {
				if (m_game->m_client_list[i] == 0) continue;
				if (m_game->m_client_list[i]->m_is_init_complete != true) continue;
				if (m_game->m_client_list[i]->m_map_index != x) continue;
				m_game->send_notify_msg(0, i, Notify::Unknown0BE8, 0, 0, 0, 0);
				m_game->request_teleport_handler(i, "1   ", 0, -1, -1);
			}
			for(int i = 0; i < MaxNpcs; i++) {
				if (m_game->m_npc_list[i] == 0) continue;
				if (m_game->m_npc_list[i]->m_is_killed) continue;
				if (m_game->m_npc_list[i]->m_map_index != x) continue;
				m_game->m_npc_list[i]->m_is_summoned = true;
				remove_heldenian_npc(i);
			}

			if (m_game->m_heldenian_mode_type == 1) {
				if (strcmp(m_game->m_map_list[x]->m_name, "btfield") == 0) {
					for(int i = 0; i < smap::MaxHeldenianTower; i++) {
						naming_value = m_game->m_map_list[x]->get_empty_naming_value();
						if (m_game->m_map_list[x]->m_heldenian_tower[i].type_id < 1)  break;
						if (m_game->m_map_list[x]->m_heldenian_tower[i].type_id > MaxNpcTypes) break;
						if (naming_value != -1) {
							dX = m_game->m_map_list[x]->m_heldenian_tower[i].x;
							dY = m_game->m_map_list[x]->m_heldenian_tower[i].y;
							side = m_game->m_map_list[x]->m_heldenian_tower[i].side;
							int npc_config_id = -1;
							for (z = 0; z < MaxNpcTypes; z++) {
								if (m_game->m_npc_config_list[z] == 0) break;
								if (m_game->m_npc_config_list[z]->m_type == m_game->m_map_list[x]->m_heldenian_tower[i].type_id) {
									npc_config_id = z;
								}
							}
							std::memset(name, 0, sizeof(name));
							std::snprintf(name, sizeof(name), "XX%d", naming_value);
							name[0] = 95;
							name[1] = static_cast<char>(i + 65);
							ret = m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[x]->m_name, (rand() % 3), 0, MoveType::Random, &dX, &dY, npc_waypoint_index, 0, 0, side, false, false, false, true, false);
							if (ret == 0) {
								m_game->m_map_list[x]->set_naming_value_empty(naming_value);
							}
							else {
								// ret is the tower's npc handle. The old get_owner probe on the
								// requested tile answered with whatever stood there instead of
								// the tower whenever the spawn search nudged it — and indexed
								// the npc list before bounds-checking the handle (#96).
								if ((ret < MaxNpcs) && (m_game->m_npc_list[ret] != 0)) {
									m_game->m_npc_list[ret]->m_build_count = 0;
								}
								if (side == 1)	m_game->m_heldenian_aresden_left_tower += 1;
								if (side == 2) m_game->m_heldenian_elvine_left_tower += 1;
							}
						}
					}
					hb::logger::log<log_channel::events>("HeldenianAresdenLeftTower : {} , HeldenianElvineLeftTower : {}", m_game->m_heldenian_aresden_left_tower, m_game->m_heldenian_elvine_left_tower);
					update_heldenian_status();
				}
			}
			else if (m_game->m_heldenian_mode_type == 2) {
				if (strcmp(m_game->m_map_list[x]->m_name, "hrampart") == 0) {
					for(int i = 0; i < smap::MaxHeldenianDoor; i++) {
						if ((m_game->m_map_list[x]->m_heldenian_gate_door[i].x == 0) &&
							(m_game->m_map_list[x]->m_heldenian_gate_door[i].y == 0)) break;
						naming_value = m_game->m_map_list[x]->get_empty_naming_value();
						if (naming_value != -1) {
							dX = m_game->m_map_list[x]->m_heldenian_gate_door[i].x;
							dY = m_game->m_map_list[x]->m_heldenian_gate_door[i].y;
							side = m_game->m_last_heldenian_winner;
							int npc_config_id = -1;
							for (z = 0; z < MaxNpcTypes; z++) {
								if (m_game->m_npc_config_list[z] == 0) break;
								if (m_game->m_npc_config_list[z]->m_type == 91) {
									npc_config_id = z;
								}
							}
							std::memset(name, 0, sizeof(name));
							std::snprintf(name, sizeof(name), "XX%d", naming_value);
							name[0] = 95;
							name[1] = static_cast<char>(i + 65);
							ret = m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[x]->m_name, (rand() % 3), 0, MoveType::Random, &dX, &dY, npc_waypoint_index, 0, 0, side, false, false, false, true, false);
							if (ret == 0) {
								m_game->m_map_list[x]->set_naming_value_empty(naming_value);
							}
							else {
								if ((ret < MaxNpcs) && (m_game->m_npc_list[ret] != 0)) {
									m_game->m_npc_list[ret]->m_build_count = 0;
									m_game->m_npc_list[ret]->m_dir = m_game->m_map_list[x]->m_heldenian_gate_door[i].dir;

									// The spawn search may have nudged the gate off its door tile
									// (anything left standing there does that); seat it back exactly.
									short door_x = m_game->m_map_list[x]->m_heldenian_gate_door[i].x;
									short door_y = m_game->m_map_list[x]->m_heldenian_gate_door[i].y;
									if ((m_game->m_npc_list[ret]->m_x != door_x) || (m_game->m_npc_list[ret]->m_y != door_y)) {
										short owner_h;
										char owner_type;
										m_game->m_map_list[x]->get_owner(&owner_h, &owner_type, door_x, door_y);
										if (owner_h == 0) {
											m_game->m_map_list[x]->clear_owner(5, ret, hb::shared::owner_class::Npc, m_game->m_npc_list[ret]->m_x, m_game->m_npc_list[ret]->m_y);
											m_game->m_map_list[x]->set_owner(ret, hb::shared::owner_class::Npc, door_x, door_y);
											m_game->m_npc_list[ret]->m_x = door_x;
											m_game->m_npc_list[ret]->m_y = door_y;
										}
										else {
											hb::logger::warn("Heldenian gate {} could not be seated on its door ({}, {}): tile owned by handle {} class {}",
												i, door_x, door_y, owner_h, static_cast<int>(owner_type));
										}
									}

									// The spawn broadcast went out before the door facing was assigned; the
									// gate never moves again, so push the corrected facing (the gate art only
									// exists for the door directions) the way the construction stages do.
									m_game->send_event_to_near_client_type_a(ret, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);

									hb::logger::log<log_channel::events>("Heldenian gate {} seated at ({}, {}) facing {}",
										i, m_game->m_npc_list[ret]->m_x, m_game->m_npc_list[ret]->m_y, static_cast<int>(m_game->m_npc_list[ret]->m_dir));
								}
								// The arch is derived from the door row, not the NPC (spawn placement may nudge it)
								set_heldenian_gate_arch(x, m_game->m_map_list[x]->m_heldenian_gate_door[i].x,
									m_game->m_map_list[x]->m_heldenian_gate_door[i].y, true);
							}
						}
					}
				}
			}
		}
	}
	m_game->m_heldenian_initiated = true;
	m_game->m_is_heldenian_mode = true;

	// The GM start opens the battle immediately, so follow the prep notice with the
	// battle-live one — the same HeldenianTeleport/HeldenianStart pairing the login
	// paths send. Without it, clients keep showing "casting is forbidden until real
	// battle" for a battle the server already considers live. The real prep window
	// (initiated=false between the T-300 notice and the start) arrives with the
	// event scheduler (#97).
	for (int i = 0; i < MaxClients; i++)
		if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
			m_game->send_notify_msg(0, i, Notify::HeldenianStart, 0, 0, 0, 0);
		}

	hb::logger::log<log_channel::events>("Heldenian started");
	m_game->m_heldenian_start_time = static_cast<uint32_t>(time(0));
}

void WarManager::global_end_heldenian_mode()
{
	//char * cp, data[32];

	if (m_game->m_is_heldenian_mode == false) return;

	local_end_heldenian_mode();

}

void WarManager::local_end_heldenian_mode()
{
	if (m_game->m_is_heldenian_mode == false) return;
	m_game->m_is_heldenian_mode = false;
	m_game->m_heldenian_initiated = true;

	m_game->m_heldenian_finish_time = static_cast<uint32_t>(time(0));
	if (m_game->m_heldenian_mode_type == 1) {
		// Battlefield tie-break ladder: towers left, then deaths; a full tie keeps the previous winner.
		if (m_game->m_heldenian_aresden_left_tower > m_game->m_heldenian_elvine_left_tower) {
			m_game->m_heldenian_victory_type = 1;
		}
		else if (m_game->m_heldenian_aresden_left_tower < m_game->m_heldenian_elvine_left_tower) {
			m_game->m_heldenian_victory_type = 2;
		}
		else if (m_game->m_heldenian_aresden_dead < m_game->m_heldenian_elvine_dead) {
			m_game->m_heldenian_victory_type = 1;
		}
		else if (m_game->m_heldenian_aresden_dead > m_game->m_heldenian_elvine_dead) {
			m_game->m_heldenian_victory_type = 2;
		}
	}
	// Type 2: m_heldenian_victory_type holds the defender from war start, or the attacker who planted the flag.
	m_game->m_last_heldenian_winner = m_game->m_heldenian_victory_type;
	hb::logger::log("Heldenian ended, winner side: {}", m_game->m_last_heldenian_winner);

	// Announce the result once per client, and recall everyone still on a war map.
	for (int x = 0; x < MaxClients; x++)
		if ((m_game->m_client_list[x] != 0) && (m_game->m_client_list[x]->m_is_init_complete)) {
			m_game->send_notify_msg(0, x, Notify::HeldenianEnd, 0, 0, 0, 0);
			if ((m_game->m_map_list[m_game->m_client_list[x]->m_map_index] != 0) &&
				(m_game->m_map_list[m_game->m_client_list[x]->m_map_index]->m_is_heldenian_map)) {
				m_game->request_teleport_handler(x, "1   ", 0, -1, -1);
			}
		}

	// Clear the field for the next war: every war structure and flag on the heldenian maps goes.
	for (int i = 0; i < MaxMaps; i++)
	{
		if ((m_game->m_map_list[i] == 0) || (m_game->m_map_list[i]->m_is_heldenian_map == false)) continue;
		for (int n = 0; n < MaxNpcs; n++)
			if ((m_game->m_npc_list[n] != 0) && (m_game->m_npc_list[n]->m_map_index == i)) {
				m_game->m_npc_list[n]->m_is_summoned = true;
				remove_heldenian_npc(n);
			}
		remove_occupy_flags(i);
	}
	create_heldenian_guid(m_game->m_heldenian_guid, m_game->m_heldenian_victory_type);
}

bool WarManager::update_heldenian_status()
{
	if (m_game->m_is_heldenian_mode != true) return false;
	if (m_game->m_bt_field_map_index == -1) return true;

	// The original's map-interleaved walk read its carry flag uninitialized and
	// notified at most one client per registered map slot (#96); plain walk over
	// the connected-client shortcut instead.
	for (int i = 0; m_game->m_client_shortcut[i] != 0; i++) {
		const int client_h = m_game->m_client_shortcut[i];
		if (m_game->m_client_list[client_h] == 0) continue;
		if (m_game->m_client_list[client_h]->m_is_init_complete != true) continue;
		if (m_game->m_client_list[client_h]->m_map_index != m_game->m_bt_field_map_index) continue;
		m_game->send_notify_msg(0, client_h, Notify::HeldenianCount,
			m_game->m_heldenian_aresden_left_tower, m_game->m_heldenian_elvine_left_tower,
			m_game->m_heldenian_aresden_dead, nullptr, m_game->m_heldenian_elvine_dead);
	}
	return true;
}

void WarManager::create_heldenian_guid(uint32_t heldenian_guid, int winner_side)
{
	char* cp, txt[256], temp[1024];
	FILE* file;

	std::filesystem::create_directories("GameData");

	file = fopen(heldenian_guid_file, "wt");
	if (file == 0) {
		hb::logger::log("Cannot create HeldenianGUID({}) file", heldenian_guid);
	}
	else {
		std::memset(temp, 0, sizeof(temp));

		std::memset(txt, 0, sizeof(txt));
		std::snprintf(txt, sizeof(txt), "HeldenianGUID = %u\n", heldenian_guid);
		strcat(temp, txt);

		std::memset(txt, 0, sizeof(txt));
		std::snprintf(txt, sizeof(txt), "winner-side = %d\n", winner_side);
		strcat(temp, txt);

		cp = (char*)temp;
		fwrite(cp, strlen(cp), 1, file);

		hb::logger::log("HeldenianGUID({}) file created", heldenian_guid);
	}
	if (file != 0) fclose(file);
}

void WarManager::manual_start_heldenian_mode(int heldenian_type)
{
	hb::time::local_time SysTime{};

	if (m_game->m_is_heldenian_mode) return;
	if (m_game->m_is_apocalypse_mode) return;
	if (m_game->m_is_crusade_mode) return;

	if ((heldenian_type == 1) || (heldenian_type == 2)) {
		m_game->m_heldenian_mode_type = static_cast<char>(heldenian_type);
	}

	SysTime = hb::time::local_time::now();
	m_game->m_heldenian_start_hour = SysTime.hour;
	m_game->m_heldenian_start_minute = SysTime.minute;

	m_game->m_heldenian_running = true;
	global_start_heldenian_mode();
}

void WarManager::manual_end_heldenian_mode()
{
	if (m_game->m_is_heldenian_mode == false) return;

	global_end_heldenian_mode();
	m_game->m_heldenian_running = false;
}

void WarManager::remove_heldenian_npc(int npc_h)
{
	if (m_game->m_npc_list[npc_h] == 0) return;
	if (m_game->m_npc_list[npc_h]->m_is_killed) return;

	// A castle gate taken down by the war sweep reopens its archway
	if (m_game->m_npc_list[npc_h]->m_type == 91) unseal_heldenian_gate_for_npc(npc_h);

	m_game->m_npc_list[npc_h]->m_is_killed = true;
	m_game->m_npc_list[npc_h]->m_hp = 0;
	m_game->m_npc_list[npc_h]->m_last_damage = 0;
	m_game->m_npc_list[npc_h]->m_regen_time = 0;
	m_game->m_map_list[m_game->m_npc_list[npc_h]->m_map_index]->m_total_alive_object--;

	m_game->release_follow_mode(npc_h, hb::shared::owner_class::Npc);
	m_game->m_npc_list[npc_h]->m_target_index = 0;
	m_game->m_npc_list[npc_h]->m_target_type = 0;

	m_game->send_event_to_near_client_type_a(npc_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::Dying, 0, 1, 0);
	m_game->m_map_list[m_game->m_npc_list[npc_h]->m_map_index]->clear_owner(10, npc_h, hb::shared::owner_class::Npc, m_game->m_npc_list[npc_h]->m_x, m_game->m_npc_list[npc_h]->m_y);
	m_game->m_map_list[m_game->m_npc_list[npc_h]->m_map_index]->set_dead_owner(npc_h, hb::shared::owner_class::Npc, m_game->m_npc_list[npc_h]->m_x, m_game->m_npc_list[npc_h]->m_y);
	m_game->m_npc_list[npc_h]->m_behavior = 4;
	m_game->m_npc_list[npc_h]->m_behavior_turn_count = 0;
	m_game->m_npc_list[npc_h]->m_dead_time = GameClock::GetTimeMS();

}

// A castle gate guards a 3-tile archway running along the anti-diagonal (the shipped
// door direction); the gate NPC itself occupies only one tile, so the arch is sealed
// by temp-blocking the span plus each tile's south-east shoulder — two tiles thick,
// because movement validates only the destination tile and a single-thick diagonal
// line can be crossed corner-to-corner. The .amd wall layer (m_is_move_allowed, never
// mutated at runtime) bounds the span, so seal and unseal compute the same tiles.
void WarManager::set_heldenian_gate_arch(int map_index, short gate_x, short gate_y, bool sealed)
{
	constexpr int max_reach = 5;
	class CMap* map = m_game->m_map_list[map_index];
	class CTile* tile;

	if (map == 0) return;

	for (int side_dir = -1; side_dir <= 1; side_dir += 2)
		for (int step = (side_dir < 0) ? 1 : 0; step <= max_reach; step++) {
			short x = static_cast<short>(gate_x + side_dir * step);
			short y = static_cast<short>(gate_y - side_dir * step);
			tile = (class CTile*)(map->m_tile + x + y * map->m_size_x);
			if (tile->m_is_move_allowed == false) break;
			map->set_temp_move_allowed_flag(x, y, !sealed);
			tile = (class CTile*)(map->m_tile + (x + 1) + y * map->m_size_x);
			if (tile->m_is_move_allowed) map->set_temp_move_allowed_flag(x + 1, y, !sealed);
		}
}

// Finds the gate-door row a dying/removed gate NPC belongs to and reopens its arch.
void WarManager::unseal_heldenian_gate_for_npc(int npc_h)
{
	int dx, dy;

	if (m_game->m_npc_list[npc_h] == 0) return;
	int map_index = m_game->m_npc_list[npc_h]->m_map_index;
	class CMap* map = m_game->m_map_list[map_index];
	if ((map == 0) || (map->m_is_heldenian_map == false)) return;

	for (int i = 0; i < smap::MaxHeldenianDoor; i++) {
		if ((map->m_heldenian_gate_door[i].x == 0) && (map->m_heldenian_gate_door[i].y == 0)) break;
		dx = map->m_heldenian_gate_door[i].x - m_game->m_npc_list[npc_h]->m_x;
		dy = map->m_heldenian_gate_door[i].y - m_game->m_npc_list[npc_h]->m_y;
		if ((dx >= -3) && (dx <= 3) && (dy >= -3) && (dy <= 3)) {
			set_heldenian_gate_arch(map_index, map->m_heldenian_gate_door[i].x, map->m_heldenian_gate_door[i].y, false);
			return;
		}
	}
}

// Computes this client's war-entry point: btfield spawns by side for a battlefield war,
// the defender/attacker rampart spawns for a castle siege. False = no war to enter
// (or this client cannot fight in it). map_name must hold MapNameLen bytes.
bool WarManager::get_heldenian_entry_point(int client_h, char* map_name, short* dX, short* dY)
{
	if (m_game->m_client_list[client_h] == 0) return false;
	if (m_game->m_is_heldenian_mode != true) return false;
	if (m_game->m_client_list[client_h]->m_is_player_civil) return false;
	if ((m_game->m_client_list[client_h]->m_side != 1) && (m_game->m_client_list[client_h]->m_side != 2)) return false;

	if (m_game->m_heldenian_mode_type == 1) {
		std::memcpy(map_name, "btfield", 7);
		if (m_game->m_client_list[client_h]->m_side == 1) {
			*dX = 68;
			*dY = 225;
		}
		else {
			*dX = 202;
			*dY = 70;
		}
		return true;
	}
	if (m_game->m_heldenian_mode_type == 2) {
		std::memcpy(map_name, "hrampart", 8);
		if (m_game->m_client_list[client_h]->m_side == m_game->m_last_heldenian_winner) {
			*dX = 81;
			*dY = 42;
		}
		else {
			*dX = 156;
			*dY = 153;
		}
		return true;
	}
	return false;
}

// Command Hall "Teleport to Battle Field": answers with a one-entry teleport list
// (or an empty one when no war is running), the shape handle_heldenian_teleport_list expects.
void WarManager::request_heldenian_tp_list(int client_h)
{
	char txt[sizeof(hb::net::PacketResponseTeleportListHeader) + sizeof(hb::net::PacketResponseTeleportListEntry)]{};
	char map_name[hb::shared::limits::MapNameLen]{};
	short dX = 0, dY = 0;
	int ret;

	if (m_game->m_client_list[client_h] == 0) return;

	auto& resp = *reinterpret_cast<hb::net::PacketResponseTeleportListHeader*>(txt);
	resp.header.msg_id = ServerMsgId::response_heldenian_tp_list;
	resp.header.msg_type = 0;
	resp.count = 0;

	size_t size = sizeof(resp);
	if (get_heldenian_entry_point(client_h, map_name, &dX, &dY)) {
		auto& entry = *reinterpret_cast<hb::net::PacketResponseTeleportListEntry*>(txt + sizeof(resp));
		entry.index = 0;
		std::memcpy(entry.map_name, map_name, sizeof(entry.map_name));
		entry.x = dX;
		entry.y = dY;
		entry.cost = 0;
		resp.count = 1;
		size += sizeof(entry);
	}

	ret = m_game->m_client_list[client_h]->m_socket->send_msg(txt, static_cast<int>(size));
	switch (ret) {
	case sock::Event::QueueFull:
	case sock::Event::SocketError:
	case sock::Event::CriticalError:
	case sock::Event::SocketClosed:
		m_game->delete_client(client_h, true, true);
		break;
	}
}

// The pick from that list. The id is display-side only — the destination is
// recomputed here, never trusted from the wire.
void WarManager::request_heldenian_tp(int client_h, char* data, size_t msg_size)
{
	char map_name[hb::shared::limits::MapNameLen]{};
	short dX = 0, dY = 0;

	if (m_game->m_client_list[client_h] == 0) return;

	const auto* req = hb::net::PacketCast<hb::net::PacketRequestTeleportId>(data, msg_size);
	if (!req) return;

	if (get_heldenian_entry_point(client_h, map_name, &dX, &dY)) {
		m_game->request_teleport_handler(client_h, "2   ", map_name, dX, dY);
	}
}

bool WarManager::check_heldenian_map(int attacker_h, int map_index, char type)
{
	short tX, tY;
	int ret;
	class CTile* tile;

	ret = 0;
	if (m_game->m_client_list[attacker_h] == 0) return 0;
	if (m_game->m_is_heldenian_mode == 1) {
		if (type == hb::shared::owner_class::Player) {
			if ((m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index] != 0) && (m_game->m_client_list[attacker_h]->m_side > 0)) {
				tX = m_game->m_client_list[attacker_h]->m_x;
				tY = m_game->m_client_list[attacker_h]->m_y;
				if ((tX < 0) || (tX >= m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index]->m_size_x) ||
					(tY < 0) || (tY >= m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index]->m_size_y)) return 0;
				tile = (class CTile*)(m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index]->m_tile + tX + tY * m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index]->m_size_y);
				if (tile == 0) return 0;
				if (tile->m_occupy_status != 0) {
					if (tile->m_occupy_status < 0) {
						if (m_game->m_client_list[attacker_h]->m_side == 1) {
							ret = 1;
						}
					}
					else if (tile->m_occupy_status > 0) {
						if (m_game->m_client_list[attacker_h]->m_side == 2) {
							ret = 1;
						}
					}
				}
			}
		}
		else if (type == hb::shared::owner_class::Npc) {
			if ((m_game->m_map_list[m_game->m_npc_list[attacker_h]->m_map_index] != 0) && (map_index != -1) && (m_game->m_npc_list[attacker_h]->m_side > 0)) {
				tX = m_game->m_npc_list[attacker_h]->m_x;
				tY = m_game->m_npc_list[attacker_h]->m_y;
				tile = (class CTile*)(m_game->m_map_list[m_game->m_npc_list[attacker_h]->m_map_index]->m_tile + tX + tY * m_game->m_map_list[m_game->m_npc_list[attacker_h]->m_map_index]->m_size_y);
				if (tile == 0) return 0;
				if (tile->m_occupy_status != 0) {
					if (tile->m_occupy_status < 0) {
						if (m_game->m_npc_list[attacker_h]->m_side == 1) {
							ret = 1;
						}
					}
					else if (tile->m_occupy_status > 0) {
						if (m_game->m_npc_list[attacker_h]->m_side == 2) {
							ret = 1;
						}
					}
				}
			}
		}
	}
	return ret;
}

void WarManager::check_heldenian_result_calculation(int client_h)
{
	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_var != 2) return;
	// Pay only once the war is over (the crusade twin's guard, dropped here in the
	// original): mid-war, m_last_heldenian_winner still names the PREVIOUS winner (#96).
	if (m_game->m_is_heldenian_mode) return;
	if ((m_game->m_heldenian_mode_type <= 0) || (m_game->m_client_list[client_h]->m_heldenian_guid == 0)) return;
	if (m_game->m_client_list[client_h]->m_heldenian_guid == m_game->m_heldenian_guid) {
		if (m_game->m_client_list[client_h]->m_side == m_game->m_last_heldenian_winner) {
			if (m_game->m_client_list[client_h]->m_level <= 80) {
				m_game->m_client_list[client_h]->m_war_contribution += (m_game->m_client_list[client_h]->m_level) * 200;
			}
			else if (m_game->m_client_list[client_h]->m_level > 80 && m_game->m_client_list[client_h]->m_level <= 100) {
				m_game->m_client_list[client_h]->m_war_contribution += (m_game->m_client_list[client_h]->m_level) * 100;
			}
			else if (m_game->m_client_list[client_h]->m_level > 100) {
				m_game->m_client_list[client_h]->m_war_contribution += (m_game->m_client_list[client_h]->m_level) * 30;
			}
			// get_exp ADDS its argument; the original seeded it with the character's
			// current exp on top of the war pay, doubling the winner (§3.3/§3.5 bug).
			m_game->get_exp(client_h, (uint32_t)(m_game->m_client_list[client_h]->m_war_contribution * 1.2));
		}
		else {
			m_game->get_exp(client_h, (m_game->m_client_list[client_h]->m_war_contribution / 5));
		}
		m_game->m_client_list[client_h]->m_war_contribution = 0;
		m_game->m_client_list[client_h]->m_heldenian_guid = 0;
		m_game->m_client_list[client_h]->m_speed_hack_check_time = GameClock::GetTimeMS();
		m_game->m_client_list[client_h]->m_speed_hack_check_exp = m_game->m_client_list[client_h]->m_exp;
	}
}

void WarManager::remove_occupy_flags(int map_index)
{
	short dX, dY;
	class CTile* tile;

	if (m_game->m_map_list[map_index] == 0) return;
	for(int i = 1; i < smap::MaxOccupyFlag; i++)
		if (m_game->m_map_list[map_index]->m_occupy_flag[i]) {
			dX = m_game->m_map_list[map_index]->m_occupy_flag[i]->m_x;
			dY = m_game->m_map_list[map_index]->m_occupy_flag[i]->m_y;

			tile = (class CTile*)(m_game->m_map_list[map_index]->m_tile + dX + dY * m_game->m_map_list[map_index]->m_size_y);
			tile->m_occupy_flag_index = 0;

			m_game->m_dynamic_object_manager->remove_dynamic_object(m_game->m_map_list[map_index]->m_occupy_flag[i]->m_dynamic_object_index);

			delete m_game->m_map_list[map_index]->m_occupy_flag[i];
			m_game->m_map_list[map_index]->m_occupy_flag[i] = 0;
			m_game->m_map_list[map_index]->m_total_occupy_flags--;
		}
}

void WarManager::apocalypse_ender()
{
	hb::time::local_time SysTime{};
	

	if (m_game->m_is_apocalypse_mode == false) return;
	if (m_game->m_is_apocalypse_starter == false) return;

	SysTime = hb::time::local_time::now();

	for(int i = 0; i < MaxApocalypse; i++)
		if ((m_game->m_apocalypse_schedule_end[i].day == SysTime.day_of_week) &&
			(m_game->m_apocalypse_schedule_end[i].hour == SysTime.hour) &&
			(m_game->m_apocalypse_schedule_end[i].minute == SysTime.minute)) {
			hb::logger::log("Automated apocalypse concluded");
			global_end_apocalypse_mode();
			return;
		}
}

void WarManager::global_end_apocalypse_mode()
{
	if (m_game->m_is_apocalypse_mode == false) return;

	local_end_apocalypse();
}

void WarManager::local_end_apocalypse()
{


	m_game->m_is_apocalypse_mode = false;
	reset_apocalypse_map_state();

	for(int i = 1; i < MaxClients; i++) {
		if (m_game->m_client_list[i] != 0) {
			m_game->send_notify_msg(0, i, Notify::ApocGateEndMsg, 0, 0, 0, 0);
			m_game->send_notify_msg(0, i, Notify::ApocGateClose, 0, 0, 0, 0);
		}
	}
	hb::logger::log("Apocalypse mode disabled");
}

void WarManager::local_start_apocalypse(uint32_t apocalypse_guid)
{

	//uint32_t dwApocalypse;

	m_game->m_is_apocalypse_mode = true;
	reset_apocalypse_map_state();

	if (apocalypse_guid != 0) {
		create_apocalypse_guid(apocalypse_guid);
		//m_game->m_apocalypse_guid = dwApocalypse;
	}

	for(int i = 1; i < MaxClients; i++) {
		if (m_game->m_client_list[i] != 0) {
			m_game->send_notify_msg(0, i, Notify::ApocGateStartMsg, 0, 0, 0, 0);
			//m_game->request_teleport_handler(i, "0   ");
			//m_game->send_notify_msg(0, i, Notify::ApocForceRecallPlayers, 0, 0, 0, 0);
		}
	}
	hb::logger::log("Apocalypse mode enabled");
}

void WarManager::reset_apocalypse_map_state()
{
	for(int i = 0; i < MaxMaps; i++) {
		if (m_game->m_map_list[i] != 0) {
			m_game->m_map_list[i]->m_apocalypse_gate_open = false;
			m_game->m_map_list[i]->m_apocalypse_boss_spawned = false;
		}
	}
}

void WarManager::apocalypse_progress_tick()
{
	if (m_game->m_is_apocalypse_mode == false) return;

	for(int i = 0; i < MaxMaps; i++)
		check_apocalypse_map_cleared(i);

	apocalypse_gate_travel_tick();
}

// Re-sends each open gate's position to everyone on that map (players who
// arrive after the gate opened need it) and teleports anyone standing on
// the portal. Lives here, not in check_client_response_time, so gate
// travel is never subject to that loop's GM-bypass/anticheat gating.
void WarManager::apocalypse_gate_travel_tick()
{
	for(int m = 0; m < MaxMaps; m++) {
		CMap* map = m_game->m_map_list[m];
		if (map == nullptr || map->m_apocalypse_gate_open == false) continue;

		for(int i = 1; i < MaxClients; i++) {
			if (m_game->m_client_list[i] == 0) continue;
			if (m_game->m_client_list[i]->m_map_index != m) continue;

			m_game->send_notify_msg(0, i, Notify::ApocGateOpen,
				map->m_dynamic_gate_coord.x, map->m_dynamic_gate_coord.y, 0, m_game->m_client_list[i]->m_map_name);

			// The gate data is a single tile but the portal graphic spans
			// several tiles around it — accept standing anywhere on it.
			constexpr int gate_enter_margin = 2;
			if ((m_game->m_client_list[i]->m_x >= map->m_dynamic_gate_coord.Left() - gate_enter_margin) &&
				(m_game->m_client_list[i]->m_x <= map->m_dynamic_gate_coord.Right() + gate_enter_margin) &&
				(m_game->m_client_list[i]->m_y >= map->m_dynamic_gate_coord.Top() - gate_enter_margin) &&
				(m_game->m_client_list[i]->m_y <= map->m_dynamic_gate_coord.Bottom() + gate_enter_margin)) {
				m_game->request_teleport_handler(i, "2   ", map->m_dynamic_gate_coord_dest_map,
					map->m_dynamic_gate_coord_tgt_x, map->m_dynamic_gate_coord_tgt_y);
			}
		}
	}
}

// Abaddon environmental lightning storm — the retail apocalypse hazard,
// reconstructed from the only known implementation (Snoopy v3.82
// DoAbaddonThunderDamageHandler): rolled on the 20s weather tick with a
// 1-in-15 chance, ~one storm every 5 minutes on average.
void WarManager::abaddon_thunder_tick()
{
	if (m_game->m_is_apocalypse_mode == false) return;
	if (m_game->dice(1, 15) != 13) return;

	unleash_abaddon_thunder();
}

// One storm wave over every apocalypse boss map, regardless of mode or
// dice (also the /thunder admin test hook). Damage mitigation matches
// the meteor strike, per retail behavior: Protection-From-Magic halves,
// Absolute-Magic-Protect nulls.
void WarManager::unleash_abaddon_thunder()
{
	for(int m = 0; m < MaxMaps; m++) {
		CMap* map = m_game->m_map_list[m];
		if (map == nullptr || map->is_apocalypse_boss_map() == false) continue;

		for(int i = 1; i < MaxClients; i++) {
			if (m_game->m_client_list[i] == 0) continue;
			if (m_game->m_client_list[i]->m_map_index != m) continue;
			if (m_game->m_client_list[i]->m_is_init_complete == false) continue;

			// Everyone on the map sees the storm; admins, executors, and
			// the dead are spared the damage.
			m_game->send_notify_msg(0, i, Notify::AbaddonThunder, 0, 0, 0, 0);

			if (m_game->m_client_list[i]->m_admin_level > 0) continue;
			if (m_game->m_client_list[i]->m_side == 4) continue;
			if (m_game->m_client_list[i]->m_is_killed) continue;

			int damage = m_game->dice(1, 20) + 100;
			if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::Protect] == 2)
				damage = (damage / 2) - 2;	// Protection-From-Magic
			if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::Protect] == 5)
				damage = 0;					// Absolute-Magic-Protect
			if (damage <= 0) continue;

			m_game->m_client_list[i]->m_hp -= damage;
			if (m_game->m_client_list[i]->m_hp <= 0) {
				m_game->m_combat_manager->client_killed_handler(i, 0, 0, damage);
				continue;
			}

			m_game->send_notify_msg(0, i, Notify::Hp, 0, 0, 0, 0);
			m_game->send_event_to_near_client_type_a(i, hb::shared::owner_class::Player, MsgId::EventMotion, Type::Damage, damage, 0, 0);

			if (m_game->m_client_list[i]->m_skill_using_status[19] != true) {
				map->clear_owner(0, i, hb::shared::owner_class::Player, m_game->m_client_list[i]->m_x, m_game->m_client_list[i]->m_y);
				map->set_owner(i, hb::shared::owner_class::Player, m_game->m_client_list[i]->m_x, m_game->m_client_list[i]->m_y);
			}

			if (m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject] != 0) {
				m_game->send_notify_msg(0, i, Notify::MagicEffectOff, hb::shared::magic::HoldObject, m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject], 0, 0);
				m_game->m_client_list[i]->m_magic_effect_status[hb::shared::magic::HoldObject] = 0;
				m_game->m_delay_event_manager->remove_from_delay_event_list(i, hb::shared::owner_class::Player, hb::shared::magic::HoldObject);
			}
		}
	}
}

void WarManager::check_apocalypse_map_cleared(int map_index)
{
	CMap* map = m_game->m_map_list[map_index];
	if (map == nullptr || map->m_is_apocalypse_map == false) return;
	if (map->m_apocalypse_mob_gen_type == 0) return;
	if (map->m_total_alive_object > 0) return;

	if (map->m_apocalypse_mob_gen_type == 1)
		open_apocalypse_gate(map_index);
	else if (map->m_apocalypse_mob_gen_type == 2)
		spawn_apocalypse_boss(map_index);
}

void WarManager::open_apocalypse_gate(int map_index)
{
	CMap* map = m_game->m_map_list[map_index];
	if (map->m_apocalypse_gate_open) return;
	if (map->m_dynamic_gate_type == 0 || map->m_dynamic_gate_coord_dest_map[0] == '\0') return;

	map->m_apocalypse_gate_open = true;
	hb::logger::log("Apocalypse map '{}' cleared - gate to '{}' opened", map->m_name, map->m_dynamic_gate_coord_dest_map);

	for(int i = 1; i < MaxClients; i++) {
		if (m_game->m_client_list[i] == 0) continue;
		if (m_game->m_client_list[i]->m_map_index != map_index) continue;

		m_game->send_notify_msg(0, i, Notify::ApocGateOpen,
			map->m_dynamic_gate_coord.x, map->m_dynamic_gate_coord.y, 0, m_game->m_client_list[i]->m_map_name);
		m_game->send_notify_msg(0, i, Notify::NoticeMsg, 0, 0, 0, "The land is cleansed - a gate has opened!");
	}
}

void WarManager::spawn_apocalypse_boss(int map_index)
{
	CMap* map = m_game->m_map_list[map_index];
	if (map->m_apocalypse_boss_spawned) return;

	int npc_id = map->m_apocalypse_boss_mob_npc_id;
	if (npc_id <= 0 || npc_id >= MaxNpcTypes || m_game->m_npc_config_list[npc_id] == nullptr) return;

	int naming_value = map->get_empty_naming_value();
	if (naming_value == -1) return;

	char name[8];
	std::snprintf(name, sizeof(name), "XX%d", naming_value);
	name[0] = '_';
	name[1] = static_cast<char>(map_index + 65);

	const auto& rc = map->m_apocalypse_boss_mob;
	int tX = rc.x + (rc.width > 0 ? m_game->dice(1, rc.width) - 1 : 0);
	int tY = rc.y + (rc.height > 0 ? m_game->dice(1, rc.height) - 1 : 0);

	if (m_game->create_new_npc(npc_id, name, map->m_name, 0, 0, MoveType::Random,
		&tX, &tY, 0, 0, 0, -1, false, false, false, false, true) == 0) {
		map->set_naming_value_empty(naming_value);
		hb::logger::warn("Apocalypse boss spawn failed (npc {}) on map '{}'", npc_id, map->m_name);
		return;
	}

	map->m_apocalypse_boss_spawned = true;
	hb::logger::log("Apocalypse map '{}' cleared - boss '{}' has risen", map->m_name, m_game->m_npc_config_list[npc_id]->m_npc_name);

	char txt[80];
	std::snprintf(txt, sizeof(txt), "%s has risen!", m_game->m_npc_config_list[npc_id]->m_npc_name);
	for(int i = 1; i < MaxClients; i++) {
		if (m_game->m_client_list[i] == 0) continue;
		if (m_game->m_client_list[i]->m_map_index != map_index) continue;

		m_game->send_notify_msg(0, i, Notify::NoticeMsg, 0, 0, 0, txt);
	}
}

bool WarManager::read_apocalypse_guid_file(const char* fn)
{
	FILE* file;
	uint32_t  file_size;
	char* cp, * token, read_mode;
	char seps[] = "= \t\r\n";

	read_mode = 0;

	std::error_code ec;
	auto fsize = std::filesystem::file_size(fn, ec);
	file_size = ec ? 0 : static_cast<uint32_t>(fsize);

	file = fopen(fn, "rt");
	if (file == 0) {
		return false;
	}
	else {
		cp = new char[file_size + 2];
		std::memset(cp, 0, file_size + 2);
		if (fread(cp, file_size, 1, file) != 1)
			hb::logger::warn("Short read on guid file");

		token = strtok(cp, seps);

		while (token != 0) {

			if (read_mode != 0) {
				switch (read_mode) {
				case 1:
					m_game->m_apocalypse_guid = atoi(token);
					hb::logger::log("ApocalypseGUID = {}", m_game->m_apocalypse_guid);
					read_mode = 0;
					break;
				}
			}
			else {
				if (memcmp(token, "ApocalypseGUID", 14) == 0) read_mode = 1;
			}

			token = strtok(NULL, seps);
		}

		delete cp;
	}
	if (file != 0) fclose(file);

	return true;
}

bool WarManager::read_heldenian_guid_file(const char* fn)
{
	FILE* file;
	uint32_t  file_size;
	char* cp, * token, read_mode;
	char seps[] = "= \t\r\n";

	read_mode = 0;

	std::error_code ec;
	auto fsize = std::filesystem::file_size(fn, ec);
	file_size = ec ? 0 : static_cast<uint32_t>(fsize);

	file = fopen(fn, "rt");
	if (file == 0) {
		return false;
	}
	else {
		cp = new char[file_size + 2];
		std::memset(cp, 0, file_size + 2);
		if (fread(cp, file_size, 1, file) != 1)
			hb::logger::warn("Short read on guid file");

		token = strtok(cp, seps);

		while (token != 0) {

			if (read_mode != 0) {
				switch (read_mode) {
				case 1:
					m_game->m_heldenian_guid = atoi(token);
					hb::logger::log("HeldenianGUID = {}", m_game->m_heldenian_guid);
					read_mode = 0;
					break;
				case 2:
					m_game->m_last_heldenian_winner = atoi(token);
					hb::logger::log("HeldenianWinnerSide = {}", static_cast<int>(m_game->m_last_heldenian_winner));
					read_mode = 0;
					break;
				}
			}
			else {
				if (memcmp(token, "HeldenianGUID", 13) == 0) read_mode = 1;
				if (memcmp(token, "winner-side", 11) == 0) read_mode = 2;
			}

			token = strtok(NULL, seps);
		}

		delete cp;
	}
	if (file != 0) fclose(file);

	return true;
}

void WarManager::create_apocalypse_guid(uint32_t apocalypse_guid)
{
	char* cp, txt[256], fn[256], temp[1024];
	FILE* file;

	std::filesystem::create_directories("GameData");
	std::memset(fn, 0, sizeof(fn));

	strcat(fn, "GameData");
	strcat(fn, "/");
	strcat(fn, "/");
	strcat(fn, "ApocalypseGUID.Txt");

	file = fopen(fn, "wt");
	if (file == 0) {
		hb::logger::log("Cannot create ApocalypseGUID({}) file", apocalypse_guid);
	}
	else {
		std::memset(temp, 0, sizeof(temp));

		std::memset(txt, 0, sizeof(txt));
		std::snprintf(txt, sizeof(txt), "ApocalypseGUID = %d\n", apocalypse_guid);
		strcat(temp, txt);

		cp = (char*)temp;
		fwrite(cp, strlen(cp), 1, file);

		hb::logger::log("ApocalypseGUID({}) file created", apocalypse_guid);
	}
	if (file != 0) fclose(file);
}

void WarManager::energy_sphere_processor()
{
	int naming_value, c_index, temp, pX, pY;
	char sa, cName_Internal[31], waypoint[31];

	if (m_game->m_middleland_map_index < 0) return;
	if (m_game->m_map_list[m_game->m_middleland_map_index] == 0) return;
	if (m_game->dice(1, 2000) != 123) return;
	if (m_game->m_total_game_server_clients < 500) return;

	if (m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index >= 0) return;

	c_index = m_game->dice(1, m_game->m_map_list[m_game->m_middleland_map_index]->m_total_energy_sphere_creation_point);

	if (m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_creation_list[c_index].type == 0) return;

	sa = 0;
	pX = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_creation_list[c_index].x;
	pY = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_creation_list[c_index].y;
	std::memset(waypoint, 0, sizeof(waypoint));

	naming_value = m_game->m_map_list[m_game->m_middleland_map_index]->get_empty_naming_value();
	if (naming_value != -1) {
		std::memset(cName_Internal, 0, sizeof(cName_Internal));
		std::snprintf(cName_Internal, sizeof(cName_Internal), "XX%d", naming_value);
		cName_Internal[0] = '_';
		cName_Internal[1] = m_game->m_middleland_map_index + 65;

		int npc_config_id = m_game->get_npc_config_id_by_name("Energy-Sphere");
		if ((m_game->create_new_npc(npc_config_id, cName_Internal, m_game->m_map_list[m_game->m_middleland_map_index]->m_name, (rand() % 5), sa, MoveType::Random, &pX, &pY, waypoint, 0, 0, -1, false, false, false)) == 0) {
			m_game->m_map_list[m_game->m_middleland_map_index]->set_naming_value_empty(naming_value);
			return;
		}
	}

	temp = m_game->dice(1, m_game->m_map_list[m_game->m_middleland_map_index]->m_total_energy_sphere_goal_point);
	if (m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[temp].result == 0) return;

	m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index = temp;

	for(int i = 1; i < MaxClients; i++)
		if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
			m_game->send_notify_msg(0, i, Notify::EnergySphereCreated, pX, pY, 0, 0);
		}

	hb::logger::log<log_channel::events>("Energy sphere created at ({}, {})", pX, pY);
}

bool WarManager::check_energy_sphere_destination(int npc_h, short attacker_h, char attacker_type)
{
	int sX, sY, dX, dY, goal_map_index;
	char result;

	if (m_game->m_npc_list[npc_h] == 0) return false;
	if (m_game->m_map_list[m_game->m_npc_list[npc_h]->m_map_index]->m_cur_energy_sphere_goal_point_index == -1) return false;

	if (m_game->m_npc_list[npc_h]->m_map_index != m_game->m_middleland_map_index) {
		goal_map_index = m_game->m_npc_list[npc_h]->m_map_index;

		sX = m_game->m_npc_list[npc_h]->m_x;
		sY = m_game->m_npc_list[npc_h]->m_y;

		result = m_game->m_map_list[goal_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index].result;
		dX = m_game->m_map_list[goal_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index].aresden_x;
		dY = m_game->m_map_list[goal_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index].aresden_y;
		if ((sX >= dX - 2) && (sX <= dX + 2) && (sY >= dY - 2) && (sY <= dY + 2)) {
			m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index = -1;

			if ((attacker_type == hb::shared::owner_class::Player) && (m_game->m_client_list[attacker_h] != 0)) {
				if (m_game->m_client_list[attacker_h]->m_side == 1) { // Aresden (Side:1)
					m_game->m_client_list[attacker_h]->m_contribution += 5;
					hb::logger::log<log_channel::events>("EnergySphere Hit By Aresden Player ({})", m_game->m_client_list[attacker_h]->m_char_name);
				}
				else {
					m_game->m_client_list[attacker_h]->m_contribution -= 10;
				}

				for(int i = 1; i < MaxClients; i++)
					if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
						m_game->send_notify_msg(0, i, Notify::EnergySphereGoalIn, result, m_game->m_client_list[attacker_h]->m_side, 2, m_game->m_client_list[attacker_h]->m_char_name);
					}
			}
			return true;
		}

		dX = m_game->m_map_list[goal_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index].elvine_x;
		dY = m_game->m_map_list[goal_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index].elvine_y;
		if ((sX >= dX - 2) && (sX <= dX + 2) && (sY >= dY - 2) && (sY <= dY + 2)) {
			m_game->m_map_list[goal_map_index]->m_cur_energy_sphere_goal_point_index = -1;

			if ((attacker_type == hb::shared::owner_class::Player) && (m_game->m_client_list[attacker_h] != 0)) {
				if (m_game->m_client_list[attacker_h]->m_side == 2) { // Elvine (Side:2)
					m_game->m_client_list[attacker_h]->m_contribution += 5;
					hb::logger::log<log_channel::events>("EnergySphere Hit By Elvine Player ({})", m_game->m_client_list[attacker_h]->m_char_name);
				}
				else {
					m_game->m_client_list[attacker_h]->m_contribution -= 10;
				}

				for(int i = 1; i < MaxClients; i++)
					if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
						m_game->send_notify_msg(0, i, Notify::EnergySphereGoalIn, result, m_game->m_client_list[attacker_h]->m_side, 1, m_game->m_client_list[attacker_h]->m_char_name);
					}
			}
		}
		return false;
	}
	else {

		sX = m_game->m_npc_list[npc_h]->m_x;
		sY = m_game->m_npc_list[npc_h]->m_y;

		result = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index].result;
		dX = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index].aresden_x;
		dY = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index].aresden_y;
		if ((sX >= dX - 4) && (sX <= dX + 4) && (sY >= dY - 4) && (sY <= dY + 4)) {
			m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index = -1;

			if ((attacker_type == hb::shared::owner_class::Player) && (m_game->m_client_list[attacker_h] != 0)) {
				if (m_game->m_client_list[attacker_h]->m_side == 1) { // Aresden (Side:1)
					m_game->m_client_list[attacker_h]->m_contribution += 5;
					hb::logger::log<log_channel::events>("EnergySphere Hit By Aresden Player ({})", m_game->m_client_list[attacker_h]->m_char_name);
				}
				else {
					m_game->m_client_list[attacker_h]->m_contribution -= 10;
				}

				for(int i = 1; i < MaxClients; i++)
					if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
						m_game->send_notify_msg(0, i, Notify::EnergySphereGoalIn, result, m_game->m_client_list[attacker_h]->m_side, 2, m_game->m_client_list[attacker_h]->m_char_name);
					}
			}
			return true;
		}

		dX = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index].elvine_x;
		dY = m_game->m_map_list[m_game->m_middleland_map_index]->m_energy_sphere_goal_list[m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index].elvine_y;
		if ((sX >= dX - 4) && (sX <= dX + 4) && (sY >= dY - 4) && (sY <= dY + 4)) {
			m_game->m_map_list[m_game->m_middleland_map_index]->m_cur_energy_sphere_goal_point_index = -1;

			if ((attacker_type == hb::shared::owner_class::Player) && (m_game->m_client_list[attacker_h] != 0)) {
				if (m_game->m_client_list[attacker_h]->m_side == 2) { // Elvine (Side:2)
					m_game->m_client_list[attacker_h]->m_contribution += 5;
					hb::logger::log<log_channel::events>("EnergySphere Hit By Aresden Player ({})", m_game->m_client_list[attacker_h]->m_char_name);
				}
				else {
					m_game->m_client_list[attacker_h]->m_contribution -= 10;
				}

				for(int i = 1; i < MaxClients; i++)
					if ((m_game->m_client_list[i] != 0) && (m_game->m_client_list[i]->m_is_init_complete)) {
						m_game->send_notify_msg(0, i, Notify::EnergySphereGoalIn, result, m_game->m_client_list[attacker_h]->m_side, 1, m_game->m_client_list[attacker_h]->m_char_name);
					}
			}
			return true;
		}
		return false;
	}
}

void WarManager::get_occupy_flag_handler(int client_h)
{
	int   num, ret, erase_req, ek_num;
	char item_name[hb::shared::limits::ItemNameLen];
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_enemy_kill_count < 3) return;
	if (m_game->m_client_list[client_h]->m_side == 0) return;

	// The original looked these up by their Korean config names, which no
	// English item table ever matched — even the original's own shipped Item.cfg
	// says "AresdenFlag" — so the EK exchange silently created nothing (#96).
	// These are the plain flags (247/248), not the Master pair.
	std::memset(item_name, 0, sizeof(item_name));
	switch (m_game->m_client_list[client_h]->m_side) {
	case 1: strcpy(item_name, "Aresden Flag"); break;
	case 2: strcpy(item_name, "Elvine Flag");  break;
	}

	// ReqPurchaseItemHandler   .
	num = 1;
	for(int i = 1; i <= num; i++) {

		item = m_game->m_item_manager->create_item(item_name, hb::server::item_origin::war_reward,
			m_game->m_item_manager->birth_at(client_h));
		if (item != nullptr) {

			if (m_game->m_item_manager->add_client_item_list(client_h, item, &erase_req)) {
				if (m_game->m_client_list[client_h]->m_cur_weight_load < 0) m_game->m_client_list[client_h]->m_cur_weight_load = 0;

				if (m_game->m_client_list[client_h]->m_enemy_kill_count > 12) {
					ek_num = 12;
					m_game->m_client_list[client_h]->m_enemy_kill_count -= 12;
				}
				else {
					ek_num = m_game->m_client_list[client_h]->m_enemy_kill_count;
					m_game->m_client_list[client_h]->m_enemy_kill_count = 0;
				}

				// EKNum .
				item->m_instance.special_effect_value1 = ek_num;

				// testcode  .
				hb::logger::log<log_channel::events>("Flag captured: player={} flag_ek={} player_ek={}", m_game->m_client_list[client_h]->m_char_name, ek_num, m_game->m_client_list[client_h]->m_enemy_kill_count);

				ret = m_game->m_item_manager->send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

				if (erase_req == 1)
					m_game->m_item_manager->destroy_item(item,
						hb::server::destroy_reason::merged, client_h);

				m_game->calc_total_weight(client_h);

				switch (ret) {
				case sock::Event::QueueFull:
				case sock::Event::SocketError:
				case sock::Event::CriticalError:
				case sock::Event::SocketClosed:
					m_game->delete_client(client_h, true, true);
					return;
				}

				m_game->send_notify_msg(0, client_h, Notify::EnemyKills, m_game->m_client_list[client_h]->m_enemy_kill_count, 0, 0, 0);
			}
			else
			{
				m_game->m_item_manager->destroy_item(item,
					hb::server::destroy_reason::discarded, client_h);

				m_game->calc_total_weight(client_h);

				ret = m_game->m_item_manager->send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

				switch (ret) {
				case sock::Event::QueueFull:
				case sock::Event::SocketError:
				case sock::Event::CriticalError:
				case sock::Event::SocketClosed:
					m_game->delete_client(client_h, true, true);
					return;
				}
			}
		}
	}
}

size_t WarManager::compose_flag_status_contents(char* data)
{
	hb::time::local_time SysTime{};
	char txt[120];
	

	if (m_game->m_middleland_map_index < 0) return 0;

	SysTime = hb::time::local_time::now();
	strcat(data, "[FILE-DATE]\n\n");

	std::snprintf(txt, sizeof(txt), "file-saved-date: %d %d %d %d %d\n", SysTime.year, SysTime.month, SysTime.day, SysTime.hour, SysTime.minute);
	strcat(data, txt);
	strcat(data, "\n\n");

	for(int i = 1; i < smap::MaxOccupyFlag; i++)
		if (m_game->m_map_list[m_game->m_middleland_map_index]->m_occupy_flag[i] != 0) {

			std::snprintf(txt, sizeof(txt), "flag = %d %d %d %d", m_game->m_map_list[m_game->m_middleland_map_index]->m_occupy_flag[i]->m_side,
				m_game->m_map_list[m_game->m_middleland_map_index]->m_occupy_flag[i]->m_x,
				m_game->m_map_list[m_game->m_middleland_map_index]->m_occupy_flag[i]->m_y,
				m_game->m_map_list[m_game->m_middleland_map_index]->m_occupy_flag[i]->m_enemy_kill_count);
			strcat(data, txt);
			strcat(data, "\n");
		}

	strcat(data, "\n\n");

	return strlen(data);
}

void WarManager::set_summon_mob_action(int client_h, int mode, size_t msg_size, char* data)
{
	int target_index;
	char   seps[] = "= \t\r\n";
	char* token, target_name[11], buff[256];

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_side == 0) return;

	switch (mode) {
	case 0: // Free
	case 1: // Hold
		// client_h   .
		for(int i = 0; i < MaxNpcs; i++)
			if (m_game->m_npc_list[i] != 0) {
				if ((m_game->m_npc_list[i]->m_is_summoned) &&
					(m_game->m_npc_list[i]->m_follow_owner_index == client_h) &&
					(m_game->m_npc_list[i]->m_follow_owner_type == hb::shared::owner_class::Player)) {

					m_game->m_npc_list[i]->m_summon_control_mode = mode;
					m_game->m_npc_list[i]->m_is_perm_attack_mode = false;
					m_game->m_npc_list[i]->m_behavior = Behavior::Move;
					m_game->m_npc_list[i]->m_behavior_turn_count = 0;
					m_game->m_npc_list[i]->m_target_index = 0;
				}
			}
		break;

	case 2:
		if ((msg_size) <= 0) return;
		memcpy(buff, data, msg_size);

		token = strtok(NULL, seps);
		token = strtok(NULL, seps);

		target_index = 0;
		if (token != 0) {
			// token
			if (strlen(token) > hb::shared::limits::CharNameLen - 1)
				memcpy(target_name, token, hb::shared::limits::CharNameLen - 1);
			else memcpy(target_name, token, strlen(token));

			// 2002.8.17
			for(int i = 1; i < MaxClients; i++)
			{
				// if ((m_game->m_client_list[i] != 0) && (memcmp(m_game->m_client_list[i]->m_char_name, target_name, 10) == 0)) { // original
				if ((m_game->m_client_list[i] != 0) &&
					(hb_strnicmp(m_game->m_client_list[i]->m_char_name, target_name, hb::shared::limits::CharNameLen - 1) == 0) &&
					(strcmp(m_game->m_client_list[client_h]->m_map_name, m_game->m_client_list[i]->m_map_name) == 0)) // adamas(map  .)
				{
					target_index = i;
					break;
				}
			}
		}

		if ((target_index != 0) && (m_game->m_client_list[target_index]->m_side != 0) &&
			(m_game->m_client_list[target_index]->m_side != m_game->m_client_list[client_h]->m_side)) {
			for(int i = 0; i < MaxNpcs; i++)
				if (m_game->m_npc_list[i] != 0) {
					if ((m_game->m_npc_list[i]->m_is_summoned) &&
						(m_game->m_npc_list[i]->m_follow_owner_index == client_h) &&
						(m_game->m_npc_list[i]->m_follow_owner_type == hb::shared::owner_class::Player)) {

						m_game->m_npc_list[i]->m_summon_control_mode = mode;
						m_game->m_npc_list[i]->m_behavior = Behavior::Attack;
						m_game->m_npc_list[i]->m_behavior_turn_count = 0;
						m_game->m_npc_list[i]->m_target_index = target_index;
						m_game->m_npc_list[i]->m_target_type = hb::shared::owner_class::Player;
						m_game->m_npc_list[i]->m_is_perm_attack_mode = true;
					}
				}
		}
		break;
	}
}

bool WarManager::set_occupy_flag(char map_index, int dX, int dY, int side, int ek_num, int client_h)
{
	int   dynamic_object_index = 0, index;
	class CTile* tile;

	if (m_game->m_map_list[map_index] == 0) return false;

	bool siege_plant = false;
	if (m_game->m_is_heldenian_mode) {
		if (m_game->m_heldenian_mode_type == 1) {
			// Battlefield: flags carry the previous winner's side and only that side plants (original rule)
			if (m_game->m_bt_field_map_index == -1) return false;
			side = m_game->m_last_heldenian_winner;
			if ((client_h > 0) && (m_game->m_client_list[client_h] != 0)) {
				if (m_game->m_client_list[client_h]->m_side != side) return false;
			}
		}
		else if (m_game->m_heldenian_mode_type == 2) {
			// Castle siege: an attacker plants their own flag inside the winning zone to take
			// the castle; the previous winner defends and wins by holding until the war ends.
			if (m_game->m_godh_map_index == -1) return false;
			if ((client_h <= 0) || (m_game->m_client_list[client_h] == 0)) return false;

			side = m_game->m_client_list[client_h]->m_side;
			if (side == m_game->m_last_heldenian_winner) return false;
			if (map_index != m_game->m_godh_map_index) return false;

			// Half-open bounds; an unconfigured zone (width/height 0) rejects every plant.
			const auto& zone = m_game->m_map_list[map_index]->m_heldenian_winning_zone;
			if ((dX < zone.Left()) || (dX >= zone.Right()) ||
				(dY < zone.Top()) || (dY >= zone.Bottom())) return false;
			siege_plant = true;
		}
	}
	// Outside heldenian (crusade EK flags): the flag keeps the side the item carries.

	if ((side != 1) && (side != 2)) return false;

	if ((dX < 25) || (dX >= m_game->m_map_list[map_index]->m_size_x - 25) ||
		(dY < 25) || (dY >= m_game->m_map_list[map_index]->m_size_y - 25)) return false;

	tile = (class CTile*)(m_game->m_map_list[map_index]->m_tile + dX + dY * m_game->m_map_list[map_index]->m_size_y);
	if (tile->m_attribute != 0) return false;
	if (tile->m_occupy_flag_index != 0) return false;
	if (tile->m_is_move_allowed == false)  return false;

	for(int ix = dX - 3; ix <= dX + 3; ix++)
		for(int iy = dY - 3; iy <= dY + 3; iy++) {
			if ((ix == dX) && (iy == dY)) {

			}
			else {
				tile = (class CTile*)(m_game->m_map_list[map_index]->m_tile + ix + iy * m_game->m_map_list[map_index]->m_size_y);
				if ((tile->m_occupy_flag_index != 0) && (tile->m_occupy_flag_index > 0) &&
					(tile->m_occupy_flag_index < smap::MaxOccupyFlag) && (m_game->m_map_list[map_index]->m_occupy_flag[tile->m_occupy_flag_index] != 0)) {
					if (m_game->m_map_list[map_index]->m_occupy_flag[tile->m_occupy_flag_index]->m_side == side) return false;
				}
			}
		}

	if (m_game->m_map_list[map_index]->m_total_occupy_flags >= smap::MaxOccupyFlag) {
		return false;
	}

	switch (side) {
	case 1:	dynamic_object_index = m_game->m_dynamic_object_manager->add_dynamic_object_list(0, 0, dynamic_object::AresdenFlag1, map_index, dX, dY, 0, 0);	break;
	case 2:	dynamic_object_index = m_game->m_dynamic_object_manager->add_dynamic_object_list(0, 0, dynamic_object::ElvineFlag1, map_index, dX, dY, 0, 0);	break;
	}
	if (dynamic_object_index == 0) return false;

	ek_num = 1;
	index = m_game->m_map_list[map_index]->register_occupy_flag(dX, dY, side, ek_num, dynamic_object_index);
	if (index < 0) {
		// No free flag slot: take the just-created flag object back down.
		m_game->m_dynamic_object_manager->remove_dynamic_object(dynamic_object_index);
		return false;
	}

	tile = (class CTile*)(m_game->m_map_list[map_index]->m_tile + dX + dY * m_game->m_map_list[map_index]->m_size_y);
	tile->m_occupy_flag_index = index;

	m_game->m_map_list[map_index]->m_total_occupy_flags++;

	if ((m_game->m_is_heldenian_mode) && (m_game->m_heldenian_mode_type == 1)) {
		for(int ix = dX - 3; ix <= dX + 3; ix++)
			for(int iy = dY - 3; iy <= dY + 3; iy++) {
				if ((ix < 0) || (ix >= m_game->m_map_list[map_index]->m_size_x) ||
					(iy < 0) || (iy >= m_game->m_map_list[map_index]->m_size_y)) {
				}
				else {
					tile = (class CTile*)(m_game->m_map_list[map_index]->m_tile + ix + iy * m_game->m_map_list[map_index]->m_size_y);
					switch (side) {
					case 1:
						tile->m_occupy_status -= ek_num;
						break;
					case 2:
						tile->m_occupy_status += ek_num;
						break;
					}
				}
			}
	}

	if (siege_plant) {
		// The attackers took the castle: end the siege now with them as the victors.
		m_game->m_heldenian_victory_type = static_cast<char>(side);
		hb::logger::log<log_channel::events>("Heldenian castle flag planted by {} (side {}) at godh({}, {})",
			m_game->m_client_list[client_h]->m_char_name, side, dX, dY);
		global_end_heldenian_mode();
	}
	return true;
}

void WarManager::fightzone_reserve_handler(int client_h, char* data, size_t msg_size)
{
}

void WarManager::fightzone_reserve_processor()
{
}

void WarManager::get_fightzone_ticket_handler(int client_h)
{
}
