// CmdLifecycleCheck.cpp: lifecycle-coverage prover (Item Provenance Ledger P3.1, #79)
//
// #78 left the ledger recording every transition that already had an item_log()
// call, and #79 closes what was left. The three gaps it closes fail in the same
// quiet way — the file keeps filling up and looks complete:
//
//   - A birth row with no location and a Tier 0 for a Tier 3 sword. The mint
//     happens before an NPC drop rolls its attributes, so the one query the
//     whole effort promises Loot 2.0 — actual drop rates by tier and source —
//     would read every drop as untiered and located nowhere.
//   - An item freed without an exit event. Its Serial then has a birth row, no
//     holder and no ending, which is exactly what Reconciliation reports as a
//     duplication. Every silent delete is a future false positive in the one
//     report the ledger exists to produce.
//   - A ground item that vanishes without a word. Despawn attrition — what
//     players walked past and did not want — has no other source.
//
// So the checks below read the columns a forensic query will read, not the code
// paths that wrote them. Where a number is permanent world fact (the stored
// event_type of a destroyed or a despawned row, ItemLogAction::Buy) the check
// asserts a literal rather than the enum it was recorded with: a check that
// queried with the same symbol it stored would still pass if both were
// renumbered together, and renumbering is the one thing that must never happen.
//
// The ground checks use a real map tile, because the eviction and expiry rules
// are properties of the tile's layout — newest at slot 0, oldest falling off the
// end — and a mock would only prove the mock. Everything placed is taken back
// off before the run returns.
//
// As with itemlogcheck the ledger sink is redirected to a scratch database and
// restored on every exit path, so the live itemledger.db gains nothing. The
// machine-readable lines carry no serials, ids, timestamps or row counts: the
// Linux gate compares them against the Windows run byte for byte.
//
// Design contract: PLANS/ItemLedger_Plan.md P3.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdLifecycleCheck.h"

#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "Map.h"
#include "RollStrategy.h"
#include "ServerConsole.h"
#include "ServerMessages.h"
#include "Tile.h"
#include "sqlite3.h"

#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace despawn_reason = hb::server::despawn_reason;
namespace destroy_reason = hb::server::destroy_reason;
namespace item_origin = hb::server::item_origin;
namespace ledger_event = hb::server::ledger_event;
namespace ItemLogAction = hb::server::net::ItemLogAction;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::find_probe_item;
using hb::server::item_ledger_store;
using hb::server::probe_client;
using hb::server::probe_scalar;
using hb::server::probe_text;

namespace
{
	constexpr const char* probe_db = "lifecyclecheck_probe.db";

	constexpr const char* actor_account = "probeacct";
	constexpr const char* actor_char    = "ProbeLife";
	constexpr const char* actor_map     = "probemap";
	constexpr int actor_x = 11;
	constexpr int actor_y = 22;

	// The birth context the venue-supplied checks look for. The NPC name carries
	// a quote on purpose: origin_detail is written through the JSON encoder in
	// the same way the event detail is, and one unescaped quote out of a content
	// file makes a row no reader can parse.
	constexpr const char* birth_npc  = "Probe\"Slime";
	constexpr const char* birth_map  = "birthmap";
	constexpr int birth_x = 33;
	constexpr int birth_y = 44;

	// Stored numbers written out as literals. These are permanent world fact the
	// moment real players exist, so the checks below must not be able to move
	// with them.
	constexpr int despawned_event_number = 101;
	constexpr int destroyed_event_number = 102;
	constexpr int buy_event_number       = 7;
	static_assert(static_cast<int>(ledger_event::despawned) == despawned_event_number,
		"ledger_event::despawned moved: every despawn row already written means something else now.");
	static_assert(static_cast<int>(ledger_event::destroyed) == destroyed_event_number,
		"ledger_event::destroyed moved: every destruction row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Buy) == buy_event_number,
		"ItemLogAction::Buy moved: every shop-purchase row already written means something else now.");

