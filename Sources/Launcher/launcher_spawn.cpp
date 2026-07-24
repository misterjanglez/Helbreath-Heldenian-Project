#include "launcher_spawn.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hb::launcher
{
	bool spawn_game(const std::string& install_dir)
	{
		std::string exe_path = (fs::path(install_dir) / game_exe_name).string();
		if (!fs::exists(exe_path))
			return false;

#ifdef _WIN32
		// Child inherits the parent environment
		SetEnvironmentVariableA("HB_LAUNCHED_BY_LAUNCHER", "1");

		STARTUPINFOA si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};

		if (!CreateProcessA(exe_path.c_str(), nullptr, nullptr, nullptr, FALSE,
			0, nullptr, install_dir.c_str(), &si, &pi))
		{
			return false;
		}

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
#else
		pid_t pid = fork();
		if (pid < 0)
			return false;

		if (pid == 0)
		{
			// Child: run the game from the install directory
			setenv("HB_LAUNCHED_BY_LAUNCHER", "1", 1);
			if (chdir(install_dir.c_str()) != 0)
				_exit(1);
			execl(exe_path.c_str(), exe_path.c_str(), nullptr);
			_exit(1); // exec failed
		}

		return true;
#endif
	}
}
