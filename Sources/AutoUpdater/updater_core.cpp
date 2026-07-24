#include "updater_core.h"
#include "UpdaterConstants.h"
#include "updater_http.h"
#include "updater_sha256.h"
#include "updater_file_ops.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace hb::updater
{
	server_endpoint& update_endpoint()
	{
		static server_endpoint endpoint{update_server_host, update_server_port};
		return endpoint;
	}

	bool load_endpoint_override(const std::string& dir)
	{
		std::ifstream file(fs::path(dir) / "update_override.txt");
		if (!file.is_open())
			return false;

		std::string host;
		int port = 0;
		file >> host >> port;
		if (host.empty() || port <= 0 || port > 65535)
			return false;

		update_endpoint() = {host, port};
		return true;
	}

	scan_status fetch_manifest(update_manifest& out)
	{
		const auto& endpoint = update_endpoint();
		std::string manifest_json;
		if (!http_get_text(endpoint.host.c_str(), endpoint.port, manifest_path, manifest_json))
			return scan_status::server_unreachable;

		if (!parse_manifest(manifest_json, out))
			return scan_status::error;

		return scan_status::update_available;
	}

	void build_plan(const update_manifest& manifest, const std::string& target_dir,
		update_plan& out)
	{
		out = {};
		out.version_major = manifest.version_major;
		out.version_minor = manifest.version_minor;
		out.version_patch = manifest.version_patch;

		for (const auto& entry : manifest.files)
		{
#ifdef _WIN32
			if (entry.platform == "linux") continue;
#else
			if (entry.platform == "windows") continue;
#endif

			std::string local_path = (fs::path(target_dir) / entry.path).string();
			if (sha256_file(local_path.c_str()) != entry.sha256)
			{
				out.total_bytes += entry.size;
				out.entries.push_back(entry);
			}
		}
	}

	scan_status scan(const std::string& target_dir, update_plan& out)
	{
		update_manifest manifest;
		scan_status status = fetch_manifest(manifest);
		if (status != scan_status::update_available)
			return status;

		build_plan(manifest, target_dir, out);
		return out.entries.empty() ? scan_status::up_to_date : scan_status::update_available;
	}

	apply_status apply(const update_plan& plan, const std::string& target_dir,
		exe_strategy strategy,
		const std::function<bool(const apply_progress&)>& on_progress)
	{
		if (plan.entries.empty())
			return apply_status::success;

		if (!ensure_staging_dir(target_dir))
			return apply_status::disperse_failed;

		std::string staging_base = (fs::path(target_dir) / staging_dir).string();
		int total = static_cast<int>(plan.entries.size());

		// Parallel download: workers pull the next file index from a shared
		// counter, each over its own keep-alive connection. Already-staged
		// files with matching hashes are skipped, so a re-apply after a
		// cancel or failure resumes where it left off.
		std::atomic<int> next_index{0};
		std::atomic<int> completed{0};
		std::atomic<bool> failed{false};
		std::atomic<bool> cancelled{false};

		auto worker = [&]()
		{
			const auto& endpoint = update_endpoint();
			http_client client(endpoint.host.c_str(), endpoint.port);
			for (;;)
			{
				if (failed.load() || cancelled.load())
					return;

				int i = next_index.fetch_add(1);
				if (i >= total)
					return;

				const auto& entry = plan.entries[i];
				std::string staged_path = (fs::path(staging_base) / entry.path).string();

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

		while (completed.load() < total && !failed.load())
		{
			if (!on_progress({apply_phase::downloading, completed.load(), total}))
			{
				cancelled.store(true);
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}

		for (auto& w : workers)
			w.join();

		if (cancelled.load())
			return apply_status::cancelled;       // staging kept for resume
		if (failed.load())
			return apply_status::download_failed; // staging kept for resume

		// Verify staged files
		int verified = 0;
		for (const auto& entry : plan.entries)
		{
			on_progress({apply_phase::verifying, verified, total});

			std::string staged_path = (fs::path(staging_base) / entry.path).string();
			if (sha256_file(staged_path.c_str()) != entry.sha256)
			{
				// Remove corrupt file so re-apply re-downloads it
				std::error_code ec;
				fs::remove(staged_path, ec);
				return apply_status::verify_failed;
			}
			++verified;
		}

		// Disperse into target_dir
		bool has_staged_exe = false;
		int dispersed = 0;
		for (const auto& entry : plan.entries)
		{
			on_progress({apply_phase::dispersing, dispersed, total});

			std::string staged_path = (fs::path(staging_base) / entry.path).string();
			std::string final_path = (fs::path(target_dir) / entry.path).string();

			bool stage_exe = entry.is_executable && strategy == exe_strategy::stage_for_swap;
			if (!disperse_file(staged_path, final_path, stage_exe))
				return apply_status::disperse_failed;

			if (stage_exe)
				has_staged_exe = true;
			++dispersed;
		}

		cleanup_staging(target_dir);
		return has_staged_exe ? apply_status::success_exe_staged : apply_status::success;
	}
}
