#include "launcher_settings.h"

#include "json.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace hb::launcher
{
	namespace
	{
		constexpr const char* settings_file_name = "settings.json";
	}

	const char* launch_settings::mode_label() const
	{
		switch (mode)
		{
		case launch_mode::borderless: return "Borderless";
		case launch_mode::fullscreen: return "Fullscreen";
		case launch_mode::windowed:
		default:                      return "Windowed";
		}
	}

	std::string launch_settings::resolution_label() const
	{
		const auto& r = valid_resolutions[resolution_index];
		return std::to_string(r.width) + "x" + std::to_string(r.height);
	}

	void launch_settings::cycle_mode()
	{
		switch (mode)
		{
		case launch_mode::windowed:   mode = launch_mode::borderless; break;
		case launch_mode::borderless: mode = launch_mode::fullscreen; break;
		case launch_mode::fullscreen: mode = launch_mode::windowed; break;
		}
	}

	void launch_settings::cycle_resolution()
	{
		resolution_index = (resolution_index + 1) % static_cast<int>(valid_resolutions.size());
	}

	launch_settings launch_settings::load(const std::string& install_dir)
	{
		launch_settings settings;

		std::ifstream file(fs::path(install_dir) / settings_file_name);
		if (!file.is_open())
			return settings;

		try
		{
			nlohmann::json j = nlohmann::json::parse(file);

			bool fullscreen = false;
			bool borderless = false;
			if (j.contains("display"))
			{
				const auto& d = j["display"];
				if (d.contains("fullscreen")) fullscreen = d["fullscreen"].get<bool>();
				if (d.contains("borderless")) borderless = d["borderless"].get<bool>();
			}
			settings.mode = fullscreen ? launch_mode::fullscreen
				: borderless ? launch_mode::borderless
				: launch_mode::windowed;

			if (j.contains("window"))
			{
				const auto& w = j["window"];
				int width = w.contains("width") ? w["width"].get<int>() : 0;
				int height = w.contains("height") ? w["height"].get<int>() : 0;
				for (int i = 0; i < static_cast<int>(valid_resolutions.size()); ++i)
				{
					if (valid_resolutions[i].width == width && valid_resolutions[i].height == height)
					{
						settings.resolution_index = i;
						break;
					}
				}
			}
		}
		catch (...)
		{
			settings = {};
		}

		return settings;
	}

	bool launch_settings::save(const std::string& install_dir) const
	{
		auto path = fs::path(install_dir) / settings_file_name;

		// Preserve every key the client owns: start from the existing file
		nlohmann::json j = nlohmann::json::object();
		{
			std::ifstream file(path);
			if (file.is_open())
			{
				try
				{
					j = nlohmann::json::parse(file);
					if (!j.is_object())
						j = nlohmann::json::object();
				}
				catch (...)
				{
					j = nlohmann::json::object();
				}
			}
		}

		// Backup the previous file before rewriting
		if (fs::exists(path))
		{
			std::error_code ec;
			fs::copy_file(path, path.string() + ".bak", fs::copy_options::overwrite_existing, ec);
		}

		j["display"]["fullscreen"] = mode == launch_mode::fullscreen;
		j["display"]["borderless"] = mode == launch_mode::borderless;
		j["window"]["width"] = valid_resolutions[resolution_index].width;
		j["window"]["height"] = valid_resolutions[resolution_index].height;

		std::ofstream file(path);
		if (!file.is_open())
			return false;

		file << j.dump(2) << "\n";
		return static_cast<bool>(file);
	}
}
