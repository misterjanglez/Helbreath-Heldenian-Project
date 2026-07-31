# Per-account database files consolidate into one Game DB

The account store has been one SQLite file per account (`accounts/<name>.db`) — a layout inherited
from flat-file thinking when the ASCII store was migrated. It worked, but it forfeits the defining
wins of a relational store, and one of its costs is structural: SQLite transactions across ATTACHed
files are **not atomic in WAL mode**, and WAL is now mandatory (ADR 0003). An Exchange between two
players saves two account files in two separate commits; a crash between them duplicates or destroys
an item — the exact exploit class the Provenance Ledger effort exists to close. No amount of careful
code fixes this under the per-file layout. Design contract: `PLANS/ItemLedger_Plan.md` (D7).

Decision: the per-account files are replaced by a single **`game.db`** — `account_name` columns,
globally `UNIQUE` character names, one persistent WAL connection with cached prepared statements,
mass-save sweeps batched into one transaction. Multi-account operations (Exchange both-parties
commit, Trading Post custody moves) become single atomic transactions. Scope is the account store
only: `gamedata.db` (static config), `MapInfo.db`, and `itemledger.db` (append-only, different
growth/backup profile) stay separate files.

The timing is deliberate: the project is in Alpha, worlds wipe freely, and the persistence layer is
already churning for Serial columns — so consolidation shares one schema epoch (v8) with ADR 0003,
ships **zero migration code** (the schema gate refuses the old layout; the world is wiped), and
lands at the only moment it will ever be this cheap. After go-live this change would require a real
migration tool against live player data.

## Considered options

- **Keep per-account files** — rejected: permanently non-atomic multi-account commits under WAL;
  name uniqueness enforceable only by directory-scan application code; every cross-account query
  (reconciliation, rankings, `CountAccountStats`) pays an open-every-file walk.
- **ATTACH-based cross-file transactions** — rejected: atomic only in rollback-journal mode;
  mutually exclusive with WAL, which the write path requires.
- **Defer consolidation to a later effort** — rejected once wipe-freedom removed the migration
  risk: deferral would churn the persistence layer twice and push the change past go-live, where
  it becomes a live-data migration.

## Consequences

- The dupe window in multi-account persistence closes structurally; the trade-commit path wraps
  both parties' saves in one transaction (ledger plan P3.4), and the Trading Post's custody moves
  can join the same transaction boundary (implementation-time pick: its listings table moves into
  `game.db`, coordinated with that workstream).
  **Delivered 2026-07-31 (#82), schema epoch v9.** The pick was taken as written: all five escrow
  tables (`listings`, `listing_items`, `offers`, `offer_items`, `notices`) are part of the v9
  `game.db` schema, and `trading_post_store` borrows that connection rather than owning one. Give
  turned out to be the same two-account shape as Exchange and takes the same primitive. The
  consequence worth recording is one ADR 0001 did not anticipate: with both halves in one
  transaction, an escrow step that fails **rewinds** instead of resolving to the designed loss —
  so "fail as loss rather than duplication" is now the fallback for a transaction that cannot
  begin, not the guarantee. A surviving `tradingpost.db` joins `accounts/` as a boot refusal.
- `character_name` gains a real `UNIQUE` constraint; scan-based name checks and directory-walk
  helpers (`CountAccountStats`) become single queries.
- Backups become point-in-time consistent (one `.backup` of `game.db` is a true world snapshot).
  Per-account restore/delete — the per-file layout's genuine conveniences — are replaced by a
  small admin script (targeted export/import, `DELETE … WHERE account_name = ?`).
- A read-only external process (rankings page, admin dashboard) can safely query the live file
  under WAL; anything needing *writes* from a second process remains the trigger to revisit
  client/server databases (per the original storage assessment).
