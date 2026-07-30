// CmdSerialCheck.cpp: Serial identity prover (Item Provenance Ledger P1.1, #75)
//
// The minting factory's contract, provable headlessly. ADR 0003 makes identity
// the primitive the whole ledger rests on, so "it compiles" is not evidence:
// what matters is that an Instanced item cannot exist without a unique Serial,
// that a Counted one never gets a spurious identity, and that a save/load round
// trip or an in-place upgrade does not silently issue a second Serial for one
// physical item. Each of those is a way Reconciliation could later report a
// dupe that never happened.
//
// Nothing enters the world and no client is involved — the same role mintcheck
// plays for the legality gate and rollsmoke plays for the drop roll.
//
// The machine-readable lines carry no absolute Serial numbers on purpose: the
// allocator's position depends on what the process did before this ran, and the
// point of the Linux gate is a byte-identical comparison against Windows.
//
// Design contract: PLANS/ItemLedger_Plan.md P1.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdSerialCheck.h"
#include "CheckTally.h"
#include "ServerConsole.h"
#include "Game.h"
#include "GmMintSpec.h"          // split_args
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "Item.h"
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace item_origin = hb::server::item_origin;
using hb::server::check_tally;
using hb::server::find_probe_item;

namespace
{
	// A Serial well above anything a fresh allocator would hand out, so
	// "restore lifted the high-water mark" is unambiguous rather than lucky.
	constexpr int64_t stored_serial_probe = 5000000;
}

void CmdSerialCheck::execute(CGame* game, const char* args)
{
	const std::vector<std::string> tokens = hb::server::split_args(args);

	int instanced_id = -1;
	int counted_id = -1;
	if (tokens.size() >= 1) std::sscanf(tokens[0].c_str(), "%d", &instanced_id);
	if (tokens.size() >= 2) std::sscanf(tokens[1].c_str(), "%d", &counted_id);

	if (instanced_id < 0) instanced_id = find_probe_item(game, false);
	if (counted_id < 0) counted_id = find_probe_item(game, true);

	if (game->m_item_manager->is_valid_item_id(instanced_id) == false)
	{
		hb::console::error("serialcheck: no usable non-stackable item id.");
		return;
	}
	if (game->m_item_manager->is_valid_item_id(counted_id) == false)
	{
		hb::console::error("serialcheck: no usable stackable item id.");
		return;
	}
	if (game->m_item_config_list[instanced_id]->is_stackable())
	{
		hb::console::error("serialcheck: item {} is stackable - it cannot probe the Instanced path.", instanced_id);
		return;
	}
	if (game->m_item_config_list[counted_id]->is_stackable() == false)
	{
		hb::console::error("serialcheck: item {} is not stackable - it cannot probe the Counted path.", counted_id);
		return;
	}

	ItemManager& items = *game->m_item_manager;
	check_tally tally("SERIALCHECK", "serialcheck");

	// unique_ptr because every probe below is a world-less item that must be
	// released on every exit path, including the early returns.
	using item_ptr = std::unique_ptr<CItem>;

	// 1-2. An Instanced item is born with a Serial, and two of them never
	//      collide. Monotonic-and-unique is the property the ledger joins on.
	item_ptr first{ items.create_item(instanced_id, item_origin::npc_drop) };
	item_ptr second{ items.create_item(instanced_id, item_origin::npc_drop) };
	if (first == nullptr || second == nullptr)
	{
		hb::console::error("serialcheck: create_item failed for item {}.", instanced_id);
		return;
	}

	tally.record("instanced_minted", first->m_serial != 0 && second->m_serial != 0);
	tally.record("instanced_unique", second->m_serial > first->m_serial);

	// 3. Origin is recorded at creation — it is the other half of provenance,
	//    and #76 reads it off the item when it writes the instance row.
	tally.record("origin_recorded", first->m_origin == item_origin::npc_drop);

	// 4. A Counted item stays unserialed. Stack merge and split dissolve
	//    identity, so a Serial there would be a lie the ledger then has to
	//    reconcile against nothing.
	//
	//    There is deliberately no "counted origin is none" check: it would only
	//    assert that the factory stored the value just handed to it, which
	//    origin_recorded already covers. A vacuous check makes the pass count
	//    read stronger than the evidence is.
	item_ptr counted{ items.create_item(counted_id, item_origin::none) };
	if (counted == nullptr)
	{
		hb::console::error("serialcheck: create_item failed for item {}.", counted_id);
		return;
	}
	tally.record("counted_unserialed", counted->m_serial == 0);

	// 6-7. Restoring a persisted item carries its stored Serial instead of
	//      minting, and lifts the allocator above it. Without the lift, a fresh
	//      in-RAM counter would re-issue a Serial a loaded item already holds —
	//      one Serial in two inventories, the exact anomaly this exists to catch.
	item_ptr restored{ items.restore_item(instanced_id, stored_serial_probe) };
	if (restored == nullptr)
	{
		hb::console::error("serialcheck: restore_item failed for item {}.", instanced_id);
		return;
	}
	tally.record("restore_carries", restored->m_serial == stored_serial_probe);

	item_ptr after_restore{ items.create_item(instanced_id, item_origin::npc_drop) };
	if (after_restore == nullptr)
	{
		hb::console::error("serialcheck: create_item failed after restore.");
		return;
	}
	tally.record("restore_high_water", after_restore->m_serial > stored_serial_probe);

	// 8. A restore with no stored Serial still mints one. Since #77 persists the
	//    column a 0 means the row is Counted or predates the column — either
	//    way an Instanced item must never sit in the world without identity.
	item_ptr restored_blank{ items.restore_item(instanced_id, 0) };
	if (restored_blank == nullptr)
	{
		hb::console::error("serialcheck: restore_item(0) failed for item {}.", instanced_id);
		return;
	}
	tally.record("restore_zero_mints",
		restored_blank->m_serial != 0 && restored_blank->m_serial != stored_serial_probe);

	// 9-10. An in-place transform keeps the original's identity and origin. A
	//       fresh Serial here would split one physical item's Biography into two
	//       instances that never met, and burn an allocator value on a phantom.
	item_ptr transformed{ items.transform_item(instanced_id, *first) };
	if (transformed == nullptr)
	{
		hb::console::error("serialcheck: transform_item failed for item {}.", instanced_id);
		return;
	}
	tally.record("transform_carries_serial", transformed->m_serial == first->m_serial);
	tally.record("transform_carries_origin", transformed->m_origin == first->m_origin);

	tally.report(std::format("instanced item {}, counted item {}", instanced_id, counted_id));
}
