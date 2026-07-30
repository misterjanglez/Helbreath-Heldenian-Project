#pragma once

#include <string>

struct server_config
{
	// The drop_rates block retired with #73. Drop generosity is four
	// multipliers in gamedata.db now (drop_multipliers), which reload live -
	// these were restart-only, and the base chances they scaled no longer exist.

	// Timing intervals (milliseconds)
	struct
	{
		int client_timeout_ms = 30000;
		int stamina_regen_ms = 10000;
		int poison_damage_ms = 8000;
		int health_regen_ms = 15000;
		int mana_regen_ms = 15000;
		int hunger_consume_ms = 60000;
		int summon_duration_ms = 300000;
		int autosave_ms = 600000;
		int lag_protection_ms = 7000;

		// Provenance Ledger flush cadence (#76). Not folded into autosave_ms
		// because the two protect different windows: autosave is per-character
		// and staggered across ten minutes, while the ledger buffer is one global
		// batch whose loss on a crash is measured from the last flush. The design
		// contract promises that loss is "the final seconds", so this is short.
		// 0 disables the timer, leaving only the size trigger and the explicit
		// flushes on save-all and shutdown.
		int ledger_flush_ms = 30000;

		// Buffered-row count that forces an early flush, so a busy world does not
		// hold an unbounded buffer between cadence windows. 0 disables it.
		int ledger_flush_events = 512;

		// How long an item lies on the ground before it despawns (#79).
		//
		// Original Helbreath has no such timer — ground items there survive
		// until the tile they are on overflows, which on a busy world means the
		// map accumulates litter for as long as the server is up, and every
		// player entering a sector pays to be told about all of it. This is a
		// deliberate deviation, added for that cost.
		//
		// 0 restores the original behaviour exactly: no sweep runs, and the only
		// ground exits are tile overflow and shutdown.
		int ground_item_lifetime_ms = 300000;   // 5 minutes

		// Ceiling on how many ground tiles one tick may expire. The sweep is
		// already proportional to what actually expired rather than to map area,
		// but a burst — a raid ending, or the first tick after the timer is
		// enabled on a world full of litter — would otherwise land on one frame.
		// Whatever is left simply waits for the next tick.
		int ground_item_sweep_budget = 64;
	} timing;

	// Combat tuning
	struct
	{
		std::string enemy_kill_mode = "deathmatch";
		int enemy_kill_adjust = 1;
		int slate_success_rate = 25;
		int min_hit_ratio = 15;
		int max_hit_ratio = 99;
	} combat;

	// Character rules
	struct
	{
		int base_stat_value = 10;
		int max_creation_stat_value = 4;
		int creation_stat_points = 10;
		int levelup_stat_gain = 3;
		int max_level = 180;
		int max_stat_value = 200;
		int starting_luck = 10;
	} character;

	// Gameplay limits
	struct
	{
		int nighttime_duration = 30;
		int starting_guild_rank = 12;
		int grand_magic_mana_cost = 15;
		int max_construction_points = 30000;
		int max_summon_points = 30000;
		int max_war_contribution = 200000;
		int max_bank_items = 200;
	} gameplay;

	// Raid schedule (-1 = disabled, minutes)
	struct
	{
		short monday = 3;
		short tuesday = 3;
		short wednesday = 3;
		short thursday = 3;
		short friday = 30;
		short saturday = 45;
		short sunday = 60;
	} raid_schedule;

	// Realm configuration
	struct
	{
		std::string name = "Apocalypse";
		std::string login_listen_ip = "0.0.0.0";
		int login_listen_port = 2500;
		std::string game_listen_ip = "0.0.0.0";
		int game_listen_port = 9907;
		std::string game_connection_ip;
		int game_connection_port = 0;
	} realm;
};

// Load server_config.json from disk. Returns true on success.
// On failure, cfg is left at defaults and an error is logged.
bool load_server_config(const std::string& path, server_config& cfg);

// Save the full config back to disk, ensuring all keys are present.
bool save_server_config(const std::string& path, const server_config& cfg);
