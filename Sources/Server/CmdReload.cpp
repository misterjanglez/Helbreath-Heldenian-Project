#include "CmdReload.h"
#include "ServerConsole.h"
#include "Game.h"
#include "SkillManager.h"
#include "MagicManager.h"
#include "ItemManager.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include "StringCompat.h"

void CmdReload::execute(CGame* game, const char* args)
{
	if (args == nullptr || args[0] == '\0')
	{
		hb::console::error("Usage: reload <items|magic|skills|npcs|shops|config|formulas|colors|tiers|guilds|all>");
		return;
	}

	bool items = false;
	bool magic = false;
	bool skills = false;
	bool npcs = false;
	bool shops = false;
	bool config = false;
	bool formulas = false;
	bool colors = false;
	bool tiers = false;
	bool guilds = false;

	if (hb_stricmp(args, "items") == 0)
		items = true;
	else if (hb_stricmp(args, "magic") == 0)
		magic = true;
	else if (hb_stricmp(args, "skills") == 0)
		skills = true;
	else if (hb_stricmp(args, "npcs") == 0)
		npcs = true;
	else if (hb_stricmp(args, "shops") == 0)
		shops = true;
	else if (hb_stricmp(args, "config") == 0)
		config = true;
	else if (hb_stricmp(args, "formulas") == 0)
		formulas = true;
	else if (hb_stricmp(args, "colors") == 0)
		colors = true;
	else if (hb_stricmp(args, "tiers") == 0 ||
		hb_stricmp(args, "catalog") == 0 || hb_stricmp(args, "attributes") == 0)  // legacy aliases
		tiers = true;
	else if (hb_stricmp(args, "guilds") == 0)
		guilds = true;
	else if (hb_stricmp(args, "all") == 0)
	{
		items = true;
		magic = true;
		skills = true;
		npcs = true;
		shops = true;
		config = true;
		formulas = true;
		colors = true;
		tiers = true;
		guilds = true;
	}
	else
	{
		hb::console::error("Unknown reload target: '{}'. Use items, magic, skills, npcs, shops, config, formulas, colors, tiers, guilds, or all.", args);
		return;
	}

	// Reload server_config.json if requested
	if (config)
	{
		if (game->reload_server_config())
			hb::console::success("server_config.json reloaded");
		else
			hb::console::error("server_config.json reload failed");
	}

	// Reload formulas from database
	if (formulas)
	{
		if (game->reload_formulas())
			hb::console::success("Formulas reloaded");
		else
			hb::console::error("Formula reload failed");
	}

	// Transactional tier reload runs before any client traffic: a rejected
	// candidate leaves the running config untouched and must not be pushed.
	// Runs after `items`/`npcs` so cross-checks see the data just reloaded.
	if (items)           game->m_item_manager->reload_item_configs();
	if (npcs)            game->reload_npc_configs();

	// A rejected candidate leaves the running config untouched and must not
	// reach clients, so tiers_ok gates the notification and push below.
	bool tiers_ok = false;
	if (tiers)
	{
		tiers_ok = game->reload_tier_tables();
		if (tiers_ok)
			hb::console::success("Tier + legacy-pool tables reloaded");
	}

	// Guild progression rides the same candidate/reject contract; it has no
	// client push — the dataset is server-side until the #123 wire family.
	bool guilds_ok = false;
	if (guilds)
	{
		guilds_ok = game->reload_guild_progression();
		if (guilds_ok)
			hb::console::success("Guild progression reloaded");
	}

	// Send reload notification to clients first (shows top bar message)
	if (items || magic || skills || npcs || shops || formulas || colors || tiers_ok)
		game->send_config_reload_notification(items, magic, skills, npcs, formulas, colors, tiers_ok);

	// Reload remaining configs from database
	if (magic)           game->m_magic_manager->reload_magic_configs();
	if (skills)          game->m_skill_manager->reload_skill_configs();
	if (shops)           game->reload_shop_configs();
	if (colors)          game->reload_color_palette();

	// Stream updated config data to clients
	if (items || magic || skills || npcs || formulas || colors || tiers_ok)
		game->push_config_reload_to_clients(items, magic, skills, npcs, formulas, colors, tiers_ok);

	if ((tiers && !tiers_ok) || (guilds && !guilds_ok))
		hb::console::error("Reload complete with errors: {} (rejected candidate leaves the running config untouched, details in server log)", args);
	else
		hb::console::success("Reload complete: {}", args);
	hb::logger::log<hb::log_channel::commands>("reload {}", args);
}
