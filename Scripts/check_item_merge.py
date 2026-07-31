#!/usr/bin/env python3
"""The third half of the Provenance Ledger coverage law (ADR 0003, issue #81).

check_item_factory.py  -- no item is BORN outside the minting factory.
check_item_destroy.py  -- no item DIES outside the destruction funnel.
this one              -- no item is silently ABANDONED by a stack merge.

`add_client_item_list` is the one place in the server that can accommodate an
item without taking ownership of the object. When the incoming stack merges into
one the character already had, it copies the count across, sets `*del_req = 1`
and returns true -- the husk is then the caller's to free. The `delete` is
commented out inside the function precisely because the caller still has to
describe the item afterwards (it logs it, and it packs it into a client packet),
so freeing it there would be a use-after-free.

That makes forgetting the flag both easy and invisible: the merge worked, the
player got their items, and one CItem is orphaned per merge. It leaked on the
hottest paths in the game -- picking up gold, being given gold, shop sale
proceeds -- and it is a REGRESSION, not inherited: the original frees it inline
(`if (iEraseReq == 1) delete pItemGold;`, HB382_CENTUU/HGServer/Game.cpp:30900).

The ledger consequence is why this lives beside the other two: a husk that is
never destroyed is a mutation path that never emits. It cannot orphan a Serial
(only stackables merge, and stackables are Counted, so they carry none), but the
Counted flow accounting is only trustworthy if every merge is accounted for --
and `destroy_reason::merged` is what tells the ledger to book NO flow, because
nothing left the world.

The rule is uniform and has no allowlist on purpose. "Only non-stackables reach
this site" is a per-site proof that rots the moment an item's config flag
changes in gamedata.db, and a wrong entry here is a silent leak again. One line
at every site is cheaper than a claim nobody re-checks.

Usage:
    python Scripts/check_item_merge.py          # report, exit 1 on violation
    python Scripts/check_item_merge.py --list   # also list the handled sites

Exit codes: 0 = clean, 1 = unhandled merge flag(s).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCES = REPO / "Sources"

# The call, capturing the out-param it was handed: `..., &erase_req)`.
CALL = re.compile(r"add_client_item_list\s*\([^;]*?&\s*(\w+)\s*\)")

# A genuine READ of that flag -- tested or compared, not merely declared or
# passed by address again.
def read_pattern(var: str) -> re.Pattern:
    return re.compile(rf"(if\s*\(\s*{var}\b|\b{var}\s*[=!]=|\(\s*{var}\s*[=!]=)")

# How far to look. Allman braces put a bare `}` at column 0 at the end of every
# function, which bounds the search far more reliably than counting braces
# through the macro-free but deeply nested handler bodies here.
MAX_LOOKAHEAD = 90


def scan() -> tuple[list[tuple[str, int, str]], list[tuple[str, int, str, int]]]:
    """Return (violations, handled). Handled carries the line that reads it."""
    violations: list[tuple[str, int, str]] = []
    handled: list[tuple[str, int, str, int]] = []

    for path in sorted(SOURCES.rglob("*.cpp")):
        if {".vs", "Debug_x64", "Release_x64", "build_linux", "x64"} & set(path.parts):
            continue

        rel = path.relative_to(REPO).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        for n, line in enumerate(lines):
            # The definition itself is not a call site.
            if "::add_client_item_list" in line or line.lstrip().startswith("//"):
                continue
            m = CALL.search(line)
            if not m:
                continue

            reads = read_pattern(m.group(1))
            found = 0
            for j in range(n + 1, min(n + 1 + MAX_LOOKAHEAD, len(lines))):
                if lines[j] == "}":        # end of the enclosing function
                    break
                if reads.search(lines[j]):
                    found = j + 1
                    break

            if found:
                handled.append((rel, n + 1, line.strip(), found))
            else:
                violations.append((rel, n + 1, line.strip()))

    return violations, handled


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", action="store_true",
                    help="list the call sites that do handle the flag")
    args = ap.parse_args()

    violations, handled = scan()

    if args.list:
        print(f"Handled merge flags: {len(handled)}\n")
        for rel, n, text, at in handled:
            print(f"  {rel}:{n}: {text}")
            print(f"      -> freed/read at line {at}")
        print()

    if violations:
        print(f"COVERAGE LAW VIOLATION: {len(violations)} add_client_item_list "
              f"call site(s) ignore the merge flag:\n")
        for rel, n, text in violations:
            print(f"  {rel}:{n}: {text}")
        print("\nWhen the stack merges, that call returns true WITHOUT taking the "
              "object. The husk is leaked and its exit is never recorded.")
        print("\nAdd, after the last read of the item (it still has to be logged "
              "and packed into the client packet first):")
        print("  if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);")
        print("\nPut it before any switch that can return, or the error paths "
              "leak the husk anyway.")
        return 1

    print(f"OK: every add_client_item_list call site handles its merge flag "
          f"({len(handled)} sites).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
