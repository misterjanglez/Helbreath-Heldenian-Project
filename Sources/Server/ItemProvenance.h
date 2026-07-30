// ItemProvenance.h: Serial identity for Instanced items (ADR 0003, plan P1.1)
//
// A Serial is the permanent server-global identity of one Instanced item —
// minted once at creation, never reused, and never sent to the client. See the
// m_serial comment on CItem for why it cannot live in the wire POD.
//
// Design contract: PLANS/ItemLedger_Plan.md P1.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace hb::server
{
	// How an item entered the world. Recorded once when it is minted and never
	// changed afterwards: an item that is traded, upgraded or transformed keeps
	// the origin it was born with, because those are events in its Biography
	// rather than new origins.
	//
	// The values are the creation venues that actually exist in the server today
	// — one per funnelled call site, not a speculative taxonomy. The ledger's
	// origin_type column stores these numbers directly, so new venues append at
	// the end and existing values never shift.
	namespace item_origin
	{
		enum item_origin : int32_t
		{
			none         = 0,   // Counted (stackable) items, and item-config templates
			npc_drop     = 1,
			shop_buy     = 2,
			craft        = 3,   // crafted goods, built items, slates
			quest        = 4,
			fishing      = 5,
			mining       = 6,
			harvest      = 7,   // crops, and the meat/fish the butcher skill yields
			lottery      = 8,
			war_reward   = 9,
			hero_reward  = 10,  // hero mantle / cape
			dark_claim   = 11,
			angel_claim  = 12,  // angelic pendants and majestic claims
			magic        = 13,  // conjured by a spell
			event_reward = 14,  // memorial ring and similar one-off grants
			gm_mint      = 15,
			restored     = 16,  // rehydrated from persistence carrying no stored Serial
		};
	}

	// Monotonic Serial source. One per server; never hands out a value twice.
	//
	// In-RAM only in P1.1. The durable high-water home in itemledger.db's `meta`
	// table arrives with #76, which will call resume_from() at boot; until then a
	// restart restarts the sequence, which D6 wipe-freedom explicitly tolerates
	// on dev worlds. Keeping the seam here means #76 is a wiring change rather
	// than another sweep of the call sites.
	//
	// Not synchronised: every mint happens inside the single-threaded game loop,
	// matching the rest of the server's ownership model.
	class serial_allocator
	{
	public:
		// The next unused Serial. Serials start at 1, which keeps 0 available as
		// "unserialed" — what every Counted item and config template is.
		int64_t mint() { return ++m_high_water; }

		int64_t high_water() const { return m_high_water; }

		// Boot recovery (#76): resume above the durable high-water mark. Never
		// lowers the counter, so a stale mark cannot hand out a live Serial twice.
		void resume_from(int64_t high_water)
		{
			if (high_water > m_high_water) m_high_water = high_water;
		}

	private:
		int64_t m_high_water = 0;
	};
}
