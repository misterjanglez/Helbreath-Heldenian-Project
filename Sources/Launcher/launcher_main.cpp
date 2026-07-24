#include "launcher_app.h"

#include "updater_core.h"
#include "updater_file_ops.h"

#include <filesystem>

// Entry point: apply any pending self-update swap, then run the launcher
// state machine (launcher_app).

namespace hb::launcher
{
	int launcher_main()
	{
		std::string exe_path = hb::updater::get_exe_path();

		// A pending .update means a self-update was staged last run
		if (hb::updater::apply_pending_exe_swap(exe_path))
		{
			hb::updater::cleanup_old_exe(exe_path);
			hb::updater::relaunch(); // does not return
		}

		// A leftover .old means the swap happened at the start of THIS process
		// tree — skip the self-update check once so a bad server-side launcher
		// upload cannot cause a relaunch loop.
		bool just_self_updated = std::filesystem::exists(exe_path + ".old");
		hb::updater::cleanup_old_exe(exe_path);

		// Staging/testing: update_override.txt beside the launcher redirects
		// the update server (same pattern as the game's server_override.txt)
		hb::updater::load_endpoint_override(hb::updater::get_exe_directory());

		launcher_app app(just_self_updated);
		return app.run();
	}
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return hb::launcher::launcher_main();
}
#else
int main()
{
	return hb::launcher::launcher_main();
}
#endif
