#pragma once
#include "launcher_config.h"
#include "launcher_gui.h"
#include "launcher_settings.h"

#include "updater_core.h"
#include "updater_manifest.h"

#include <string>

// The launcher's state machine: check first, then offer exactly one primary
// action — Play Game (up to date), Update (changes found), Install (no game
// on disk), or Retry (server unreachable, nothing installed).

namespace hb::launcher
{
	class launcher_app
	{
	public:
		// just_self_updated: a pending .update swap was applied at startup this
		// run — the self-update check is skipped once so a bad server-side
		// launcher upload cannot cause a relaunch loop.
		explicit launcher_app(bool just_self_updated);

		int run();

	private:
		enum class app_state
		{
			checking,
			not_installed,      // Install CTA + path row
			update_available,   // Update CTA
			ready,              // Play Game CTA
			offline_ready,      // Play Game CTA, server unreachable
			server_down,        // Retry CTA, nothing installed
			applying            // progress bar, no CTA
		};

		void refresh();
		void self_update_check();
		void enter_state(app_state state);
		void on_primary();
		void do_install();
		void do_apply(bool installing);
		void start_game();

		bool game_installed(const std::string& dir) const;
		std::string default_install_dir() const;
		void read_installed_version();
		void write_version_file() const;
		void update_version_line();

		launcher_gui m_gui;
		launcher_config m_config;
		launch_settings m_settings;

		hb::updater::update_manifest m_manifest;
		hb::updater::update_plan m_plan;

		app_state m_state = app_state::checking;
		std::string m_exe_dir;
		std::string m_latest_version;
		std::string m_installed_version;
		bool m_online = false;
		bool m_just_self_updated = false;
		bool m_confirm_nonempty = false;
		bool m_close_requested = false;
	};
}
