#pragma once
#include "updater_manifest.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// GUI-free scan/apply engine. scan() answers "is target_dir up to date?"
// without touching anything; apply() downloads and disperses a plan.
// Callers own all UI, retry policy, and relaunch decisions.

namespace hb::updater
{
	struct update_plan
	{
		std::vector<manifest_entry> entries;    // changed files only, platform-filtered, by value
		uint64_t total_bytes = 0;               // sum of entry sizes
		int version_major = 0;                  // manifest version (display only)
		int version_minor = 0;
		int version_patch = 0;
	};

	enum class scan_status
	{
		up_to_date,
		update_available,
		server_unreachable,
		error
	};

	// The update server endpoint, defaulting to the UpdaterConstants values.
	// load_endpoint_override() replaces it from <dir>/update_override.txt
	// ("host port" on one line) when that file exists — the staging/testing
	// analogue of the game's server_override.txt. Returns true if applied.
	struct server_endpoint
	{
		std::string host;
		int port;
	};
	server_endpoint& update_endpoint();
	bool load_endpoint_override(const std::string& dir);

	// Fetch and parse the manifest. Returns update_available on success (the
	// caller decides what "up to date" means for its target). One attempt.
	scan_status fetch_manifest(update_manifest& out);

	// SHA-compare a manifest against target_dir into a plan. A missing local
	// file counts as changed, so an empty directory yields the full manifest
	// as the plan (= fresh install).
	void build_plan(const update_manifest& manifest, const std::string& target_dir,
		update_plan& out);

	// fetch_manifest + build_plan in one call, no dialogs.
	scan_status scan(const std::string& target_dir, update_plan& out);

	enum class exe_strategy
	{
		stage_for_swap,     // executables land as <path>.update, swapped on next launch
		                    // (in-process update: client self-update, launcher self-update)
		write_direct        // executables written in place (target process not running)
	};

	enum class apply_phase
	{
		downloading,
		verifying,
		dispersing
	};

	struct apply_progress
	{
		apply_phase phase;
		int files_done;
		int files_total;
	};

	enum class apply_status
	{
		success,            // everything dispersed, no exe staged
		success_exe_staged, // success, but >=1 executable staged for next-launch swap
		cancelled,          // progress callback returned false; staging kept for resume
		download_failed,    // network failure; staging kept — re-apply resumes
		verify_failed,      // corrupt staged file removed; re-apply re-downloads it
		disperse_failed     // couldn't write a final file (e.g. target exe running)
	};

	// Download plan entries into <target_dir>/updates/, verify hashes, then
	// disperse into target_dir. on_progress is called from the waiting thread
	// (~30ms cadence); return false to cancel. Staging is removed only on
	// success so interrupted runs resume cheaply.
	apply_status apply(const update_plan& plan, const std::string& target_dir,
		exe_strategy strategy,
		const std::function<bool(const apply_progress&)>& on_progress);
}
