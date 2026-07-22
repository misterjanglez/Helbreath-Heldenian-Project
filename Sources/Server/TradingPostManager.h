#pragma once

#include <cstddef>
#include <cstdint>

class CGame;

namespace hb::server
{
	class trading_post_store;

	// Request handlers for the Trading Post — the Auctioneer (Vince) dialog
	// protocol. One handler per Tp* request MsgId, plus the login-flush and
	// expiry-sweep hooks.
	//
	// Handlers own the protocol-layer policy: parse the request, confirm the actor
	// is online, alive, not mid-Exchange, and standing at an Auctioneer, then
	// re-validate the business rules against live server state before calling the
	// trading_post_store escrow primitives. Every mutating action answers with a
	// PacketResponseTpActionResult carrying a TpResultCode; browse/detail answer
	// with their own response packets (or a TpActionResult on failure).
	//
	// Item data is never trusted from the client: create/offer requests carry
	// inventory slot indices + amounts only, and the server re-reads the
	// character's item list at request time (inside the store).
	class trading_post_manager
	{
	public:
		trading_post_manager() = default;

		void set_game(CGame* game) { m_game = game; }

		// The request handlers, dispatched from the client message switch.
		void handle_board_page(int client_h, char* data, size_t size);
		void handle_my_board(int client_h, char* data, size_t size);
		void handle_listing_detail(int client_h, char* data, size_t size);
		void handle_create_listing(int client_h, char* data, size_t size);
		void handle_place_offer(int client_h, char* data, size_t size);
		void handle_rescind_offer(int client_h, char* data, size_t size);
		void handle_finalize(int client_h, char* data, size_t size);
		void handle_delist(int client_h, char* data, size_t size);

		// Login hook: flush the character's queued notices as system chat lines.
		void on_player_login(int client_h);

		// Periodic hook (piggybacks the game tick at ~autosave cadence): delist any
		// Listing past its expiry. Cheap no-op when nothing is due.
		void process_expiry(uint32_t now_ms);

	private:
		// True when the actor may run a Trading Post action right now. On failure it
		// has already replied with the matching TpResultCode (or returns false
		// silently when the actor is offline and cannot be answered).
		bool actor_ready(int client_h, uint16_t action);

		// Is the actor standing within interaction range of an Auctioneer NPC?
		// Always false until Phase 4 places Vince — handlers then reply
		// NotNearAuctioneer, which is expected and correct for Phase 3.
		bool is_near_auctioneer(int client_h) const;

		// Send a PacketResponseTpActionResult to the actor.
		void send_result(int client_h, uint16_t action, uint16_t code, int32_t value = 0);

		// The escrow store owned by CGame (nullptr until it is opened at startup).
		trading_post_store* store() const;

		CGame* m_game = nullptr;
		uint32_t m_last_sweep_ms = 0;   // last expiry sweep (GameClock ms)
		bool m_swept_once = false;      // force one sweep on the first tick
	};
}
