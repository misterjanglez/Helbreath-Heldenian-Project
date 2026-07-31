// CmdItemHistory.cpp: the Biography query (#84, plan P4.2)
//
// The replay is in ItemBiography.cpp; this file decides what to read and how to
// say it. Two things shape the output:
//
//   1. **A Biography is read by a person settling a dispute**, not by a script.
//      So the timestamps are in their timezone, the places are words, and the
//      counterparty of a hand-off is printed beside the actor — "who had it when
//      the trade happened" is most of what such a dispute turns on.
//
//   2. **What the ledger does not know has to look different from what it
//      knows.** A Serial with no birth row, an event naming nobody, and a step a
//      crash can account for are each said in their own words rather than folded
//      into a blank column.
//
//////////////////////////////////////////////////////////////////////

#include "CmdItemHistory.h"

#include "GmMintSpec.h"           // split_args
#include "ItemBiography.h"
#include "ItemLedgerStore.h"      // default_ledger_path
#include "ItemProvenance.h"       // item_origin::name
#include "LedgerTools.h"
#include "Log.h"
#include "ServerConsole.h"
#include "ServerLogChannels.h"
#include "TimeUtils.h"

#include <cstdlib>
#include <format>
#include <string>

using hb::log_channel;
namespace ledger_place = hb::server::ledger_place;
namespace ledger_violation = hb::server::ledger_violation;

namespace
{
	std::string stamp(int64_t at)
	{
		char text[32];
		hb::time::format_timestamp(hb::time::local_time::from_unix(at), text, sizeof(text));
		return text;
	}

	// The `x, y` half of a location. Skipped entirely when there is no map,
	// because 0,0 is a real corner of a real map and printing it for "we were not
	// told" is how a forensic reader ends up looking in the wrong place.
	std::string where(const std::string& map, int x, int y)
	{
		return map.empty() ? std::string() : std::format(" {}({},{})", map, x, y);
	}

	// The machine-line verdict for one step. Three states, not two: a step can be
	// legal, rejected, or rejected-but-explained.
	std::string verdict(const hb::server::biography_step& step)
	{
		if (step.move.violation == ledger_violation::none) return "ok";
		const char* name = hb::server::ledger_violation_name(step.move.violation);
		return step.excused ? std::format("excused:{}", name) : name;
	}
}

