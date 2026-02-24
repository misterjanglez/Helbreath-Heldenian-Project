#include "GameChatCommand.h"
#include "GameCmdWhisper.h"
#include "GameCmdBlock.h"
#include "GameCmdUnblock.h"
#include "GameCmdGM.h"
#include "GameCmdRegen.h"
#include "GameCmdCreateItem.h"
#include "GameCmdGiveItem.h"
#include "GameCmdSpawn.h"
#include "GameCmdGoto.h"
#include "GameCmdCome.h"
#include "GameCmdInvis.h"
#include "GameCmdSaveAll.h"
#include "Game.h"
#include "Client.h"
#include "AdminLevel.h"
#include "GameConfigSqliteStore.h"
#include <cstring>
#include <cstdio>
#include "Log.h"
#include "StringCompat.h"
#include "TimeUtils.h"

using namespace hb::shared::net;
GameChatCommandManager& GameChatCommandManager::get()
{
	static GameChatCommandManager instance;
	return instance;
}

void GameChatCommandManager::initialize(CGame* game)
{
	if (m_initialized)
		return;

	m_game = game;
	register_built_in_commands();
	seed_command_permissions();
	m_initialized = true;
}

void GameChatCommandManager::register_command(std::unique_ptr<GameChatCommand> command)
{
	m_commands.push_back(std::move(command));
}

bool GameChatCommandManager::process_command(int client_h, const char* message, size_t msg_size)
{
	if (m_game == nullptr || message == nullptr)
		return false;

	if (message[0] != '/')
		return false;

	const char* command = message + 1;

	for (const auto& cmd : m_commands)
	{
		const char* cmdName = cmd->get_name();
		size_t cmdLen = std::strlen(cmdName);

		if (hb_strnicmp(command, cmdName, cmdLen) == 0)
		{
			char nextChar = command[cmdLen];
			if (nextChar == '\0' || nextChar == ' ' || nextChar == '\t')
			{
				const char* args = command + cmdLen;
				while (*args == ' ' || *args == '\t')
					args++;

				// Permission check: DB is sole authority, default to Administrator if not configured
				int required_level = m_game->get_command_required_level(cmd->get_name());

				// Level 0 = no restriction (player commands like /to, /block)
				if (required_level > 0)
				{
					int player_level = m_game->m_client_list[client_h]->m_admin_level;
					if (player_level < required_level)
						return false;

					// Admin commands require GM mode to be active (except /gm itself)
					if (cmd->requires_gm_mode() && !m_game->m_client_list[client_h]->m_is_gm_mode)
					{
						m_game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "You must enable GM mode first (/gm on).");
						return true;
					}
				}

				log_command(client_h, message);
				return cmd->execute(m_game, client_h, args);
			}
		}
	}

	return false;
}

void GameChatCommandManager::log_command(int client_h, const char* command)
{
	if (m_game == nullptr || m_game->m_client_list[client_h] == nullptr)
		return;

	FILE* file = fopen("gamelogs/commands.log", "a");
	if (file == nullptr)
		return;

	hb::time::local_time st{};
	st = hb::time::local_time::now();

	fprintf(file, "[%02d:%02d:%02d] %s: %s\n",
		st.hour, st.minute, st.second,
		m_game->m_client_list[client_h]->m_char_name,
		command);

	fclose(file);
}

void GameChatCommandManager::register_built_in_commands()
{
	register_command(std::make_unique<GameCmdWhisper>());
	register_command(std::make_unique<GameCmdBlock>());
	register_command(std::make_unique<GameCmdUnblock>());
	register_command(std::make_unique<GameCmdGM>());
	register_command(std::make_unique<GameCmdRegen>());
	register_command(std::make_unique<GameCmdCreateItem>());
	register_command(std::make_unique<GameCmdGiveItem>());
	register_command(std::make_unique<GameCmdSpawn>());
	register_command(std::make_unique<GameCmdGoto>());
	register_command(std::make_unique<GameCmdCome>());
	register_command(std::make_unique<GameCmdInvis>());
	register_command(std::make_unique<GameCmdSaveAll>());
}

void GameChatCommandManager::seed_command_permissions()
{
	if (m_game == nullptr)
		return;

	bool changed = false;
	for (const auto& cmd : m_commands)
	{
		const char* name = cmd->get_name();
		if (m_game->m_command_permissions.find(name) == m_game->m_command_permissions.end())
		{
			CommandPermission perm;
			perm.admin_level = cmd->get_default_level();
			m_game->m_command_permissions[name] = perm;
			changed = true;

			hb::logger::log("Command '/{}' registered (default level: {})", name, cmd->get_default_level());
		}
	}

	if (changed)
	{
		sqlite3* configDb = nullptr;
		std::string dbPath;
		if (EnsureGameConfigDatabase(&configDb, dbPath, nullptr))
		{
			SaveCommandPermissions(configDb, m_game);
			CloseGameConfigDatabase(configDb);
		}
	}
}
