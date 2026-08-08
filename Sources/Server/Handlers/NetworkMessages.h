// NetworkMessages.h: registration entry points for the Handlers/ domain files
// (the Phase-1 seam's family roster). One line per family; CGame's constructor
// calls each once, and nothing else about a family is visible outside its
// domain file.
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace hb::server
{
	class packet_handler_registry;

	// The guild wire family (#123, PacketGuild.h).
	void register_guild_packet_handlers(packet_handler_registry& registry);
}
