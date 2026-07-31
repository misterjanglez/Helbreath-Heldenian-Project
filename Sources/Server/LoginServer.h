#pragma once

#include "CommonTypes.h"
#include <iostream>
#include <vector>
using namespace std;

#include "Game.h"
#include "AccountSqliteStore.h"

enum class LogIn
{
	Ok,
	NoAcc,
	NoPass,
};

class LoginServer
{
public:
	LoginServer();
	~LoginServer();

	void request_login(int h, char* data);
	void get_char_list(string acc, char*& cp2, const std::vector<AccountDbCharacterSummary>& chars);
	LogIn AccountLogIn(string name, string pass, std::vector<AccountDbCharacterSummary>& chars);
	void response_character(int h, char* data);
	void delete_character(int h, char* data);
	void change_password(int h, char* data);
	void request_enter_game(int h, char* data);
	void create_new_account(int h, char* data);
	void send_login_msg(uint32_t msgid, uint16_t msgtype, char* data, size_t sz, int h);
	void send_balance_config(int h);
	void send_server_config(int h);
	void send_color_palette(int h);

	// Persist one online character. Returns whether the character SNAPSHOT
	// reached the database — a block list that failed to write is not part of
	// the verdict, because it stays flagged dirty and rides the next save, and
	// nothing about items depends on it.
	bool local_save_player_data(int h);

	// Persist several online characters in ONE game.db transaction: every one is
	// durable or none of them is (#82, plan P3.4, docs/adr/0004-single-game-db.md).
	//
	// This is the primitive the per-account files could not offer. An Exchange
	// mutates two characters, and before this the two halves became durable at
	// whatever unrelated moments each character next happened to be saved — a
	// logout, a map change, an autosave sweep. Whichever landed first and was
	// followed by a crash left the world holding one half of a trade: the item on
	// both characters, or on neither. Ordering the two saves carefully cannot fix
	// that; only one commit can.
	//
	// Duplicate handles are collapsed, and a handle whose client is gone is
	// skipped rather than failing the batch — a trade partner who disconnected
	// mid-operation is an ordinary case, not an error.
	//
	// A false return means nothing was written. The callers' in-memory state has
	// already changed by then, so RAM is ahead of the database until the next
	// successful save — which is exactly where every one of these paths sat for
	// the whole autosave window before this existed.
	bool save_players_atomic(const int* handles, int count);
	void activated();

private:
	// One character's rows, with no transaction of its own beyond the SAVEPOINT
	// SaveCharacterSnapshot already opens. Shared by the single and the atomic
	// save so the two can never drift about what "saved" includes.
	bool save_one_player(sqlite3* db, int h);
};

// The batch ceiling. Every caller is a fixed, tiny set — an Exchange or a Give
// is two characters, and the largest composed Trading Post operation is a
// Seller, a winner and its losing offerers — so this is a stack array rather
// than an allocation. Anything larger than a board full of Offers is a bug, and
// truncating loudly beats growing without bound inside a transaction.
inline constexpr int max_atomic_save_handles = 16;

extern LoginServer* g_login;