	// A loaded map with room for the tile probes, and a tile on it that starts
	// empty. Taken from live data rather than hardcoded: which maps exist is a
	// content decision, and a hardcoded name would turn a content edit into a
	// prover failure that looks like a code regression.
	CMap* find_probe_map(CGame* game, short& out_x, short& out_y)
	{
		for (int i = 0; i < hb::server::config::MaxMaps; i++)
		{
			CMap* map = game->m_map_list[i];
			if (map == nullptr || map->m_tile == nullptr) continue;
			if (map->m_size_x < 8 || map->m_size_y < 8) continue;

			// Scan for a tile holding nothing, so the overflow and expiry counts
			// below start from a known zero.
			for (short y = 1; y < map->m_size_y && y < 64; y++)
				for (short x = 1; x < map->m_size_x && x < 64; x++)
					if (map->peek_item(x, y) == nullptr)
					{
						out_x = x;
						out_y = y;
						return map;
					}
		}
		return nullptr;
	}
}

void CmdLifecycleCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("LIFECYCLECHECK", "lifecyclecheck");

	if (game == nullptr || game->m_item_manager == nullptr
		|| game->m_item_ledger_store == nullptr || !game->m_item_ledger_store->is_open())
	{
		hb::console::error("lifecyclecheck: the live ledger is not open.");
		return;
	}

	ItemManager& items = *game->m_item_manager;

	const int instanced_id = find_probe_item(game, false);
	const int counted_id = find_probe_item(game, true);
	if (items.is_valid_item_id(instanced_id) == false
		|| items.is_valid_item_id(counted_id) == false)
	{
		hb::console::error("lifecyclecheck: no usable probe item ids.");
		return;
	}

	const int actor_h = find_free_handle(game);
	if (G_pIOPool == nullptr || actor_h < 0)
	{
		hb::console::error("lifecyclecheck: no free client handle for the probe.");
		return;
	}

	short tile_x = 0, tile_y = 0;
	CMap* map = find_probe_map(game, tile_x, tile_y);
	if (map == nullptr)
	{
		hb::console::error("lifecyclecheck: no loaded map with an empty probe tile.");
		return;
	}

	//----------------------------------------------------------------------
	// Redirect the sink. Everything below records into the scratch file.
	//----------------------------------------------------------------------

	hb::server::ledger_sink_swap swap(game, probe_db);
	if (!swap.ok())
	{
		hb::console::error("lifecyclecheck: could not create the scratch ledger '{}'.", probe_db);
		return;
	}
	item_ledger_store& ledger = swap.scratch();

	const probe_client actor(game, actor_h, actor_account, actor_char, actor_map, actor_x, actor_y);

	using item_ptr = std::unique_ptr<CItem>;

	//----------------------------------------------------------------------
	// Birth context: what the venue knows and the mint funnel cannot.
	//----------------------------------------------------------------------

	const hb::server::birth_context birth{ birth_npc, birth_map, birth_x, birth_y };

	item_ptr placed{ items.create_item(instanced_id, item_origin::npc_drop, birth) };
	item_ptr contextless{ items.create_item(instanced_id, item_origin::npc_drop) };

	// The loot venue: attributes are rolled inside the entry point so the mint
	// happens after them. A tiered roll is only produced by the tiered strategy,
	// so what is asserted below is agreement between the row and the item —
	// which is the actual contract, and holds in either mode.
	hb::server::roll_context rolls;
	rolls.loot_grade = 5;
	rolls.tier_rolls = true;
	item_ptr looted{ items.create_loot_item(instanced_id, rolls, birth) };

	if (placed == nullptr || contextless == nullptr || looted == nullptr)
	{
		hb::console::error("lifecyclecheck: item creation failed for a probe item.");
		return;
	}

	const int64_t placed_serial = placed->m_serial;
	const int64_t contextless_serial = contextless->m_serial;
	const int64_t looted_serial = looted->m_serial;
	const int looted_tier = static_cast<int>(looted->get_tier());

	//----------------------------------------------------------------------
	// Destruction. The Counted half is observed in the buffer, because
	// "nothing was recorded" is the one thing a query of the file cannot tell
	// apart from "the flush has not happened yet".
	//----------------------------------------------------------------------

	item_ptr doomed{ items.create_item(instanced_id, item_origin::shop_buy, birth) };
	if (doomed == nullptr)
	{
		hb::console::error("lifecyclecheck: item creation failed for the destruction probe.");
		return;
	}
	const int64_t doomed_serial = doomed->m_serial;

	CItem* doomed_raw = doomed.release();
	items.destroy_item(doomed_raw, destroy_reason::upgrade_break, actor_h);
	const bool destroy_nulled = (doomed_raw == nullptr);

	const size_t before_counted = ledger.pending_count();
	CItem* counted_raw = items.create_item(counted_id, item_origin::shop_buy, birth);
	items.destroy_item(counted_raw, destroy_reason::sold, actor_h);
	const bool counted_silent = (ledger.pending_count() == before_counted);

	// A shop purchase. The call site is in request_purchase_item_handler; what
	// is provable here is that the action reaches the ledger as its own number.
	items.item_log(ItemLogAction::Buy, actor_h, -1, placed.get());

	//----------------------------------------------------------------------
	// Ground lifecycle, on a real tile.
	//----------------------------------------------------------------------

	// Placement stamps the clock. Everything the sweep does keys off this, and
	// an unstamped item would read as placed at time zero — instantly expired.
	item_ptr grounded{ items.create_item(instanced_id, item_origin::npc_drop, birth) };
	if (grounded == nullptr)
	{
		hb::console::error("lifecyclecheck: item creation failed for the ground probe.");
		return;
	}
	CItem* grounded_raw = grounded.release();
	map->set_item(tile_x, tile_y, grounded_raw);
	const bool ground_stamped = (grounded_raw->m_ground_at != 0);
	const int64_t grounded_serial = grounded_raw->m_serial;

	// Expiry takes only what is due. The second item is placed after the first
	// and then both are aged by hand — the first past the lifetime, the second
	// not — so the check is about the rule, not about waiting five minutes.
	item_ptr fresher{ items.create_item(instanced_id, item_origin::npc_drop, birth) };
	if (fresher == nullptr)
	{
		hb::console::error("lifecyclecheck: item creation failed for the expiry probe.");
		return;
	}
	CItem* fresher_raw = fresher.release();
	map->set_item(tile_x, tile_y, fresher_raw);
	const int64_t fresher_serial = fresher_raw->m_serial;

	constexpr int probe_lifetime_ms = 60000;
	const uint32_t now_ms = grounded_raw->m_ground_at + probe_lifetime_ms;
	grounded_raw->m_ground_at = now_ms - probe_lifetime_ms;        // exactly due
	fresher_raw->m_ground_at = now_ms - (probe_lifetime_ms / 2);   // half its life left

	std::vector<CItem*> expired;
	map->expire_ground_items(tile_x, tile_y, now_ms, probe_lifetime_ms, expired);
	const bool expiry_took_one = (expired.size() == 1)
		&& (!expired.empty() && expired[0]->m_serial == grounded_serial);
	const bool expiry_left_fresher = (map->peek_item(tile_x, tile_y) == fresher_raw);

	for (CItem* item : expired)
		game->despawn_ground_item(item, despawn_reason::expired, map->m_name, tile_x, tile_y);

	// Tile overflow. Filling the tile past its capacity must push the oldest off
	// the end AND record it — the original simply deleted it.
	// The one already sitting there is the one that gets pushed out: the tile
	// keeps the newest at slot 0, so a full tile's worth of arrivals on top of it
	// carries it off the end.
	const int64_t evicted_serial = fresher_serial;
	for (int i = 0; i < hb::server::map::TilePerItems; i++)
	{
		CItem* filler = items.create_item(instanced_id, item_origin::npc_drop, birth);
		if (filler == nullptr) break;
		map->set_item(tile_x, tile_y, filler);
	}

	//----------------------------------------------------------------------
	// Flush and read the columns back.
	//----------------------------------------------------------------------

	ledger.flush(items.serial_high_water());
	sqlite3* db = ledger.handle();

	auto instance_text = [&](const char* column, int64_t serial)
	{
		return probe_text(db, std::format(
			"SELECT {} FROM item_instances WHERE serial={}", column, serial).c_str());
	};
	auto instance_scalar = [&](const char* column, int64_t serial)
	{
		return probe_scalar(db, std::format(
			"SELECT {} FROM item_instances WHERE serial={}", column, serial).c_str());
	};
	auto event_count = [&](int event_type, int64_t serial)
	{
		return hb::server::event_scalar(db, "COUNT(*)", event_type, serial);
	};
	auto event_text = [&](const char* column, int event_type, int64_t serial)
	{
		return hb::server::event_text(db, column, event_type, serial);
	};
	auto event_scalar = [&](const char* column, int event_type, int64_t serial)
	{
		return hb::server::event_scalar(db, column, event_type, serial);
	};

	//----------------------------------------------------------------------
	// Birth context
	//----------------------------------------------------------------------

	tally.record("birth_map_recorded", instance_text("map", placed_serial) == birth_map);
	tally.record("birth_xy_recorded",
		instance_scalar("x", placed_serial) == birth_x
		&& instance_scalar("y", placed_serial) == birth_y);

	// The NPC name reaches origin_detail, its schema home, with the quote intact.
	tally.record("birth_detail_recorded", instance_text("origin_detail", placed_serial) == birth_npc);

	// A venue that knows nothing leaves the columns NULL rather than claiming
	// the top-left corner of a map nobody named.
	tally.record("birth_location_null_without_context",
		instance_text("map", contextless_serial).empty()
		&& instance_scalar("x", contextless_serial) == 0);

	// The loot row's tier is the tier the item actually carries. This is the one
	// create_loot_item exists for: minting before the roll wrote 0 here.
	//
	// Agreement is asserted, never the value: the tier is a live roll, so it
	// differs from run to run and printing it would make the machine line — which
	// the Linux gate compares byte for byte against Windows — disagree with
	// itself on the same machine.
	tally.record("loot_tier_matches_item",
		instance_scalar("tier", looted_serial) == looted_tier);
	tally.record("loot_birth_located", instance_text("map", looted_serial) == birth_map);

	//----------------------------------------------------------------------
	// Destruction
	//----------------------------------------------------------------------

	tally.record("destroy_emits_one_event",
		event_count(destroyed_event_number, doomed_serial) == 1);
	tally.record("destroy_reason_recorded",
		event_text("detail", destroyed_event_number, doomed_serial)
			== std::string("{\"reason\":\"upgrade_break\"}"));
	tally.record("destroy_records_actor",
		event_text("actor_char", destroyed_event_number, doomed_serial) == actor_char);
	tally.record("destroy_nulls_caller_pointer", destroy_nulled);
	tally.record("destroy_counted_silent", counted_silent);

	// Shop purchases reach the ledger as ItemLogAction::Buy's own number.
	tally.record("shop_buy_event_recorded", event_count(buy_event_number, placed_serial) == 1);

	//----------------------------------------------------------------------
	// Ground lifecycle
	//----------------------------------------------------------------------

	tally.record("ground_placement_stamped", ground_stamped);
	tally.record("expiry_takes_only_due", expiry_took_one);
	tally.record("expiry_leaves_fresher", expiry_left_fresher);

	tally.record("despawn_emits_one_event",
		event_count(despawned_event_number, grounded_serial) == 1);
	tally.record("despawn_reason_recorded",
		event_text("detail", despawned_event_number, grounded_serial)
			== std::string("{\"reason\":\"expired\"}"));

	// A despawn has no actor by definition. Storing "" instead of NULL here
	// would make "who last held it" answer with an empty name.
	//
	// The row count is asserted alongside the emptiness on purpose: an absent row
	// also reads as empty, so without it this check would pass loudest exactly
	// when the despawn emitter had stopped working altogether.
	tally.record("despawn_has_no_actor",
		event_count(despawned_event_number, grounded_serial) == 1
		&& event_text("actor_char", despawned_event_number, grounded_serial).empty()
		&& event_text("actor_account", despawned_event_number, grounded_serial).empty());

	// Its location is the tile's, which is the only place it was ever true.
	tally.record("despawn_located_at_tile",
		event_text("map", despawned_event_number, grounded_serial) == map->m_name
		&& event_scalar("x", despawned_event_number, grounded_serial) == tile_x);

	tally.record("overflow_despawns_oldest",
		event_count(despawned_event_number, evicted_serial) == 1);
	tally.record("overflow_reason_recorded",
		event_text("detail", despawned_event_number, evicted_serial)
			== std::string("{\"reason\":\"tile_overflow\"}"));

	//----------------------------------------------------------------------
	// Leave the tile as it was found. Ground items are RAM-only, so nothing
	// here survives a restart — but this command can also be typed on a live
	// server, where a tile left holding a dozen probe items would be litter a
	// player walks into.
	//----------------------------------------------------------------------

	while (map->peek_item(tile_x, tile_y) != nullptr)
	{
		CItem* taken = map->get_item(tile_x, tile_y);
		if (taken == nullptr) break;
		game->despawn_ground_item(taken, despawn_reason::world_shutdown,
			map->m_name, tile_x, tile_y);
	}

	tally.record("probe_tile_left_empty", map->peek_item(tile_x, tile_y) == nullptr);

	tally.report(std::format("instanced item {}, counted item {}, map {}",
		instanced_id, counted_id, map->m_name));
}
