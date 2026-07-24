#include "auto_updater.h"
#include "UpdaterConstants.h"
#include "updater_core.h"
#include "updater_file_ops.h"
#include "updater_gui.h"
#include <cstdio>

// Thin in-client wrapper over the scan/apply core (updater_core.h): silent
// pre-launch check with the classic progress window. The standalone launcher
// drives the same core with its own UI and target directory.

namespace hb::updater
{
	update_result check_for_updates()
	{
		std::string exe_path = get_exe_path();
		std::string base_dir = get_exe_directory();

		// Pending exe swap from last run
		if (apply_pending_exe_swap(exe_path))
		{
			cleanup_old_exe(exe_path);
			relaunch(); // does not return
		}

		// Clean up .old if left over
		cleanup_old_exe(exe_path);

		// Staging/testing endpoint override (update_override.txt beside exe)
		load_endpoint_override(base_dir);

		// Scan, with the classic retry dialog on connectivity failure
		update_plan plan;
		for (;;)
		{
			scan_status status = scan(base_dir, plan);
			if (status == scan_status::up_to_date)
				return update_result::no_update;
			if (status == scan_status::error)
				return update_result::error;
			if (status == scan_status::update_available)
				break;

			// server_unreachable
			if (!show_retry_dialog("Could not reach the update server."))
				return update_result::server_unreachable;
		}

		// Apply with the classic progress window; the in-process client uses
		// stage-for-swap so its own exe is swapped on the next launch.
		updater_gui gui;
		gui.create();

		auto on_progress = [&](const apply_progress& p) -> bool
		{
			switch (p.phase)
			{
			case apply_phase::downloading:
			{
				char status_buf[256];
				std::snprintf(status_buf, sizeof(status_buf),
					"Downloading files (%d of %d)...", p.files_done, p.files_total);
				gui.set_status(status_buf);
				gui.set_progress(p.files_total > 0
					? static_cast<float>(p.files_done) / static_cast<float>(p.files_total)
					: 0.0f);
				break;
			}
			case apply_phase::verifying:
				gui.set_status("Verifying files...");
				gui.set_progress(1.0f);
				break;
			case apply_phase::dispersing:
				gui.set_status("Applying update...");
				break;
			}
			gui.pump_messages();
			return !gui.is_cancelled();
		};

		for (;;)
		{
			apply_status status = apply(plan, base_dir, exe_strategy::stage_for_swap, on_progress);

			switch (status)
			{
			case apply_status::success:
				gui.destroy();
				return update_result::updated;

			case apply_status::success_exe_staged:
				gui.destroy();
				return update_result::restart_required;

			case apply_status::download_failed:
				// Staged files are kept — retry resumes where it left off
				if (show_retry_dialog("The update download failed."))
					continue;
				gui.destroy();
				return update_result::error;

			case apply_status::disperse_failed:
				cleanup_staging(base_dir);
				gui.destroy();
				return update_result::error;

			case apply_status::cancelled:
			case apply_status::verify_failed:
				gui.destroy();
				return update_result::error;
			}
		}
	}
}
