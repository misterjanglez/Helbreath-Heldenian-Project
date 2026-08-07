#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <memory>

#include "NetConstants.h"

class CGame;

// First word of `args` into `out` (clamped to the 10-char wire name), false
// when there is none. The fourth command to hand-roll this loop is why it
// lives here — three copies had already grown three spellings of the bound.
bool parse_char_name(const char* args,
	char (&out)[hb::shared::limits::CharNameLen]);

class GameChatCommand
{
public:
	virtual ~GameChatCommand() = default;
	virtual const char* get_name() const = 0;
	virtual int get_default_level() const { return 0; }
	virtual bool requires_gm_mode() const { return get_default_level() > 0; }
	virtual bool execute(CGame* game, int client_h, const char* args) = 0;
};

class GameChatCommandManager
{
public:
	static GameChatCommandManager& get();

	void initialize(CGame* game);
	void register_command(std::unique_ptr<GameChatCommand> command);

	// Process a chat message starting with '/'. Returns true if consumed.
	bool process_command(int client_h, const char* message, size_t msg_size);

private:
	GameChatCommandManager() = default;
	~GameChatCommandManager() = default;
	GameChatCommandManager(const GameChatCommandManager&) = delete;
	GameChatCommandManager& operator=(const GameChatCommandManager&) = delete;

	void register_built_in_commands();
	void seed_command_permissions();
	void log_command(int client_h, const char* command);

	CGame* m_game = nullptr;
	std::vector<std::unique_ptr<GameChatCommand>> m_commands;
	bool m_initialized = false;
};
