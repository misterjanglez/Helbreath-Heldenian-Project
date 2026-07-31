// LedgerTools.h: the shared door the offline ledger tools open files through
//
// `reconcile`, `itemhistory` and `ledgervalidate` all answer a different
// question and all start the same way: decide whether the path they were given
// is the live file, push the ledger's RAM buffer to disk if it is, and open
// what they read READ-ONLY. Three copies of that would be three chances for one
// of them to become a second writer to a running server's database — which is
// the one thing none of them may ever do.
//
// So it lives here once, and #85's analytics tooling gets it for free.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <string>

struct sqlite3;
class CGame;

namespace hb::server
{
	// A read-only connection, closed however the command returns.
	//
	// Read-only is the contract rather than a precaution: these jobs are pointed
	// at the live files of a running server, and SQLite is what enforces it at
	// the connection instead of leaving it to every query in every tool.
	class read_only_db
	{
	public:
		explicit read_only_db(const std::string& path);
		~read_only_db();

		read_only_db(const read_only_db&) = delete;
		read_only_db& operator=(const read_only_db&) = delete;

		// Null when the file could not be opened; `error()` says why.
		sqlite3* get() const { return m_db; }
		const std::string& error() const { return m_error; }

	private:
		sqlite3* m_db = nullptr;
		std::string m_error;
	};

	// Whether two paths name the same file. Compared as canonical paths and not
	// as strings, so `itemledger.db` and `./itemledger.db` are both the live file
	// and both take the flush below.
	bool same_file(const std::string& a, const std::string& b);

	// If `path` names the live Provenance Ledger, flush its buffer and return how
	// many events that pushed to disk; 0 otherwise.
	//
	// The ledger keeps its events in RAM until the flush cadence fires (D4), so a
	// tool that read the file without pushing first would report the last few
	// seconds of perfectly legitimate moves as findings — the report would be at
	// its most alarming exactly when the server is busiest.
	size_t flush_if_live_ledger(CGame* game, const std::string& path);
}
