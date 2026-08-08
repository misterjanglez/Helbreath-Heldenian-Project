// PacketHandlerRegistry.h: the inbound packet-family seam (Phase 1, plan §5)
//
// msg_process owns a 40-case switch and client_common_handler a 60-case one;
// every packet family before this seam grew them both. The registry is the
// other door: a family registers one handler per top-level msg id at boot, and
// msg_process consults the registry before declaring a message unknown. A new
// family therefore lands as one domain file (Handlers/NetworkMessages_*.cpp,
// the client's 23-file pattern mirrored server-side) plus one registration
// call — msg_process, client_common_handler and send_notify_msg stay closed.
//
// The registry is a plain CGame member, not a singleton: it lives and dies
// with the game object (function-local statics have already produced one
// destruction-order segfault in this codebase), and handlers receive the CGame
// they were dispatched from rather than reaching for a global.
//
// Handlers run behind the same standing gates client_common_handler applies to
// every player action — the client slot must be occupied, fully in the world
// (m_is_init_complete) and alive — so a family cannot forget them. A gated-off
// message is consumed, not passed on: its msg id is known, there is just
// nobody in a state to act on it. Families with pre-init or post-death
// messages do not exist today; if one arrives, the gate becomes a per-handler
// flag then, not a hole now.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

class CGame;

namespace hb::server
{
	// One family's handler for one top-level msg id. `data` is the raw
	// message (PacketHeader first); the handler PacketCasts to its typed
	// request and validates `msg_size` itself, exactly as the switch-arm
	// handlers do.
	using packet_handler = void (*)(CGame& game, int client_h,
		const char* data, std::size_t msg_size);

	class packet_handler_registry
	{
	public:
		packet_handler_registry() = default;

		packet_handler_registry(const packet_handler_registry&) = delete;
		packet_handler_registry& operator=(const packet_handler_registry&) = delete;

		// Claim a msg id for `handler`. Registration happens once, at boot,
		// from CGame's constructor; a second claim on the same id is a wiring
		// bug and fails loudly there (the first registration wins, so the
		// running server stays deterministic).
		void register_handler(uint32_t msg_id, packet_handler handler);

		// Route one client message. False when no family owns `msg_id` — the
		// caller keeps its unknown-message logging. True means the message is
		// spoken for, whether the handler ran or a standing gate ate it.
		bool dispatch(CGame& game, int client_h, uint32_t msg_id,
			const char* data, std::size_t msg_size) const;

	private:
		std::unordered_map<uint32_t, packet_handler> m_handlers;
	};
}
