#include "launcher_app.h"
#include "launcher_spawn.h"

#include "UpdaterConstants.h"
#include "updater_file_ops.h"
#include "updater_sha256.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using namespace hb::updater;

namespace hb::launcher
{
	namespace
	{
		constexpr const char* version_file_name = "version.txt";

		std::string trim(const std::string& s)
		{
			size_t begin = s.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos)
				return {};
			size_t end = s.find_last_not_of(" \t\r\n");
			return s.substr(begin, end - begin + 1);
		}

		bool is_launcher_entry(const manifest_entry& entry)
		{
			return fs::path(entry.path).filename().string() == launcher_exe_name;
		}

		std::string format_size_mb(uint64_t bytes)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
			return buf;
		}
	}

	launcher_app::launcher_app(bool just_self_updated)
		: m_just_self_updated(just_self_updated)
	{
	}

	int launcher_app::run()
	{
		if (!m_gui.create())
			return 1;

		m_exe_dir = get_exe_directory();
		m_config = launcher_config::load(m_exe_dir);

		// Auto-detect: launcher dropped into an existing game folder
		if (m_config.install_dir.empty() && game_installed(m_exe_dir))
		{
			m_config.install_dir = m_exe_dir;
			m_config.save(m_exe_dir);
		}

		refresh();

		while (!m_close_requested)
		{
			m_gui.pump_messages();

			for (;;)
			{
				ui_event e = m_gui.poll_event();
				if (e == ui_event::none)
					break;

				switch (e)
				{
				case ui_event::closed:
					m_close_requested = true;
					break;

				case ui_event::primary_clicked:
					on_primary();
					break;

				case ui_event::browse_clicked:
				{
					std::string chosen = browse_for_folder(m_gui);
					if (!chosen.empty())
					{
						m_gui.set_path_text(chosen);
						m_confirm_nonempty = false;
					}
					break;
				}

				case ui_event::mode_cycled:
					m_settings.cycle_mode();
					m_gui.set_mode_label(m_settings.mode_label());
					if (game_installed(m_config.install_dir))
						m_settings.save(m_config.install_dir);
					break;

				case ui_event::resolution_cycled:
					m_settings.cycle_resolution();
					m_gui.set_resolution_label(m_settings.resolution_label());
					if (game_installed(m_config.install_dir))
						m_settings.save(m_config.install_dir);
					break;

				case ui_event::path_changed:
					m_confirm_nonempty = false;
					break;

				case ui_event::none:
					break;
				}

				// A state-changing action may have requested exit (Play)
				if (m_state == app_state::applying)
					break;
			}

			if (m_close_requested)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(15));
		}

		m_gui.destroy();
		return 0;
	}

	void launcher_app::refresh()
	{
		enter_state(app_state::checking);
		m_gui.set_status("Checking for updates...");
		m_gui.pump_messages();

		scan_status status = fetch_manifest(m_manifest);
		m_online = status == scan_status::update_available;

		read_installed_version();

		if (!m_online)
		{
			m_latest_version.clear();
			update_version_line();

			if (game_installed(m_config.install_dir))
			{
				enter_state(app_state::offline_ready);
				m_gui.set_status(status == scan_status::server_unreachable
					? "Offline - could not reach the update server."
					: "Update check failed - you can still play.");
			}
			else
			{
				enter_state(app_state::server_down);
				m_gui.set_status("Could not reach the update server.");
			}
			return;
		}

		m_latest_version = std::to_string(m_manifest.version_major) + "."
			+ std::to_string(m_manifest.version_minor) + "."
			+ std::to_string(m_manifest.version_patch);
		update_version_line();

		self_update_check();

		if (!game_installed(m_config.install_dir))
		{
			enter_state(app_state::not_installed);
			return;
		}

		build_plan(m_manifest, m_config.install_dir, m_plan);
		std::erase_if(m_plan.entries, is_launcher_entry);

		if (m_plan.entries.empty())
		{
			enter_state(app_state::ready);
		}
		else
		{
			enter_state(app_state::update_available);
			char buf[128];
			std::snprintf(buf, sizeof(buf), "Update available - %d file%s, %s.",
				static_cast<int>(m_plan.entries.size()),
				m_plan.entries.size() == 1 ? "" : "s",
				format_size_mb(m_plan.total_bytes).c_str());
			m_gui.set_status(buf);
		}
	}

	void launcher_app::self_update_check()
	{
		if (m_just_self_updated)
			return;

		std::string own_exe = get_exe_path();
		for (const auto& entry : m_manifest.files)
		{
#ifdef _WIN32
			if (entry.platform == "linux") continue;
#else
			if (entry.platform == "windows") continue;
#endif
			if (!is_launcher_entry(entry))
				continue;

			if (sha256_file(own_exe.c_str()) == entry.sha256)
				return; // launcher is current

			m_gui.set_status("Updating launcher...");
			m_gui.show_progress(true);
			m_gui.pump_messages();

			update_plan self_plan;
			self_plan.entries.push_back(entry);
			self_plan.total_bytes = entry.size;

			auto on_progress = [&](const apply_progress&) -> bool
			{
				m_gui.pump_messages();
				return true; // self-update is quick; not cancellable
			};

			apply_status result = apply(self_plan, m_exe_dir,
				exe_strategy::stage_for_swap, on_progress);
			m_gui.show_progress(false);

			if (result == apply_status::success_exe_staged || result == apply_status::success)
				relaunch(); // next run swaps .update into place; does not return

			m_gui.set_status("Launcher self-update failed - continuing.");
			return;
		}
	}

	void launcher_app::enter_state(app_state state)
	{
		m_state = state;

		switch (state)
		{
		case app_state::checking:
		case app_state::applying:
			m_gui.show_install_row(false);
			m_gui.show_options_row(false);
			m_gui.set_primary("", false, false);
			m_gui.show_progress(state == app_state::applying);
			break;

		case app_state::not_installed:
			m_gui.show_progress(false);
			m_gui.show_options_row(false);
			m_gui.show_install_row(true);
			m_gui.set_path_text(!m_config.install_dir.empty()
				? m_config.install_dir : default_install_dir());
			m_gui.set_primary("Install", true, true);
			m_gui.set_status("Game is not installed. Choose a folder and click Install.");
			break;

		case app_state::update_available:
			m_gui.show_progress(false);
			m_gui.show_install_row(false);
			m_settings = launch_settings::load(m_config.install_dir);
			m_gui.set_mode_label(m_settings.mode_label());
			m_gui.set_resolution_label(m_settings.resolution_label());
			m_gui.show_options_row(true);
			m_gui.set_primary("Update", true, true);
			break;

		case app_state::ready:
		case app_state::offline_ready:
			m_gui.show_progress(false);
			m_gui.show_install_row(false);
			m_settings = launch_settings::load(m_config.install_dir);
			m_gui.set_mode_label(m_settings.mode_label());
			m_gui.set_resolution_label(m_settings.resolution_label());
			m_gui.show_options_row(true);
			m_gui.set_primary("Play Game", true, true);
			if (state == app_state::ready)
				m_gui.set_status("Ready to play.");
			break;

		case app_state::server_down:
			m_gui.show_progress(false);
			m_gui.show_install_row(false);
			m_gui.show_options_row(false);
			m_gui.set_primary("Retry", true, true);
			break;
		}
	}

	void launcher_app::on_primary()
	{
		switch (m_state)
		{
		case app_state::not_installed:    do_install(); break;
		case app_state::update_available: do_apply(false); break;
		case app_state::ready:
		case app_state::offline_ready:    start_game(); break;
		case app_state::server_down:      refresh(); break;
		case app_state::checking:
		case app_state::applying:         break;
		}
	}

	void launcher_app::do_install()
	{
		std::string dir = trim(m_gui.path_text());
		if (dir.empty())
		{
			m_gui.set_status("Enter an install folder first.");
			return;
		}

		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec)
		{
			m_gui.set_status("Could not create that folder - check the path.");
			return;
		}

		// Writability probe
		auto probe = fs::path(dir) / ".hbmt_write_probe";
		{
			std::ofstream f(probe);
			if (!f.is_open())
			{
				m_gui.set_status("That folder is not writable - choose another.");
				return;
			}
		}
		fs::remove(probe, ec);

		// Non-empty folder without a game in it: ask for a second click
		if (!game_installed(dir) && !fs::is_empty(dir, ec) && !m_confirm_nonempty)
		{
			m_confirm_nonempty = true;
			m_gui.set_status("Folder is not empty - click Install again to use it anyway.");
			return;
		}
		m_confirm_nonempty = false;

		m_config.install_dir = dir;
		m_config.save(m_exe_dir);

		build_plan(m_manifest, dir, m_plan);
		std::erase_if(m_plan.entries, is_launcher_entry);

		do_apply(true);
	}

	void launcher_app::do_apply(bool installing)
	{
		app_state return_state = m_state;
		enter_state(app_state::applying);

		const char* verb = installing ? "Installing" : "Updating";

		auto on_progress = [&](const apply_progress& p) -> bool
		{
			switch (p.phase)
			{
			case apply_phase::downloading:
			{
				char buf[128];
				std::snprintf(buf, sizeof(buf), "%s - downloading file %d of %d...",
					verb, p.files_done + 1 <= p.files_total ? p.files_done + 1 : p.files_total,
					p.files_total);
				m_gui.set_status(buf);
				m_gui.set_progress(p.files_total > 0
					? static_cast<float>(p.files_done) / static_cast<float>(p.files_total)
					: 0.0f);
				break;
			}
			case apply_phase::verifying:
				m_gui.set_status("Verifying files...");
				m_gui.set_progress(1.0f);
				break;
			case apply_phase::dispersing:
				m_gui.set_status("Applying files...");
				break;
			}

			m_gui.pump_messages();

			for (;;)
			{
				ui_event e = m_gui.poll_event();
				if (e == ui_event::none)
					break;
				if (e == ui_event::closed)
				{
					m_close_requested = true;
					return false; // cancel; staging is kept for resume
				}
			}
			return true;
		};

		apply_status result = apply(m_plan, m_config.install_dir,
			exe_strategy::write_direct, on_progress);

		switch (result)
		{
		case apply_status::success:
		case apply_status::success_exe_staged:
			write_version_file();
			m_installed_version = m_latest_version;
			update_version_line();
			m_settings.save(m_config.install_dir);
			enter_state(app_state::ready);
			m_gui.set_status(installing ? "Install complete - ready to play." : "Update complete - ready to play.");
			break;

		case apply_status::cancelled:
			// Window closed mid-apply; staging kept, next run resumes
			break;

		case apply_status::download_failed:
			enter_state(return_state);
			m_gui.set_status("Download failed - check your connection and try again.");
			break;

		case apply_status::verify_failed:
			enter_state(return_state);
			m_gui.set_status("A downloaded file was corrupt - try again.");
			break;

		case apply_status::disperse_failed:
			enter_state(return_state);
			m_gui.set_status("Could not write game files - is the game still running?");
			break;
		}
	}

	void launcher_app::start_game()
	{
		m_settings.save(m_config.install_dir);

		if (spawn_game(m_config.install_dir))
			m_close_requested = true;
		else
			m_gui.set_status("Could not start the game - check the install folder.");
	}

	bool launcher_app::game_installed(const std::string& dir) const
	{
		return !dir.empty() && fs::exists(fs::path(dir) / game_exe_name);
	}

	std::string launcher_app::default_install_dir() const
	{
#ifdef _WIN32
		const char* local_app_data = std::getenv("LOCALAPPDATA");
		if (local_app_data)
			return (fs::path(local_app_data) / "Helbreath Medieval Times").string();
		return "C:\\Helbreath Medieval Times";
#else
		const char* home = std::getenv("HOME");
		if (home)
			return (fs::path(home) / "Games" / "helbreath-mt").string();
		return "./helbreath-mt";
#endif
	}

	void launcher_app::read_installed_version()
	{
		m_installed_version.clear();
		if (m_config.install_dir.empty())
			return;

		std::ifstream file(fs::path(m_config.install_dir) / version_file_name);
		if (file.is_open())
		{
			std::string line;
			std::getline(file, line);
			m_installed_version = trim(line);
		}
	}

	void launcher_app::write_version_file() const
	{
		std::ofstream file(fs::path(m_config.install_dir) / version_file_name);
		if (file.is_open())
			file << m_latest_version << "\n";
	}

	void launcher_app::update_version_line()
	{
		std::string line;
		if (!m_installed_version.empty())
			line = "Installed: " + m_installed_version;
		if (!m_latest_version.empty())
		{
			if (!line.empty())
				line += "   |   ";
			line += "Latest: " + m_latest_version;
		}
		m_gui.set_version_line(line);
	}
}
