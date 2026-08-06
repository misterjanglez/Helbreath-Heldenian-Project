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

		// The stable name of a venue, for the half of the ledger a human reads —
		// a Biography line saying an item was born of origin 11 answers nothing.
		// Beside the enum for the same reason destroy_reason::name is, and
		// switch-based so a venue added without being named is a compiler warning
		// rather than a row silently reading "unknown".
		//
		// These names are permanent world fact once real players exist: add
		// freely, never rename, or every forensic query written against the old
		// spelling stops matching without saying so.
		inline const char* name(item_origin origin)
		{
			switch (origin)
			{
			case npc_drop:     return "npc_drop";
			case shop_buy:     return "shop_buy";
			case craft:        return "craft";
			case quest:        return "quest";
			case fishing:      return "fishing";
			case mining:       return "mining";
			case harvest:      return "harvest";
			case lottery:      return "lottery";
			case war_reward:   return "war_reward";
			case hero_reward:  return "hero_reward";
			case dark_claim:   return "dark_claim";
			case angel_claim:  return "angel_claim";
			case magic:        return "magic";
			case event_reward: return "event_reward";
			case gm_mint:      return "gm_mint";
			case restored:     return "restored";
			case none:         break;
			}
			return "none";
		}
	}

	// Why an item left the population for good (plan P3.1).
	//
	// Recorded in the destroyed event's `detail` as a string rather than a
	// number, because that column is the half of the ledger a human reads
	// directly. The names are therefore **permanent world fact the moment real
	// players exist**: add new ones freely, never rename an existing one, or
	// every forensic query written against the old name silently stops matching.
	//
	// Destruction and despawn are different exits and stay different event types
	// (ItemLedgerStore.h): this enum answers "why was it destroyed", and a ground
	// item that simply timed out is a despawn, not a destruction.
	namespace destroy_reason
	{
		enum destroy_reason : int32_t
		{
			// A slot emptied without the caller saying more. The default, so a
			// destruction can never be *missing* just because nobody chose a
			// reason — an unspecified exit still beats a silent one.
			unspecified,

			// Used up by the action that consumed it: a potion drunk, an arrow
			// fired, a material spent on a craft.
			consumed,

			// Sold to a shop. The item leaves the player economy entirely, which
			// is what separates this from a trade.
			sold,

			// Broken by a failed upgrade.
			upgrade_break,

			// Minted but never delivered: the buyer could not pay, the inventory
			// was full, the request was rejected after the item existed. Its
			// Serial was spent, so the ledger has a birth row that would
			// otherwise have no matching exit.
			discarded,

			// Destroyed in place on the ground by something in the world, rather
			// than timing out — today, burned by a fire spell.
			ground_burn,

			// A custody transfer failed with the item already out of its owner's
			// hands, and it was lost rather than returned. A delivery failure, not
			// a player choice.
			//
			// Two venues reach it, both Trading Post (#80), and both are the
			// deliberate loss side of ADR 0001's ordering — escrow is arranged so
			// that a failure loses an item rather than duplicating it: a Warehouse
			// that could not take a returning bundle, and an escrow row that could
			// not be written after the inventory was already reduced.
			delivery_lost,

			// A stackable whose contents live on in a stack the holder holds, so
			// nothing left the world: a piece merged into a stack the holder
			// already had — a pickup, a hand-over, a crafted batch landing on an
			// existing pile. The husk is freed and the units remain in play,
			// which is why this is the one reason the flow booking excludes (#81).
			//
			// A second shape used to reach it: a split piece freed after its
			// contents were restored to the stack it was split from — a Give an
			// NPC refused (#110). #112 resolves the Give's destination before
			// the split, so a refusal no longer creates a piece and no live
			// caller restores-then-frees anymore.
			//
			// Only stackables merge, so this reason should never reach a row —
			// Counted items carry no Serial and the funnel records nothing for
			// them. It exists so that if that invariant ever breaks, the ledger
			// says something true instead of calling a merge a consumption.
			merged,

			// The character that owned it was deleted. Only escrowed Trading Post
			// items reach this: they share the fate of the inventory they were
			// pulled from, so they are never delivered and never returned.
			//
			// A separate reason from delivery_lost because nothing failed here.
			// Rolled into it, every character deletion would read as an outage.
			character_deleted,
		};

		// The stable wire/storage name of a reason. Kept beside the enum so the
		// two cannot drift, and switch-based so adding a value without naming it
		// is a compiler warning rather than a row reading "unspecified".
		inline const char* name(destroy_reason reason)
		{
			switch (reason)
			{
			case consumed:          return "consumed";
			case sold:              return "sold";
			case upgrade_break:     return "upgrade_break";
			case discarded:         return "discarded";
			case ground_burn:       return "ground_burn";
			case delivery_lost:     return "delivery_lost";
			case merged:            return "merged";
			case character_deleted: return "character_deleted";
			case unspecified:       break;
			}
			return "unspecified";
		}
	}

	// Why an item left the ground without anyone taking it (plan P3.1).
	//
	// Deliberately a separate enum from destroy_reason rather than one shared
	// "exit" taxonomy: the two are recorded as different event types, and a
	// shared enum would let a despawn be filed as "sold". Both are permanent
	// world fact under the same rule — append, never rename.
	namespace despawn_reason
	{
		enum despawn_reason : int32_t
		{
			// Its time on the ground ran out. This is the attrition signal Loot
			// 2.0 tuning wants: items players walked past and did not want.
			expired,

			// Pushed off a full tile. A tile holds a fixed number of items and
			// the oldest falls off the end when a new one lands on it, so this
			// is attrition too — just crowd-driven rather than clock-driven.
			tile_overflow,

			// The world stopped while it was lying there. Ground items live only
			// in RAM, so a shutdown ends every one of them.
			//
			// **Exclude this reason from attrition queries.** It says something
			// about the operator, not about whether players valued the item, and
			// counting it would make every restart look like a wave of rejected
			// loot.
			world_shutdown,
		};

		inline const char* name(despawn_reason reason)
		{
			switch (reason)
			{
			case tile_overflow:  return "tile_overflow";
			case world_shutdown: return "world_shutdown";
			case expired:        break;
			}
			return "expired";
		}
	}

	// What a creation venue knows about a birth that the item itself cannot say
	// (plan P3.1). The ledger's item_instances row has columns for all of it —
	// origin_detail, map, x, y — and #78 left them empty because the minting
	// funnel is the one place in the server that does not know any of it.
	//
	// So the venue supplies it. Every field is optional: a venue that genuinely
	// has no location (a boot-time grant, a tool) passes nothing and the columns
	// stay NULL, which is the honest answer rather than a 0,0 a query would
	// mistake for the top-left corner of a map.
	//
	// The pointers are borrowed for the duration of the mint only — record_mint
	// copies what it needs into the buffered row before returning — so a caller
	// can point them straight at a client's map name or an NPC's name without
	// owning a copy.
	struct birth_context
	{
		const char* detail = nullptr;   // npc name, crafter, GM name
		const char* map    = nullptr;
		int         x      = 0;
		int         y      = 0;
	};

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
