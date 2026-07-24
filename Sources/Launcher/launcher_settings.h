#pragma once
#include <array>
#include <string>

// Read-modify-write of the CLIENT's <install_dir>/settings.json. The client
// owns that file; the launcher touches only the four display keys below and
// preserves everything else byte-for-byte at the JSON level.

namespace hb::launcher
{
	enum class launch_mode
	{
		windowed,
		borderless,
		fullscreen
	};

	struct display_resolution
	{
		int width;
		int height;
	};

	// Keep in sync with s_ValidResolutions in Sources/Client/ConfigManager.cpp —
	// the client snaps unknown sizes to this list anyway.
	inline constexpr std::array<display_resolution, 5> valid_resolutions{{
		{800, 600}, {1024, 768}, {1280, 960}, {1440, 1080}, {1920, 1440}
	}};

	struct launch_settings
	{
		launch_mode mode = launch_mode::windowed;
		int resolution_index = 0;   // into valid_resolutions

		const char* mode_label() const;
		std::string resolution_label() const;

		void cycle_mode();
		void cycle_resolution();

		// Read the display keys from <install_dir>/settings.json (defaults if
		// absent/corrupt).
		static launch_settings load(const std::string& install_dir);

		// Write only display.fullscreen / display.borderless / window.width /
		// window.height back, preserving all other keys. Saves a .bak of the
		// previous file first.
		bool save(const std::string& install_dir) const;
	};
}
