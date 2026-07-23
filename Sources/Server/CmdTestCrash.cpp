#include "CmdTestCrash.h"
#include "ServerConsole.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include "StringCompat.h"

void CmdTestCrash::execute(CGame* game, const char* args)
{
	if (args == nullptr || hb_stricmp(args, "confirm") != 0)
	{
		hb::console::warn("testcrash: this will CRASH the server without saving players.");
		hb::console::warn("Run 'saveall' first, then 'testcrash confirm' to proceed.");
		return;
	}

	hb::logger::warn("testcrash: deliberate crash requested from console");
	hb::console::warn("Crashing now - the report uploads on next server start.");

	volatile int* null_pointer = nullptr;
	*null_pointer = 42;
}
