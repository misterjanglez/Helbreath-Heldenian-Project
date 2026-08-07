#pragma once
#include <cstddef>
#include <cstdint>
#include "EventSchedule.h"

class CGame;

// Shared by the writer (create_heldenian_guid) and the boot reader so the two
// path spellings cannot drift apart again (case matters on Linux).
constexpr const char* heldenian_guid_file = "GameData/HeldenianGUID.Txt";

class WarManager
{
public:
	WarManager() = default;
	~WarManager() = default;
	void set_game(CGame* game) { m_game = game; }

	// ========================================================================
	// Event Scheduler
	// ========================================================================
	// 5 s tick from CGame::on_timer: starts (and, where the row carries an end
	// time, ends) the three war events from the event_schedule table (#97).
	void event_scheduler();

	// True while any of the three war events runs. The mutual-exclusion
	// predicate every start path shares.
	bool war_event_active() const;

	// ========================================================================
	// Crusade System
	// ========================================================================
	void start_crusade_mode();
	void local_start_crusade_mode(uint32_t guild_guid);
	void local_end_crusade_mode(int winner_side);
	void manual_end_crusade_mode(int winner_side);
	void create_crusade_structures();
	void remove_crusade_structures();
	void remove_crusade_npcs(void);
	void remove_crusade_recall_time(void);
	void sync_middleland_map_info();
	void select_crusade_duty_handler(int client_h, int duty);
	void check_crusade_result_calculation(int client_h);
	bool read_crusade_guid_file(const char* fn);
	void create_crusade_guid(uint32_t crusade_guid, int winner_side);
	void check_commander_construction_point(int client_h);
	bool set_construction_kit(int map_index, int dX, int dY, int type, int time_cost, int client_h);

	// ========================================================================
	// Grand Magic / Meteor Strike
	// ========================================================================
	void meteor_strike_handler(int map_index);
	void meteor_strike_msg_handler(char attacker_side);
	void calc_meteor_strike_effect_handler(int map_index);
	void do_meteor_strike_damage_handler(int map_index);
	void link_strike_point_map_index();
	void grand_magic_launch_msg_send(int type, char attacker_side);
	void grand_magic_result_handler(char* map_name, int crashed_structure_num, int structure_damage_amount, int casualities, int active_structure, int total_strike_points, char* data);
	void collected_mana_handler(uint16_t aresden_mana, uint16_t elvine_mana);
	void send_collected_mana();

	// ========================================================================
	// Map Status & Guild War Operations
	// ========================================================================
	void send_map_status(int client_h);
	void map_status_handler(int client_h, int mode, const char* map_name);
	void request_summon_war_unit_handler(int client_h, int dX, int dY, char type, char num, char mode);
	void request_guild_teleport_handler(int client_h);
	void request_set_guild_teleport_loc_handler(int client_h, int dX, int dY, int guild_guid, const char* map_name);
	void request_set_guild_construct_loc_handler(int client_h, int dX, int dY, int guild_guid, const char* map_name);

	// ========================================================================
	// Heldenian Battle System
	// ========================================================================
	void global_start_heldenian_mode();
	void local_start_heldenian_mode(short v1, short v2, uint32_t heldenian_guid);
	void global_end_heldenian_mode();
	void local_end_heldenian_mode();
	bool update_heldenian_status();
	void create_heldenian_guid(uint32_t heldenian_guid, int winner_side);
	void start_heldenian_mode(int heldenian_type);
	void remove_heldenian_npc(int npc_h);
	void set_heldenian_gate_arch(int map_index, short gate_x, short gate_y, bool sealed);
	void unseal_heldenian_gate_for_npc(int npc_h);
	bool get_heldenian_entry_point(int client_h, char* map_name, short* dX, short* dY);
	void request_heldenian_tp_list(int client_h);
	void request_heldenian_tp(int client_h, char* data, size_t msg_size);
	void request_cityhall_tp_list(int client_h);
	void request_cityhall_tp(int client_h, char* data, size_t msg_size);
	int heldenian_shop_price_multiplier(int client_h);
	uint8_t heldenian_shop_discount(int client_h);
	bool check_heldenian_map(int attacker_h, int map_index, char type);
	void check_heldenian_result_calculation(int client_h);
	void remove_occupy_flags(int map_index);

	// ========================================================================
	// Apocalypse System
	// ========================================================================
	void start_apocalypse_mode();
	void global_end_apocalypse_mode();
	void local_end_apocalypse();
	void local_start_apocalypse(uint32_t apocalypse_guid);
	// Clear-gate progression poll, called once per second while the server
	// runs. When a clear-gated apocalypse map has no alive mobs left, opens
	// its dynamic gate (gen type 1) or spawns the boss (gen type 2). Polling
	// (instead of a kill hook) also catches deletion paths that zero the
	// alive counter, and never spawns the boss mid bulk-kill sweep.
	void apocalypse_progress_tick();
	void abaddon_thunder_tick();
	void unleash_abaddon_thunder();
	bool read_apocalypse_guid_file(const char* fn);
	bool read_heldenian_guid_file(const char* fn);
	void create_apocalypse_guid(uint32_t apocalypse_guid);

	// ========================================================================
	// Energy Sphere & Occupy Territory
	// ========================================================================
	void energy_sphere_processor();
	bool check_energy_sphere_destination(int npc_h, short attacker_h, char attacker_type);
	void get_occupy_flag_handler(int client_h);
	size_t compose_flag_status_contents(char* data);
	void set_summon_mob_action(int client_h, int mode, size_t msg_size, char* data = 0);
	bool set_occupy_flag(char map_index, int dX, int dY, int side, int ek_num, int client_h);

	// ========================================================================
	// FightZone System
	// ========================================================================
	void fightzone_reserve_handler(int client_h, char* data, size_t msg_size);
	void fightzone_reserve_processor();
	void get_fightzone_ticket_handler(int client_h);

private:
	// One row of a teleport list. Both list responses and the City Hall pick
	// recompute their rows server-side, so the wire carries display data only.
	struct tp_list_entry
	{
		const char* map_name;
		short x, y;
	};
	static constexpr int max_tp_list_entries = 2;
	// Battlefield war-entry spawns by side (index side-1), shared by the war
	// teleport and the winner's between-wars hunt teleport (#114).
	static constexpr tp_list_entry btfield_spawn[2] = { { "btfield", 68, 225 }, { "btfield", 202, 70 } };
	int build_cityhall_tp_list(int client_h, tp_list_entry* entries);
	void send_teleport_list(int client_h, uint32_t msg_id, const tp_list_entry* entries, int count);

	void check_apocalypse_map_cleared(int map_index);
	void apocalypse_gate_travel_tick();
	void open_apocalypse_gate(int map_index);
	void spawn_apocalypse_boss(int map_index);
	void reset_apocalypse_map_state();

	// Calendar date (yyyymmdd) each event last auto-started, indexed by
	// scheduled_event::type (-1 = never). The scheduler's once-per-day guard;
	// GM starts never stamp it, so a manual war does not consume the day's
	// scheduled one.
	int m_last_event_start_date[hb::server::config::scheduled_event::count] = { -1, -1, -1 };

	CGame* m_game = nullptr;
};
