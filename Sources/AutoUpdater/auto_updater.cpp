#include "auto_updater.h"
#include "UpdaterConstants.h"
#include "updater_manifest.h"
#include "updater_http.h"
#include "updater_sha256.h"
#include "updater_file_ops.h"
#include "updater_gui.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace hb::updater
{
	update_result check_for_updates()
	{
		std::string exe_path = get_exe_path();
		std::string base_dir = get_exe_directory();

		// Phase 0 — pending exe swap from last run
		if (apply_pending_exe_swap(exe_path))
		{
			cleanup_old_exe(exe_path);
			relaunch(); // does not return
		}

		// Clean up .old if left over
		cleanup_old_exe(exe_path);

		// Phase 1 — fetch manifest
		std::string manifest_json;
		while (!http_get_text(update_server_host, update_server_port, manifest_path, manifest_json))
		{
			if (!show_retry_dialog("Could not reach the update server."))
				return update_result::server_unreachable;
		}

		update_manifest manifest;
		if (!parse_manifest(manifest_json, manifest))
			return update_result::error;

		// Phase 2 — compare local files vs manifest
		struct changed_file
		{
			const manifest_entry* entry;
		};
		std::vector<changed_file> changed_files;

		for (const auto& entry : manifest.files)
		{
#ifdef _WIN32
			if (entry.platform == "linux") continue;
#else
			if (entry.platform == "windows") continue;
#endif

			std::string local_path = (fs::path(base_dir) / entry.path).string();
			std::string local_hash = sha256_file(local_path.c_str());

			if (local_hash != entry.sha256)
				changed_files.push_back({&entry});
		}

		if (changed_files.empty())
			return update_result::no_update;

		// Phase 3 — download changed files to staging
		updater_gui gui;
		gui.create();

		if (!ensure_staging_dir(base_dir))
		{
			gui.destroy();
			return update_result::error;
		}

		std::string staging_base = (fs::path(base_dir) / staging_dir).string();
		int total = static_cast<int>(changed_files.size());

		// Parallel download: workers pull the next file index from a shared
		// counter, each over its own keep-alive connection. The main thread
		// only pumps the GUI. On persistent failure the user chooses retry or
		// skip; already-staged files are skipped on retry (hash check below),
		// so no progress is lost.
		for (;;)
		{
			std::atomic<int> next_index{0};
			std::atomic<int> completed{0};
			std::atomic<bool> failed{false};
			std::atomic<bool> cancelled{false};

			auto worker = [&]()
			{
				http_client client(update_server_host, update_server_port);
				for (;;)
				{
					if (failed.load() || cancelled.load())
						return;

					int i = next_index.fetch_add(1);
					if (i >= total)
						return;

					const auto& entry = *changed_files[i].entry;
					std::string staged_path = (fs::path(staging_base) / entry.path).string();

					// Resume: skip files already staged with correct hash
					if (sha256_file(staged_path.c_str()) == entry.sha256)
					{
						completed.fetch_add(1);
						continue;
					}

					std::string url = "/" + entry.path;
					if (!client.download_file(url.c_str(), staged_path))
					{
						failed.store(true);
						return;
					}

					completed.fetch_add(1);
				}
			};

			int worker_count = std::min(total, parallel_downloads);
			std::vector<std::thread> workers;
			for (int t = 0; t < worker_count; ++t)
				workers.emplace_back(worker);

			int last_done = -1;
			while (completed.load() < total && !failed.load())
			{
				if (gui.is_cancelled())
				{
					cancelled.store(true);
					break;
				}

				int done = completed.load();
				if (done != last_done)
				{
					last_done = done;
					char status_buf[256];
					std::snprintf(status_buf, sizeof(status_buf),
						"Downloading files (%d of %d)...", done, total);
					gui.set_status(status_buf);
					gui.set_progress(static_cast<float>(done) / static_cast<float>(total));
				}
				gui.pump_messages();
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
			}

			for (auto& w : workers)
				w.join();

			if (cancelled.load())
			{
				gui.destroy();
				return update_result::error;
			}

			if (!failed.load())
				break;

			if (!show_retry_dialog("The update download failed."))
			{
				gui.destroy();
				return update_result::error;
			}
		}

		// Phase 4 — verify staged files
		gui.set_status("Verifying files...");
		gui.set_progress(1.0f);
		gui.pump_messages();

		for (const auto& cf : changed_files)
		{
			const auto& entry = *cf.entry;
			std::string staged_path = (fs::path(staging_base) / entry.path).string();
			std::string hash = sha256_file(staged_path.c_str());

			if (hash != entry.sha256)
			{
				// Remove corrupt file so next run re-downloads it
				std::error_code ec;
				fs::remove(staged_path, ec);
				gui.destroy();
				return update_result::error;
			}
		}

		// Phase 5 — disperse files
		bool has_exe_update = false;
		gui.set_status("Applying update...");
		gui.pump_messages();

		for (const auto& cf : changed_files)
		{
			const auto& entry = *cf.entry;
			std::string staged_path = (fs::path(staging_base) / entry.path).string();
			std::string final_path = (fs::path(base_dir) / entry.path).string();

			if (!disperse_file(staged_path, final_path, entry.is_executable))
			{
				cleanup_staging(base_dir);
				gui.destroy();
				return update_result::error;
			}

			if (entry.is_executable)
				has_exe_update = true;
		}

		cleanup_staging(base_dir);
		gui.destroy();

		// Phase 6
		if (has_exe_update)
			return update_result::restart_required;

		return update_result::updated;
	}
}
