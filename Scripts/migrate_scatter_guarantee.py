"""Add the #89 guaranteed-head columns and author the three boss scatter rosters.

Two things, in one transaction:

1. SCHEMA. `drop_tables` gains `guaranteed_rolls`, `guarantee_item_id` and
   `tail_rarity_divisor`; `drop_entries` gains `count_throws`. Every default
   reproduces the pre-#89 behaviour exactly, so the migration is a no-op for the
   other 97 tables. `drop_entries_readable` is rebuilt because a guarantee row
   carries 0 ppb and `1000000000.0 / 0` is not a number.

2. DATA. Tables 40034 / 40035 / 40036 are rewritten from
   `Scripts/derive_scatter_rosters.py`, which is the authority for both the
   roster and every chance in it. Nothing is hand-typed here; this script imports
   that module and writes what it computes.

Idempotent: safe to re-run. Verify with

    python Scripts/derive_scatter_rosters.py --compare
    Server --dropodds
    Server --scattersmoke 46

Usage:
    python Scripts/migrate_scatter_guarantee.py [--db PATH] [--dry-run]
"""
import argparse
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import derive_scatter_rosters as derive          # noqa: E402

DEFAULT_DB = Path(__file__).resolve().parent.parent / "Binaries" / "Server" / "gamedata.db"

# Rebuilt rather than altered: NULLIF for the guarantee row's 0 ppb, and the new
# columns exposed so the readable view stays the thing you can hand-edit against.
READABLE_VIEW = """
CREATE VIEW drop_entries_readable AS
 SELECT e.drop_table_id, t.name AS table_name, t.stage, e.item_id,
 e.drop_chance_ppb,
 1000000000.0 / NULLIF(e.drop_chance_ppb, 0) AS one_in,
 e.min_count, e.max_count, e.count_throws,
 CASE WHEN e.item_id = t.guarantee_item_id THEN 'guarantee' END AS role
 FROM drop_entries e JOIN drop_tables t USING (drop_table_id)
 ORDER BY e.drop_table_id, e.drop_chance_ppb DESC
"""

NEW_COLUMNS = [
    ("drop_tables", "guaranteed_rolls", "INTEGER NOT NULL DEFAULT 0"),
    ("drop_tables", "guarantee_item_id", "INTEGER NOT NULL DEFAULT 0"),
    ("drop_tables", "tail_rarity_divisor", "INTEGER NOT NULL DEFAULT 1"),
    ("drop_entries", "count_throws", "INTEGER NOT NULL DEFAULT 1"),
]


def columns(con, table):
    return {r[1] for r in con.execute(f"PRAGMA table_info({table})")}


def add_columns(con, dry):
    for table, column, decl in NEW_COLUMNS:
        if column in columns(con, table):
            print(f"  {table}.{column} already present")
            continue
        print(f"  ALTER TABLE {table} ADD COLUMN {column} {decl}")
        if not dry:
            con.execute(f"ALTER TABLE {table} ADD COLUMN {column} {decl}")


def rebuild_view(con, dry):
    print("  rebuilding view drop_entries_readable")
    if dry:
        return
    con.execute("DROP VIEW IF EXISTS drop_entries_readable")
    con.execute(READABLE_VIEW)


def write_rosters(con, dry):
    """Rewrite the three scatter tables from the derivation module."""
    names = {r[0]: r[1] for r in con.execute("select item_id, name from items")}

    for npc_id, npc_type, label, table_id, i_min, i_max, jackpot, rare in derive.BOSSES:
        head = i_min + 1
        roster, _orig, _tier = derive.build_roster(npc_type, jackpot, rare)
        rows = {i: derive.ppb_of(c) for i, c in roster.items()}

        total = sum(rows.values())
        if total > derive.PPB:
            sys.exit(f"{label}: rows sum to {total} ppb, over the denominator")

        unknown = [i for i in rows if i not in names]
        if unknown:
            sys.exit(f"{label}: rows reference ids absent from items: {unknown}")

        print(f"\n  --- {label} (table {table_id}) ---")
        print(f"    {len(rows)} rows, {total:,} ppb, head {head} of {i_max} rolls,"
              f" guarantee {derive.GOLD}, tail divisor 5")

        if dry:
            continue

        con.execute(
            "UPDATE drop_tables SET roll_count_min = ?, roll_count_max = ?,"
            " placement = 'spiral', delay = 'decay', guaranteed_rolls = ?,"
            " guarantee_item_id = ?, tail_rarity_divisor = ?"
            " WHERE drop_table_id = ?",
            (i_max, i_max, head, derive.GOLD, 5, table_id))
        if con.total_changes == 0:
            sys.exit(f"{label}: drop_tables has no row {table_id}")

        con.execute("DELETE FROM drop_entries WHERE drop_table_id = ?", (table_id,))
        con.executemany(
            "INSERT INTO drop_entries (drop_table_id, item_id, drop_chance_ppb,"
            " min_count, max_count, count_throws) VALUES (?, ?, ?, 1, 1, 1)",
            [(table_id, i, ppb) for i, ppb in sorted(rows.items())])

        # Gold is not a competitor: 0 ppb, present only to say how big a pile is.
        # 10 throws of 1..15000 is the original's own iDice(10, 15000).
        con.execute(
            "INSERT INTO drop_entries (drop_table_id, item_id, drop_chance_ppb,"
            " min_count, max_count, count_throws) VALUES (?, ?, 0, ?, ?, ?)",
            (table_id, derive.GOLD, derive.GOLD_MIN, derive.GOLD_MAX,
             derive.GOLD_THROWS))


def report(con):
    print("\n=== as written ===")
    for _npc, _type, label, table_id, _mn, _mx, _j, _r in derive.BOSSES:
        row = con.execute(
            "SELECT roll_count_min, roll_count_max, placement, delay,"
            " guaranteed_rolls, guarantee_item_id, tail_rarity_divisor"
            " FROM drop_tables WHERE drop_table_id = ?", (table_id,)).fetchone()
        n, total = con.execute(
            "SELECT COUNT(*), COALESCE(SUM(drop_chance_ppb), 0) FROM drop_entries"
            " WHERE drop_table_id = ?", (table_id,)).fetchone()
        print(f"  {table_id} {label:<12} rolls {row[0]}-{row[1]} {row[2]}/{row[3]}"
              f"  head {row[4]} -> item {row[5]}  tail /{row[6]}"
              f"  {n} rows  {total:,} ppb")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(DEFAULT_DB))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    db = Path(args.db)
    if not db.exists():
        sys.exit(f"no such database: {db}")

    con = sqlite3.connect(db)
    con.execute("PRAGMA foreign_keys = ON")
    print(f"{'DRY RUN on' if args.dry_run else 'migrating'} {db}\n")
    print("schema:")
    add_columns(con, args.dry_run)
    rebuild_view(con, args.dry_run)
    print("\nrosters:")
    write_rosters(con, args.dry_run)

    if args.dry_run:
        con.rollback()
        print("\ndry run - nothing written")
        return
    con.commit()
    report(con)
    print("\nmigrated. Re-run Server --dropodds and --scattersmoke to verify.")


if __name__ == "__main__":
    main()
