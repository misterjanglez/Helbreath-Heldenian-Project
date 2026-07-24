#pragma once
#include <string>

namespace hb::launcher
{
	// Game executable basename inside the install directory (per platform).
#ifdef _WIN32
	constexpr const char* game_exe_name = "Game_x64_win.exe";
#else
	constexpr const char* game_exe_name = "Game_x64_linux";
#endif

	// Launch the game from install_dir (working directory = install_dir) with
	// HB_LAUNCHED_BY_LAUNCHER=1 set so the client skips its embedded update
	// check. Returns false if the process could not be started.
	bool spawn_game(const std::string& install_dir);
}
