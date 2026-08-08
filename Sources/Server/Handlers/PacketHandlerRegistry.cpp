// PacketHandlerRegistry.cpp: dispatch + registration for the Phase-1 seam.
// See the header for why this exists and what the gates are.
//
//////////////////////////////////////////////////////////////////////

#include "PacketHandlerRegistry.h"

#include "../Game.h"
#include "Log.h"

namespace hb::server
{
	void packet_handler_registry::register_handler(uint32_t msg_id,
		packet_handler handler)
	{
		if (handler == nullptr)
		{
			hb::logger::error(
				"packet_handler_registry: null handler for msg id 0x{:08X}", msg_id);
			return;
		}
		const auto [it, inserted] = m_handlers.emplace(msg_id, handler);
		if (!inserted)
		{
			hb::logger::error(
				"packet_handler_registry: msg id 0x{:08X} registered twice; keeping the first",
				msg_id);
		}
	}

	bool packet_handler_registry::dispatch(CGame& game, int client_h,
		uint32_t msg_id, const char* data, std::size_t msg_size) const
	{
		const auto it = m_handlers.find(msg_id);
		if (it == m_handlers.end())
		{
			return false;
		}

		// The standing player-action gates, applied once for every family
		// (client_common_handler's exact ladder).
		CClient* client = game.m_client_list[client_h];
		if (client == nullptr) return true;
		if (client->m_is_init_complete == false) return true;
		if (client->m_is_killed) return true;

		it->second(game, client_h, data, msg_size);
		return true;
	}
}
