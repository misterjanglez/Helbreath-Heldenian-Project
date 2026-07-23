#pragma once

#include "ServerCommand.h"

class CmdTestCrash : public ServerCommand
{
public:
	const char* get_name() const override { return "testcrash"; }
	const char* GetDescription() const override { return "Deliberately crash the server to test crash reporting"; }
	const char* GetHelp() const override { return "Usage: testcrash confirm\n  Immediately crashes the server process (null dereference) so the\n  Sentry/crashpad pipeline can be verified end-to-end. The crash report\n  uploads on the next server start. Requires the literal argument\n  'confirm'. Players are NOT saved - use saveall first."; }
	void execute(CGame* game, const char* args) override;
};
