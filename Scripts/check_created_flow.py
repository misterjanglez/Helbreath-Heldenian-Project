#!/usr/bin/env python3
"""Enforce the Counted-creation booking law (issue #111, ItemLedger_Plan footnote 1).

A Counted (stackable) item has no Serial, so its entry into the world is a
`created` flow booked BY THE VENUE — the factory cannot book it, because every
venue sets the count after `create_item` returns. That makes venue coverage a
roster, and #111's sweep found the roster is exactly the thing that rots: five
gold mints and every gather venue were unbooked, silently.

This script pins the roster the way check_item_factory.py pins the factory:
every `create_item` / `create_loot_item` CALL SITE in Sources/Server is either
inside a file whose site count and booking story are declared below, or it is a
violation. A new venue fails the check until somebody classifies it — names
which transition books its Counted entries, or books them with
`record_created_flow` — which is the deliberate act the ledger's coverage
depends on.

Prover files (Cmd*Check.cpp) are exempt: they create probes against a scratch
sink, and their call sites grow with every ticket.

Usage:
    python Scripts/check_created_flow.py          # report, exit 1 on violation
    python Scripts/check_created_flow.py --list   # also list every pinned site

Exit codes: 0 = clean, 1 = unclassified venue, 2 = the roster itself is stale.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SERVER = REPO / "Sources" / "Server"

# file (relative to Sources/Server) -> (expected call sites, how each site's
# Counted entry reaches the ledger). A count that stops matching means a venue
# was added, removed, or moved: update the entry AND make sure the new site
# books — `record_created_flow` if nothing else carries the quantity.
ROSTER: dict[str, tuple[int, str]] = {
    "CraftingManager.cpp": (2, "both products book record_created_flow (#111)"),
    "EntityManager.cpp": (1, "NPC drop; item_log(NewGenDrop) books after the count roll"),
    "FishingManager.cpp": (1, "generator mints; create_fish books record_created_flow once registered (#111)"),
    "Game.cpp": (6, "3 gold mints book record_created_flow (#111); memorial ring, "
                    "lottery and angel handlers make Instanced items (factory event)"),
    "GameCmdCreateItem.cpp": (1, "GM mint; record_gm_mint books per copy (#104)"),
    "GameCmdGiveItem.cpp": (1, "GM mint; record_gm_mint books per copy (#104)"),
    "CmdGiveItem.cpp": (1, "headless GM give; record_gm_mint books per copy (#104)"),
    "ItemManager.cpp": (13, "sale proceeds, BuildItem and slates book "
                            "record_created_flow (#111); both GM mint doors set "
                            "count 1 for their GmMint flows; shop buy books Buy; "
                            "splits and exchange copies are movements (flow_none "
                            "contract); butcher/hero/dark products are Instanced"),
    "LootManager.cpp": (1, "reward gold books record_created_flow (#111)"),
    "MagicManager.cpp": (1, "conjured food is Instanced; factory event + item_log(Drop)"),
    "MiningManager.cpp": (1, "ore books record_created_flow (#111)"),
    "QuestManager.cpp": (1, "reward books record_created_flow before the receive check (#111)"),
    "StatusEffectManager.cpp": (1, "crops book record_created_flow (#111)"),
    "WarManager.cpp": (1, "occupation flag is Instanced (factory event)"),
}

PROVER = re.compile(r"^Cmd\w*Check\.cpp$")
CALL = re.compile(r"\bcreate_(?:loot_)?item\s*\(")
DEFINITION = re.compile(r"ItemManager::create_(?:loot_)?item|CItem\s*\*\s*create_")
LINE_COMMENT = re.compile(r"^\s*//")


def strip_block_comments(text: str) -> list[str]:
    """Lines with /* ... */ regions blanked, so dead code cannot count."""
    out: list[str] = []
    in_block = False
    for line in text.splitlines():
        buf: list[str] = []
        i = 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    break
                in_block = False
                i = end + 2
            else:
                start = line.find("/*", i)
                if start < 0:
                    buf.append(line[i:])
                    break
                buf.append(line[i:start])
                in_block = True
                i = start + 2
        out.append("".join(buf))
    return out


def scan() -> dict[str, list[tuple[int, str]]]:
    """Map of file name -> creation call sites as (lineno, text)."""
    sites: dict[str, list[tuple[int, str]]] = {}
    for path in sorted(SERVER.glob("*.cpp")):
        name = path.name
        if PROVER.match(name):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for n, line in enumerate(strip_block_comments(text), 1):
            if not CALL.search(line):
                continue
            if LINE_COMMENT.match(line) or DEFINITION.search(line):
                continue
            sites.setdefault(name, []).append((n, line.strip()))
    return sites


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", action="store_true", help="list every pinned site")
    args = ap.parse_args()

    sites = scan()
    status = 0

    unrostered = sorted(set(sites) - set(ROSTER))
    if unrostered:
        print("COVERAGE LAW VIOLATION: item-creation call sites in files the "
              "roster does not classify:\n")
        for name in unrostered:
            for n, text in sites[name]:
                print(f"  Sources/Server/{name}:{n}: {text}")
        print("\nA Counted item minted here enters the world with no `created` "
              "flow unless the venue books one. Classify the file in "
              "Scripts/check_created_flow.py: name the transition that books "
              "its Counted entries, or call record_created_flow once the "
              "count is real (see ItemManager.h).")
        status = 1

    for name, (expected, note) in sorted(ROSTER.items()):
        actual = len(sites.get(name, []))
        if actual != expected:
            print(f"\nSTALE ROSTER: Sources/Server/{name} holds {actual} "
                  f"creation call site(s) but the roster declares {expected}.")
            print(f"  Declared story: {note}")
            print("  If you added a venue, make its Counted entry book and "
                  "update the roster; if you removed or moved one, update the "
                  "count so the roster stays evidence.")
            status = max(status, 2)

    if args.list:
        for name in sorted(sites):
            for n, text in sites[name]:
                print(f"  Sources/Server/{name}:{n}: {text}")

    if status == 0:
        total = sum(len(v) for v in sites.values())
        print(f"OK: {total} item-creation call sites across {len(sites)} files, "
              f"all classified for Counted-entry booking ({len(ROSTER)} roster entries).")
    return status


if __name__ == "__main__":
    sys.exit(main())
