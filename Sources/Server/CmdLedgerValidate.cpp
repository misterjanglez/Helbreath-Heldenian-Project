// CmdLedgerValidate.cpp: the state-machine validator (#84, plan P4.2)
//
// The replay is in ItemBiography.cpp; this file decides what to read and how to
// report it. The reporting rule is the one #83 set and this inherits: **count
// everything, accuse only what deserves it.** Four classes a healthy ledger
// cannot produce are errors, one that a lost flush makes honestly is a warning,
// and the states that are simply where items end up — sold, still carried, never
// placed — are printed as a census so the findings that matter are not buried in
// them.
//
//////////////////////////////////////////////////////////////////////

#include "CmdLedgerValidate.h"

#include "GmMintSpec.h"           // split_args
#include "ItemBiography.h"
#include "ItemLedgerStore.h"      // default_ledger_path
#include "LedgerTools.h"
#include "Log.h"
#include "ServerConsole.h"
#include "ServerLogChannels.h"

#include <cstdlib>
#include <format>
#include <string>

using hb::log_channel;
namespace ledger_violation = hb::server::ledger_violation;

void CmdLedgerValidate::execute(CGame* game, const char* args)
{
	// A token that parses as a number is the sample cap, anything else is the
	// path — the same positional-with-a-type shape `reconcile` takes, so an
	// operator who has used one already knows this one.
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
		if (paths_given++ > 0) {
			hb::console::error("ledgervalidate: too many paths - usage is [ledger_db] [limit].");
			return;
		}
		ledger_path = token;
	}

	if (const size_t pending = hb::server::flush_if_live_ledger(game, ledger_path); pending > 0) {
		hb::console::info("ledgervalidate: flushed {} buffered ledger event(s) before reading.",
			pending);
	}

	hb::server::read_only_db ledger(ledger_path);
	if (ledger.get() == nullptr) {
		hb::console::error("ledgervalidate: cannot open '{}' - {}.", ledger_path, ledger.error());
		return;
	}

	hb::server::validate_options options;
	options.sample_limit = sample_limit;

	const hb::server::validate_report report =
		hb::server::run_validation(ledger.get(), options);

	if (!report.ok) {
		hb::console::error("ledgervalidate: {}.", report.failure);
		return;
	}

	// --- The report --------------------------------------------------------
	hb::console::write("");
	hb::console::info("Ledger validation: {}", ledger_path);
	hb::console::write(std::format(
		"  replayed {} event(s) across {} Serial(s), against {} birth row(s)",
		report.events, report.serials, report.instances));

	for (int class_id = 0; class_id < ledger_violation::count; class_id++) {
		hb::console::write(std::format("LEDGERVALIDATE CLASS {} {}",
			hb::server::ledger_violation_name(class_id), report.class_counts[class_id]));
	}
	for (const auto& sample : report.samples) {
		hb::console::write(std::format("LEDGERVALIDATE VIOLATION {} {} {}",
			hb::server::ledger_violation_name(sample.class_id), sample.serial, sample.detail));
	}

	hb::console::write(std::format("LEDGERVALIDATE INFO ended {}", report.ended));
	hb::console::write(std::format("LEDGERVALIDATE INFO in_world {}", report.in_world));
	hb::console::write(std::format("LEDGERVALIDATE INFO never_moved {}", report.never_moved));
	hb::console::write(std::format("LEDGERVALIDATE INFO gaps {}", report.gaps));
	hb::console::write(std::format("LEDGERVALIDATE INFO gap_excused {}", report.gap_excused));

	const int64_t total = report.violation_total();
	const int64_t critical = report.critical_total();
	hb::console::write(std::format("LEDGERVALIDATE RESULT {} {} {}",
		total, critical, report.serials));

	// The suppressed evidence is named rather than left implicit: a class whose
	// count is larger than its printed examples looks, to a reader scrolling
	// past, exactly like a class that has been fully shown.
	if (sample_limit > 0) {
		for (int class_id = 0; class_id < ledger_violation::count; class_id++) {
			if (static_cast<size_t>(report.class_counts[class_id]) > sample_limit) {
				hb::console::write(std::format(
					"  ({} more {} not shown - `ledgervalidate {} 0` for all)",
					report.class_counts[class_id] - static_cast<int64_t>(sample_limit),
					hb::server::ledger_violation_name(class_id), ledger_path));
			}
		}
	}

	if (report.gap_excused > 0) {
		hb::console::warn("  {} step(s) excused by {} crash boundary(ies): the events that would "
			"explain them were in a flush window a crash lost.",
			report.gap_excused, report.gaps);
	}

	if (critical > 0) {
		hb::console::error("ledgervalidate: {} violation(s), {} of them CRITICAL - the ledger "
			"records a history that could not have happened.", total, critical);
	}
	else if (total > 0) {
		hb::console::warn("ledgervalidate: {} violation(s), none critical. These are the shape a "
			"lost flush makes: the ledger believed an item was somewhere it had already left.",
			total);
	}
	else {
		hb::console::success("ledgervalidate: {} Serial(s) replayed, every transition legal.",
			report.serials);
	}

	hb::logger::log<log_channel::commands>(
		"ledgervalidate: {} serial(s), {} event(s), {} violation(s), {} critical, {} excused ({})",
		report.serials, report.events, total, critical, report.gap_excused, ledger_path);
}
