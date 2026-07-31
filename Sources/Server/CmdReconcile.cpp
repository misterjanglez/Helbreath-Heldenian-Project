// CmdReconcile.cpp: the Reconciliation job (#83, plan P4.1)
//
// Two responsibilities, and neither of them is the comparison — that lives in
// ItemReconciliation.cpp, on two raw handles, so the prover can drive it against
// planted anomalies without a world.
//
//   1. **Decide what to read.** With no arguments this is the live pair, and
//      "live" means the ledger's RAM buffer has to reach disk first or the
//      report accuses the world of losing the events that are still in flight.
//      With paths it is a snapshot pair, opened read-only and touched no other
//      way — a copy of a running server's files is a valid target, which is the
//      reader-facing half of what WAL was chosen for (D4).
//
//   2. **Say what was found, honestly.** A live world produces holder
//      mismatches by design: a logged-in character's inventory is in memory
//      until it saves. That is why the classes are split by severity rather
//      than summed, and why an online player count is part of the report
//      instead of a footnote.
//
//////////////////////////////////////////////////////////////////////

#include "CmdReconcile.h"

#include "Game.h"
#include "GameDatabase.h"
#include "GmMintSpec.h"           // split_args
#include "ItemLedgerStore.h"      // default_ledger_path
#include "ItemReconciliation.h"
#include "Item.h"
#include "LedgerTools.h"          // read_only_db, same_file, flush_if_live_ledger
#include "Log.h"
#include "ServerConsole.h"
#include "ServerLogChannels.h"

#include <cstdlib>
#include <format>
#include <string>

using hb::log_channel;
namespace reconcile_class = hb::server::reconcile_class;

namespace
{
	constexpr const char* default_world_db = "game.db";

	int count_online(CGame* game)
	{
		int online = 0;
		for (int i = 1; i < hb::server::config::MaxClients; i++) {
			if (game->m_client_list[i] != nullptr) online++;
		}
		return online;
	}
}

void CmdReconcile::execute(CGame* game, const char* args)
{
	// Paths first, then the limit: an argument that parses as a number is the
	// sample cap, anything else is a path. Positional-with-a-type rather than
	// flags, because every other console command here takes bare tokens and a
	// lone `reconcile 0` (show everything, live world) is the shape an operator
	// reaches for first.
	std::string world_path = default_world_db;
	std::string ledger_path = hb::server::default_ledger_path;
	size_t sample_limit = 20;
	int paths_given = 0;

	for (const std::string& token : hb::server::split_args(args)) {
		char* end = nullptr;
		const long value = std::strtol(token.c_str(), &end, 10);
		if (end != nullptr && *end == '\0' && !token.empty() && value >= 0) {
			sample_limit = static_cast<size_t>(value);
			continue;
		}
		if (paths_given == 0) world_path = token;
		else if (paths_given == 1) ledger_path = token;
		else {
			hb::console::error("reconcile: too many paths - usage is [game_db] [ledger_db] [limit].");
			return;
		}
		paths_given++;
	}

	if (paths_given == 1) {
		hb::console::error("reconcile: name both files or neither - a Game DB read against the "
			"wrong ledger reports every item as unknown.");
		return;
	}

	const bool live_world = hb::server::same_file(world_path, hb::server::game_db().path());
	if (const size_t pending = hb::server::flush_if_live_ledger(game, ledger_path); pending > 0) {
		hb::console::info("reconcile: flushed {} buffered ledger event(s) before reading.",
			pending);
	}

	hb::server::read_only_db world(world_path);
	if (world.get() == nullptr) {
		hb::console::error("reconcile: cannot open '{}' - {}.", world_path, world.error());
		return;
	}
	hb::server::read_only_db ledger(ledger_path);
	if (ledger.get() == nullptr) {
		hb::console::error("reconcile: cannot open '{}' - {}.", ledger_path, ledger.error());
		return;
	}

	hb::server::reconcile_options options;
	options.sample_limit = sample_limit;
	// Stackability comes from the loaded item configs, which is the same table
	// the save path read when it decided whether to write a Serial at all.
	options.is_instanced = hb::server::instanced_item_lookup(*game);

	const hb::server::reconcile_report report =
		hb::server::run_reconciliation(world.get(), ledger.get(), options);

	if (!report.ok) {
		hb::console::error("reconcile: {}.", report.failure);
		return;
	}

	// --- The report -------------------------------------------------------
	hb::console::write("");
	hb::console::info("Reconciliation: {} vs {}", world_path, ledger_path);
	hb::console::write(std::format(
		"  scanned {} held row(s), {} distinct Serial(s), against {} birth row(s) (high-water {})",
		report.held_rows, report.held_serials, report.instances, report.high_water));

	if (live_world) {
		const int online = count_online(game);
		if (online > 0) {
			hb::console::warn("  {} player(s) online: their carried items are in memory, not in "
				"this file. Run `saveall` first for a clean comparison.", online);
		}
	}

	for (int class_id = 0; class_id < reconcile_class::count; class_id++) {
		const int64_t found = report.class_counts[class_id];
		hb::console::write(std::format("RECONCILE CLASS {} {}",
			hb::server::reconcile_class_name(class_id), found));
	}
	for (const auto& sample : report.samples) {
		hb::console::write(std::format("RECONCILE ANOMALY {} {} {}",
			hb::server::reconcile_class_name(sample.class_id), sample.serial, sample.detail));
	}
	hb::console::write(std::format("RECONCILE INFO on_ground {}", report.on_ground));
	hb::console::write(std::format("RECONCILE INFO never_placed {}", report.never_placed));
	hb::console::write(std::format("RECONCILE INFO gone {}", report.gone));
	hb::console::write(std::format("RECONCILE INFO indeterminate {}", report.indeterminate));

	const int64_t total = report.anomaly_total();
	const int64_t critical = report.critical_total();
	hb::console::write(std::format("RECONCILE RESULT {} {} {}",
		total, critical, report.held_serials));

	// The suppressed evidence is named rather than left implicit: a class whose
	// count is larger than its printed examples looks, to a reader scrolling
	// past, exactly like a class that has been fully shown.
	if (sample_limit > 0) {
		for (int class_id = 0; class_id < reconcile_class::count; class_id++) {
			if (static_cast<size_t>(report.class_counts[class_id]) > sample_limit) {
				hb::console::write(std::format("  ({} more {} not shown - `reconcile 0` for all)",
					report.class_counts[class_id] - static_cast<int64_t>(sample_limit),
					hb::server::reconcile_class_name(class_id)));
			}
		}
	}

	if (critical > 0) {
		hb::console::error("reconcile: {} anomaly(ies), {} of them CRITICAL - the two records "
			"disagree about items that exist.", total, critical);
	}
	else if (total > 0) {
		hb::console::warn("reconcile: {} anomaly(ies), none critical. On a live world these are "
			"mostly unsaved players; on a saved one they are ledger coverage gaps.", total);
	}
	else {
		hb::console::success("reconcile: {} Serial(s) held, every one of them where the ledger "
			"says it is.", report.held_serials);
	}

	hb::logger::log<log_channel::commands>(
		"reconcile: {} held serial(s), {} anomaly(ies), {} critical ({} vs {})",
		report.held_serials, total, critical, world_path, ledger_path);
}
