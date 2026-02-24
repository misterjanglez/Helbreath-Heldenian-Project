#include "ServerConfig.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "Log.h"

using json = nlohmann::json;

namespace
{
	template <typename T>
	void read_value(const json& obj, const char* key, T& out)
	{
		if (obj.contains(key) && !obj[key].is_null())
		{
			out = obj[key].get<T>();
		}
	}

	void read_value(const json& obj, const char* key, short& out)
	{
		if (obj.contains(key))
		{
			out = static_cast<short>(obj[key].get<int>());
		}
	}
}

bool load_server_config(const std::string& path, server_config& cfg)
{
	cfg = server_config{};

	std::string file_path = path;
	if (!std::filesystem::exists(file_path))
	{
		auto exe_dir = std::filesystem::current_path();
		file_path = (exe_dir / path).string();
	}

	if (!std::filesystem::exists(file_path))
	{
		hb::logger::error("server_config.json not found at '{}'", file_path);
		return false;
	}

	std::ifstream file(file_path);
	if (!file.is_open())
	{
		hb::logger::error("Failed to open server_config.json at '{}'", file_path);
		return false;
	}

	json root;
	try
	{
		root = json::parse(file, nullptr, true, true);
	}
	catch (const json::parse_error& e)
	{
		hb::logger::error("server_config.json parse error: {}", e.what());
		return false;
	}

	// Drop rates
	if (root.contains("drop_rates"))
	{
		auto& dr = root["drop_rates"];
		read_value(dr, "primary", cfg.drop_rates.primary);
		read_value(dr, "gold", cfg.drop_rates.gold);
		read_value(dr, "secondary", cfg.drop_rates.secondary);
		read_value(dr, "rep_modifier", cfg.drop_rates.rep_modifier);
	}

	// Timing
	if (root.contains("timing"))
	{
		auto& t = root["timing"];
		read_value(t, "client_timeout_ms", cfg.timing.client_timeout_ms);
		read_value(t, "stamina_regen_ms", cfg.timing.stamina_regen_ms);
		read_value(t, "poison_damage_ms", cfg.timing.poison_damage_ms);
		read_value(t, "health_regen_ms", cfg.timing.health_regen_ms);
		read_value(t, "mana_regen_ms", cfg.timing.mana_regen_ms);
		read_value(t, "hunger_consume_ms", cfg.timing.hunger_consume_ms);
		read_value(t, "summon_duration_ms", cfg.timing.summon_duration_ms);
		read_value(t, "autosave_ms", cfg.timing.autosave_ms);
		read_value(t, "lag_protection_ms", cfg.timing.lag_protection_ms);
	}

	// Combat
	if (root.contains("combat"))
	{
		auto& c = root["combat"];
		read_value(c, "enemy_kill_mode", cfg.combat.enemy_kill_mode);
		read_value(c, "enemy_kill_adjust", cfg.combat.enemy_kill_adjust);
		read_value(c, "slate_success_rate", cfg.combat.slate_success_rate);
		read_value(c, "min_hit_ratio", cfg.combat.min_hit_ratio);
		read_value(c, "max_hit_ratio", cfg.combat.max_hit_ratio);
	}

	// Character
	if (root.contains("character"))
	{
		auto& ch = root["character"];
		read_value(ch, "base_stat_value", cfg.character.base_stat_value);
		read_value(ch, "creation_stat_bonus", cfg.character.creation_stat_bonus);
		read_value(ch, "levelup_stat_gain", cfg.character.levelup_stat_gain);
		read_value(ch, "max_level", cfg.character.max_level);
		read_value(ch, "starting_luck", cfg.character.starting_luck);
	}

	// Gameplay
	if (root.contains("gameplay"))
	{
		auto& gp = root["gameplay"];
		read_value(gp, "nighttime_duration", cfg.gameplay.nighttime_duration);
		read_value(gp, "starting_guild_rank", cfg.gameplay.starting_guild_rank);
		read_value(gp, "grand_magic_mana_cost", cfg.gameplay.grand_magic_mana_cost);
		read_value(gp, "max_construction_points", cfg.gameplay.max_construction_points);
		read_value(gp, "max_summon_points", cfg.gameplay.max_summon_points);
		read_value(gp, "max_war_contribution", cfg.gameplay.max_war_contribution);
		read_value(gp, "max_bank_items", cfg.gameplay.max_bank_items);
	}

	// Raid schedule
	if (root.contains("raid_schedule"))
	{
		auto& rs = root["raid_schedule"];
		read_value(rs, "monday", cfg.raid_schedule.monday);
		read_value(rs, "tuesday", cfg.raid_schedule.tuesday);
		read_value(rs, "wednesday", cfg.raid_schedule.wednesday);
		read_value(rs, "thursday", cfg.raid_schedule.thursday);
		read_value(rs, "friday", cfg.raid_schedule.friday);
		read_value(rs, "saturday", cfg.raid_schedule.saturday);
		read_value(rs, "sunday", cfg.raid_schedule.sunday);
	}

	// Realm
	if (root.contains("realm"))
	{
		auto& r = root["realm"];
		read_value(r, "name", cfg.realm.name);
		read_value(r, "login_listen_ip", cfg.realm.login_listen_ip);
		read_value(r, "login_listen_port", cfg.realm.login_listen_port);
		read_value(r, "game_listen_ip", cfg.realm.game_listen_ip);
		read_value(r, "game_listen_port", cfg.realm.game_listen_port);
		read_value(r, "game_connection_ip", cfg.realm.game_connection_ip);
		read_value(r, "game_connection_port", cfg.realm.game_connection_port);
	}

	hb::logger::log("Loaded server_config.json successfully");
	return true;
}