void CmdItemHistory::execute(CGame* game, const char* args)
{
	std::string ledger_path = hb::server::default_ledger_path;
	int64_t serial = 0;
	int paths_given = 0;

	for (const std::string& token : hb::server::split_args(args)) {
		char* end = nullptr;
		const long long value = std::strtoll(token.c_str(), &end, 10);
		if (end != nullptr && *end == '\0' && !token.empty() && value > 0) {
			serial = static_cast<int64_t>(value);
			continue;
		}
		if (paths_given++ > 0) {
			hb::console::error("itemhistory: too many paths - usage is <serial> [ledger_db].");
			return;
		}
		ledger_path = token;
	}

	if (serial <= 0) {
		hb::console::error("itemhistory: name a Serial - usage is <serial> [ledger_db]. "
			"`reconcile` and `ledgervalidate` both print them.");
		return;
	}

	if (const size_t pending = hb::server::flush_if_live_ledger(game, ledger_path); pending > 0) {
		hb::console::info("itemhistory: flushed {} buffered ledger event(s) before reading.",
			pending);
	}

	hb::server::read_only_db ledger(ledger_path);
	if (ledger.get() == nullptr) {
		hb::console::error("itemhistory: cannot open '{}' - {}.", ledger_path, ledger.error());
		return;
	}

	const hb::server::biography life = hb::server::read_biography(ledger.get(), serial);
	if (!life.ok) {
		hb::console::error("itemhistory: {}.", life.failure);
		return;
	}

	// --- The birth ---------------------------------------------------------
	hb::console::write("");
	hb::console::info("Biography of Serial {} ({})", serial, ledger_path);

	if (life.has_instance) {
		const auto origin = static_cast<hb::server::item_origin::item_origin>(
			life.birth.origin_type);
		hb::console::write(std::format("  item {}, tier {}, minted {} - {}{}{}",
			life.birth.item_id, life.birth.tier, stamp(life.birth.created_at),
			hb::server::item_origin::name(origin),
			life.birth.origin_detail.empty()
				? std::string() : std::format(" '{}'", life.birth.origin_detail),
			where(life.birth.map, life.birth.x, life.birth.y)));
		hb::console::write(std::format("ITEMHISTORY BIRTH {} {} {} {}",
			serial, life.birth.item_id, static_cast<int>(life.birth.tier),
			hb::server::item_origin::name(origin)));
	}
	else {
		// Not an error. A Serial the ledger has events for and no birth row is a
		// finding in its own right — the validator's `orphan_event` — and one an
		// operator asking about this item needs to be told plainly.
		hb::console::warn("  no birth row: the ledger has no record of where this Serial "
			"came from.");
		hb::console::write(std::format("ITEMHISTORY BIRTH {} - - -", serial));
	}

	if (life.steps.empty()) {
		hb::console::write("  no events recorded.");
	}

	// --- The events --------------------------------------------------------
	int ordinal = 0;
	for (const hb::server::biography_step& step : life.steps) {
		ordinal++;

		std::string who = hb::server::ledger_actor_name(step.actor_char);
		if (!step.counterparty_char.empty()) {
			who += std::format(" -> {}", step.counterparty_char);
		}

		std::string line = std::format("  {:>3}  {}  {:<15} {:<24} => {}",
			ordinal, stamp(step.at), hb::server::ledger_event_name(step.event_type), who,
			hb::server::ledger_place_name(step.move.to));

		const std::string place = where(step.map, step.x, step.y);
		if (!place.empty()) line += place;
		if (!step.detail.empty()) line += std::format("  {}", step.detail);

		if (step.move.violation == ledger_violation::none) {
			hb::console::write(line);
		}
		else if (step.excused) {
			// Shown, and shown as not-counted. An operator who sees the step but
			// not the reason it was forgiven will report it as a bug in the tool.
			hb::console::write(line);
			hb::console::write(std::format("       EXCUSED {} - a crash boundary sits before this "
				"event, so the move that would explain it may never have reached disk.",
				hb::server::ledger_violation_name(step.move.violation)));
		}
		else {
			hb::console::write(line);
			hb::console::error("       {} - expected it {}",
				hb::server::ledger_violation_name(step.move.violation),
				hb::server::ledger_place_name(step.move.required));
		}

		hb::console::write(std::format("ITEMHISTORY EVENT {} {} {} {} {} {}",
			ordinal, hb::server::ledger_event_name(step.event_type),
			hb::server::ledger_place_name(step.move.to),
			step.actor_char.empty() ? "-" : step.actor_char,
			step.counterparty_char.empty() ? "-" : step.counterparty_char,
			verdict(step)));
	}

	// --- Where it is now ---------------------------------------------------
	hb::console::write(std::format("ITEMHISTORY RESULT {} {} {} {}",
		serial, life.steps.size(), hb::server::ledger_place_name(life.final_place),
		life.violations));

	if (life.violations > 0) {
		hb::console::error("itemhistory: Serial {} - {} event(s), now {}, and {} step(s) the "
			"transition machine says could not have happened.",
			serial, life.steps.size(), hb::server::ledger_place_name(life.final_place),
			life.violations);
	}
	else if (!life.has_instance && life.steps.empty()) {
		hb::console::warn("itemhistory: Serial {} - nothing at all is recorded against it. "
			"Either it was never minted, or its flush was lost.", serial);
	}
	else {
		hb::console::success("itemhistory: Serial {} - {} event(s), now {}, every step legal.",
			serial, life.steps.size(), hb::server::ledger_place_name(life.final_place));
	}

	hb::logger::log<log_channel::commands>("itemhistory: serial {}, {} event(s), {} violation(s) ({})",
		serial, life.steps.size(), life.violations, ledger_path);
}
