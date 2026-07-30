#!/usr/bin/env python3
"""Enforce the Item Provenance Ledger coverage law (ADR 0003, issue #75).

Item creation must funnel through ItemManager's factory so that every Instanced
item is born with a Serial and an origin. ADR 0003 states that after the #75
refactor "a raw `new CItem` in game code becomes a review defect" — this script
is what makes that mechanical instead of a thing reviewers have to remember.

An audit log with gaps is worse than none: one forgotten `new CItem` is an item
with no Serial, which shows up later as a Reconciliation anomaly that no amount
of querying can explain.

Usage:
    python Scripts/check_item_factory.py          # report, exit 1 on violation
    python Scripts/check_item_factory.py --list   # also list the sanctioned sites

Exit codes: 0 = clean, 1 = violation(s) found, 2 = the allowlist itself is stale.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCES = REPO / "Sources"

# The factory itself is the only place allowed to allocate a CItem. Anything
# else routes through create_item / restore_item / transform_item /
# create_template / create_snapshot.
FACTORY_FILE = "Sources/Server/ItemManager.cpp"

# How many allocations the factory is expected to contain, one per named entry
# point. Pinning the count means a new `new CItem` cannot be quietly parked
# inside the factory file either — adding an entry point is a deliberate act
# that updates this number and says why.
FACTORY_ENTRY_POINTS = {
    "create_item(int item_id, ...)",
    "create_item(const char* item_name, ...)",
    "restore_item",
    "transform_item",
    "create_template",
    "create_snapshot",
}

NEW_CITEM = re.compile(r"\bnew\s+CItem\b")
# Line comments and the ADR quotes in headers talk *about* `new CItem`; only
# real code counts.
COMMENT = re.compile(r"^\s*(//|\*|/\*)")


def scan() -> tuple[list[tuple[str, int, str]], list[tuple[str, int, str]]]:
    """Return (violations, sanctioned) as (relpath, lineno, text) triples."""
    violations: list[tuple[str, int, str]] = []
    sanctioned: list[tuple[str, int, str]] = []

    for path in sorted(SOURCES.rglob("*")):
        if path.suffix not in (".cpp", ".h", ".hpp", ".cc"):
            continue
        # Build output and IDE caches are not source.
        parts = set(path.parts)
        if parts & {".vs", "Debug_x64", "Release_x64", "build_linux", "x64"}:
            continue

        rel = path.relative_to(REPO).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        for n, line in enumerate(lines, 1):
            if not NEW_CITEM.search(line) or COMMENT.match(line):
                continue
            entry = (rel, n, line.strip())
            (sanctioned if rel == FACTORY_FILE else violations).append(entry)

    return violations, sanctioned


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", action="store_true",
                    help="list the sanctioned factory allocations too")
    args = ap.parse_args()

    violations, sanctioned = scan()

    if args.list or violations:
        print(f"Factory allocations in {FACTORY_FILE}: "
              f"{len(sanctioned)} (expected {len(FACTORY_ENTRY_POINTS)})")
        if args.list:
            for rel, n, text in sanctioned:
                print(f"  {rel}:{n}: {text}")

    status = 0

    if len(sanctioned) != len(FACTORY_ENTRY_POINTS):
        print(f"\nSTALE ALLOWLIST: the factory holds {len(sanctioned)} allocations "
              f"but {len(FACTORY_ENTRY_POINTS)} entry points are declared.")
        print("If you added a creation entry point, add it to FACTORY_ENTRY_POINTS "
              "and say why in the commit. If you parked a `new CItem` inside the "
              "factory file to dodge this check, don't.")
        status = 2

    if violations:
        print(f"\nCOVERAGE LAW VIOLATION: {len(violations)} raw `new CItem` "
              f"outside the factory:\n")
        for rel, n, text in violations:
            print(f"  {rel}:{n}: {text}")
        print("\nAn item created outside ItemManager's factory has no Serial and "
              "no origin, so it is invisible to the Provenance Ledger and will "
              "surface later as an unexplainable Reconciliation anomaly.")
        print("\nUse instead (Sources/Server/ItemManager.h):")
        print("  create_item(id|name, origin)  - a new item entering the world")
        print("  restore_item(id, serial)      - rehydrating a persisted item")
        print("  transform_item(new_id, from)  - in-place evolution, carries identity")
        print("  create_template()             - an item-config type template")
        print("  create_snapshot(source)       - a throwaway copy for a log line")
        status = 1

    if status == 0:
        print(f"OK: no raw `new CItem` outside {FACTORY_FILE} "
              f"({len(sanctioned)} sanctioned factory allocations).")

    return status


if __name__ == "__main__":
    sys.exit(main())
