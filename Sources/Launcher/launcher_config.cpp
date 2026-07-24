#include "launcher_config.h"

#include "json.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace hb::launcher
{
	namespace
	{
		constexpr const char* config_file_name = "launcher.json";
	}

	launcher_config launcher_config::load(const std::string& exe_dir)
	{
		launcher_config config;

		std::ifstream file(fs::path(exe_dir) / config_file_name);
		if (!file.is_open())
			return config;

		try
		{
			nlohmann::json j = nlohmann::json::parse(file);
			if (j.contains("install_dir") && j["install_dir"].is_string())
				config.install_dir = j["install_dir"].get<std::string>();
		}
		catch (...)
		{
			// Corrupt config: fall back to defaults
			config = {};
		}

		return config;
	}

	bool launcher_config::save(const std::string& exe_dir) const
	{
		nlohmann::json j;
		j["install_dir"] = install_dir;

		std::ofstream file(fs::path(exe_dir) / config_file_name);
		if (!file.is_open())
			return false;

		file << j.dump(2) << "\n";
		return static_cast<bool>(file);
	}
}
