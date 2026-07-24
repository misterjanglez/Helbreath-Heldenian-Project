#pragma once
#include <string>

// launcher.json beside the launcher executable — the launcher's own
// persistence (never part of the update manifest or the installer).

namespace hb::launcher
{
	struct launcher_config
	{
		std::string install_dir;

		// Load from <exe_dir>/launcher.json; absent or corrupt file yields
		// defaults (never throws).
		static launcher_config load(const std::string& exe_dir);

		bool save(const std::string& exe_dir) const;
	};
}
