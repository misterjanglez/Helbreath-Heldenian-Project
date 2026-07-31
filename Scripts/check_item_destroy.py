#!/usr/bin/env python3
"""Enforce the other half of the Provenance Ledger coverage law (ADR 0003, issue #79).

check_item_factory.py makes sure no item is *born* outside ItemManager's factory.
This makes sure no item *dies* outside ItemManager's destruction funnel.

The asymmetry that motivates it: an item whose creation is missed shows up
immediately as an unserialed instance, but an item whose destruction is missed
looks exactly like a live one. Its Serial has a birth row, no exit event, and no
holder in the Game DB — which is precisely the shape Reconciliation reports as a
duplication anomaly. Every silent `delete` is therefore a future false positive
in the one report the whole effort exists to produce.

So every `delete` of an item is either:

  * routed through `ItemManager::destroy_item` / `despawn_item`, or
  * listed below with a reason it is not an exit.

The second list is the interesting one. Most freed items are NOT destructions —
the object is released because its contents moved somewhere else (to the ground,
into a bank slot, into Trading Post escrow, into a stack it merged with), or
because it was never a world item at all (a config template, a log-line
snapshot). Emitting `destroyed` for those would be worse than emitting nothing:
`transform_majestic_item` frees the item it just carried a live Serial *off* of,
so a blanket rule there would mark an item the player is still wearing as gone.

Usage:
    python Scripts/check_item_destroy.py          # report, exit 1 on violation
    python Scripts/check_item_destroy.py --list   # also list the sanctioned sites

Exit codes: 0 = clean, 1 = unclassified delete(s), 2 = the allowlist is stale.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCES = REPO / "Sources"

# `delete <expr>` where the expression mentions an item. Deliberately textual:
# the point is to catch a new `delete` the day it is written, and anything that
# frees something called *item* is worth a human deciding about.
DELETE_ITEM = re.compile(r"\bdelete\s+([A-Za-z_][A-Za-z0-9_\.\[\]>\-]*)")
COMMENT = re.compile(r"^\s*(//|\*|/\*)")

# Sanctioned deletes: (file, exact source text) -> why this is not an exit.
#
# Keyed on the statement text rather than a line number so the list survives
# edits above it. If the same statement appears twice in one file, both are
# covered by the one entry — they are the same decision.
ALLOWED: dict[tuple[str, str], str] = {
    # --- the funnels themselves -----------------------------------------
    ("Sources/Server/ItemManager.cpp", "delete item;"):
        "the factory's own failure paths (init failed, never stamped, no Serial) "
        "and the body of destroy_item/despawn_item, which are the funnel",
    ("Sources/Server/Game.cpp", "delete item;"):
        "despawn_ground_item's fallback when the item manager is already gone "
        "during shutdown teardown - leaking would be a worse answer",

    # --- not world items ------------------------------------------------
    ("Sources/Server/Game.cpp", "if (m_item_config_list[i] != 0) delete m_item_config_list[i];"):
        "item-config templates (create_template): a type, not an instance",
    ("Sources/Server/Game.cpp", "if (m_build_item_list[i] != 0) delete m_build_item_list[i];"):
        "build-recipe templates, same as the item config list",
    ("Sources/Server/ItemManager.cpp", "delete m_game->m_item_config_list[i];"):
        "item-config templates, freed on reload",
    ("Sources/Server/GameConfigSqliteStore.cpp", "delete itemList[item_id];"):
        "item-config templates being replaced during a config load",
    ("Sources/Server/GameConfigSqliteStore.cpp", "delete game->m_build_item_list[i];"):
        "build-recipe templates being replaced during a config load",
    ("Sources/Server/Game.cpp", "delete m_item_manager;"):
        "the manager object, not an item",

    # --- log-line snapshots (carry a LIVE Serial - never an exit) --------
    ("Sources/Server/ItemManager.cpp", "delete item_bcopy[i];"):
        "create_snapshot copy: it shares the source item's Serial and exists only "
        "to describe it in one Exchange log line. Recording an exit here would "
        "mark an item the player still holds as destroyed",
    ("Sources/Server/ItemManager.cpp", "delete item_acopy[i];"):
        "create_snapshot copy, as above",

    # --- identity carried forward (NOT an exit) -------------------------
    ("Sources/Server/ItemManager.cpp", "delete old_item;"):
        "transform_majestic_item: transform_item already carried this item's "
        "Serial onto its replacement, so the item lives on and only the old "
        "object is freed. An exit event here would kill a live Serial",

    # --- custody moved elsewhere, object freed --------------------------
    ("Sources/Server/TradingPostStore.cpp", "delete client->m_item_list[p.slot];"):
        "Trading Post escrow: the item is now persisted in the listing, so this "
        "frees the inventory copy of something that still exists. The custody "
        "move itself is recorded by record_escrow_event (#80), and the Serial on "
        "the escrow row is what the rebuilt item comes back out with",
    # --- RAM release of something the Game DB still holds ---------------
    ("Sources/Server/Client.cpp", "delete m_item_list[i];"):
        "logout teardown: the character's rows stay in game.db, so this frees "
        "RAM rather than ending an item",
    ("Sources/Server/Client.cpp", "delete m_item_in_bank_list[i];"):
        "logout teardown of the bank list, as above",
    ("Sources/Server/Game.cpp", "delete m_client_list[client_h]->m_item_list[i];"):
        "login load clearing stale in-RAM inventory before reading game.db",
    ("Sources/Server/Game.cpp", "delete m_client_list[client_h]->m_item_in_bank_list[i];"):
        "login load clearing the stale in-RAM bank list",
    ("Sources/Server/Game.cpp", "delete m_client_list[client_h]->m_item_list[item.slot];"):
        "login load replacing a slot with the row just read from game.db",
    ("Sources/Server/Game.cpp", "delete m_client_list[client_h]->m_item_in_bank_list[item.slot];"):
        "login load replacing a bank slot with the row just read from game.db",

    # --- shutdown backstop ----------------------------------------------
    ("Sources/Server/Tile.cpp", "if (m_item[i] != 0) delete m_item[i];"):
        "~CTile. CGame::quit despawns every ground item before the maps are "
        "deleted, so by then this frees nothing; it stays as the backstop for a "
        "map destroyed outside that path",
    ("Sources/Server/Fish.h", "if (m_item != 0) delete m_item;"):
        "~CFish: the fish on a hook was never on a tile or in an inventory",
}


def scan() -> tuple[list[tuple[str, int, str]], list[tuple[str, int, str]]]:
    """Return (violations, sanctioned) as (relpath, lineno, text) triples."""
    violations: list[tuple[str, int, str]] = []
    sanctioned: list[tuple[str, int, str]] = []

    for path in sorted(SOURCES.rglob("*")):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        parts = set(path.parts)
        if parts & {".vs", "Debug_x64", "Release_x64", "build_linux", "x64"}:
            continue

        rel = path.relative_to(REPO).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        for n, line in enumerate(lines, 1):
            m = DELETE_ITEM.search(line)
            if not m or COMMENT.match(line):
                continue
            if "item" not in m.group(1).lower():
                continue
            text = line.strip()
            entry = (rel, n, text)
            (sanctioned if (rel, text) in ALLOWED else violations).append(entry)

    return violations, sanctioned


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", action="store_true",
                    help="list the sanctioned non-exit deletes and why each is one")
    args = ap.parse_args()

    violations, sanctioned = scan()
    status = 0

    if args.list:
        print(f"Sanctioned non-exit deletes: {len(sanctioned)}\n")
        for rel, n, text in sanctioned:
            print(f"  {rel}:{n}: {text}")
            print(f"      -> {ALLOWED[(rel, text)]}")

    # A stale entry is a real problem: it means the code it described is gone,
    # so the reason nobody re-checked it is gone too.
    seen = {(rel, text) for rel, _, text in sanctioned}
    stale = sorted(set(ALLOWED) - seen)
    if stale:
        print(f"\nSTALE ALLOWLIST: {len(stale)} entr(ies) match nothing in the tree:\n")
        for rel, text in stale:
            print(f"  {rel}: {text}")
        print("\nThe code they described has moved or gone. Re-check the site and "
              "either update the entry or drop it - a reason nobody can point at "
              "is not a reason.")
        status = 2

    if violations:
        print(f"\nCOVERAGE LAW VIOLATION: {len(violations)} unclassified item "
              f"delete(s):\n")
        for rel, n, text in violations:
            print(f"  {rel}:{n}: {text}")
        print("\nAn item freed without an event leaves a Serial with a birth row, "
              "no exit and no holder - which Reconciliation cannot tell apart "
              "from a duplicated item.")
        print("\nUse instead (Sources/Server/ItemManager.h):")
        print("  destroy_item(item, reason, actor_h)      - it left the population")
        print("  despawn_item(item, reason, map, x, y)    - it left the ground, no actor")
        print("\nIf it is genuinely not an exit - the contents moved elsewhere, or it "
              "was never a world item - add it to ALLOWED in this script with the "
              "reason, so the next reader does not have to work it out again.")
        status = 1

    if status == 0:
        print(f"OK: every item delete is either funnelled or classified "
              f"({len(sanctioned)} sanctioned non-exit deletes).")

    return status


if __name__ == "__main__":
    sys.exit(main())
