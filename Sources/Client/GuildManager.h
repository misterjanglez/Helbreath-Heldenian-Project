// guild_manager.h: Handles client-side guild network messages.
// Extracted from NetworkMessages_Guild.cpp (Phase B4).

#pragma once

#include "GameConstants.h"
#include <cstdint>
#include <string>

class CGame;

class guild_manager
{
public:
	struct guild_name_entry
	{
		uint32_t ref_time = 0;
		int guild_rank = -1;
		std::string char_name;
		std::string guild_name;
	};

	guild_manager() = default;
	~guild_manager() = default;

	void set_game(CGame* game) { m_game = game; }

	// Guild name cache
	bool find_guild_name(const char* name, uint32_t cur_time, int* out_index);
	void clear_name_cache();
	guild_name_entry& get_name_entry(int index) { return m_name_cache[index]; }

	// Guild creation/disband responses
	void handle_create_new_guild_response(char* data);
	void handle_disband_guild_response(char* data);

	// Guild notification handlers
	void handle_guild_disbanded(char* data);
	void handle_new_guilds_man(char* data);
	void handle_dismiss_guilds_man(char* data);
	void handle_cannot_join_more_guilds_man(char* data);

	// Guild membership responses
	void handle_join_guild_approve(char* data);
	void handle_join_guild_reject(char* data);
	void handle_dismiss_guild_approve(char* data);
	void handle_dismiss_guild_reject(char* data);

	// Guild queries
	void handle_query_join_guild_permission(char* data);
	void handle_query_dismiss_guild_permission(char* data);
	void handle_req_guild_name_answer(char* data);

	// Simple notification handlers
	void handle_no_guild_master_level(char* data);
	void handle_success_ban_guild_man(char* data);
	void handle_cannot_ban_guild_man(char* data);

private:
	static void update_location_flags(CGame* game, const char* location);
	CGame* m_game = nullptr;
	guild_name_entry m_name_cache[game_limits::max_guild_names];
};
