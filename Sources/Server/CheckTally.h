// CheckTally.h: the shared scaffold for the headless contract provers
//
// `serialcheck`, `ledgercheck` and the provers the rest of epic #74 will add all
// answer the same shape of question: run a list of named checks against a
// contract, print one machine-readable line each, and finish with a total. The
// output format is not cosmetic — the Linux gate compares it against the Windows
// run byte for byte — so it lives in one place. A per-prover copy would let a
// format tweak land in one and silently desynchronise the other's baseline.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>

#include "ServerConsole.h"

struct sqlite3;
class CGame;
class CItem;

namespace hb::server
{
	// Runs a prover's checks and reports them. `prefix` is the machine-line tag
	// ("SERIALCHECK", "LEDGERCHECK"); it is also what the closing summary names.
	class check_tally
	{
	public:
		explicit check_tally(const char* prefix, const char* command)
			: m_prefix(prefix), m_command(command) {}

		void record(const char* name, bool ok, const std::string& detail = {})
		{
			++m_total;
			if (ok) ++m_passed;
			if (detail.empty())
				hb::console::write("{} {} {}", m_prefix, name, ok ? "PASS" : "FAIL");
			else
				hb::console::write("{} {} {} {}", m_prefix, name, ok ? "PASS" : "FAIL", detail);
		}

		// The closing lines: the machine-readable total, then a human summary.
		// `context` is an optional parenthetical for the success line (the probe
		// ids a run happened to pick, say) — deliberately kept off the machine
		// line, which must stay comparable across platforms.
		void report(const std::string& context = {}) const
		{
			hb::console::write("{} RESULT {} {}", m_prefix, m_passed, m_total);

			if (m_passed == m_total)
			{
				if (context.empty())
					hb::console::success("{}: {}/{} checks passed.", m_command, m_passed, m_total);
				else
					hb::console::success("{}: {}/{} checks passed ({}).",
						m_command, m_passed, m_total, context);
			}
			else
			{
				hb::console::error("{}: {}/{} checks passed - the contract is broken.",
					m_command, m_passed, m_total);
			}
		}

		int passed() const { return m_passed; }
		int total() const { return m_total; }

	private:
		const char* m_prefix;
		const char* m_command;
		int m_passed = 0;
		int m_total = 0;
	};

	// The first loaded item-config row with the wanted stackability, or -1.
	// Deriving probe items from data beats hard-coded ids that a content edit
	// could quietly turn into the wrong kind of item — and both provers must pick
	// the same way, or their outputs stop being evidence about the same thing.
	int find_probe_item(CGame* game, bool want_stackable);

	// First column of the first row. `probe_scalar` returns -1 when the query
	// fails or yields nothing (no expected value in any prover is -1);
	// `probe_text` returns empty.
	//
	// Here rather than in each prover because a check is only evidence if it
	// reads the database the same way the other provers do — a per-prover copy
	// that treated a NULL or a failed prepare differently would make two
	// commands disagree about the same file for a reason nobody would look for.
	int64_t probe_scalar(sqlite3* db, const char* sql);
	std::string probe_text(sqlite3* db, const char* sql);
}
