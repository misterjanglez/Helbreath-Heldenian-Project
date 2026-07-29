# Every identity-bearing item gets a permanent Serial, and its full life is recorded in an append-only Provenance Ledger

Loot 2.0 makes dropped gear the game's economy, which makes item history first-class data: drop-rate
truth, entering-population and despawn-attrition telemetry, ownership-dispute resolution, and dupe
forensics all require knowing what happened to *this* item, not just that *an* item of its kind was
involved. The server's existing `item_log()` taxonomy (Exchange, Give, Drop, get, Make, Deposit,
Retrieve, Upgrade…, `ItemManager.cpp`) already instruments most of the right transitions, but it
writes formatted text lines about *indistinguishable* items — telemetry, not provenance. The missing
primitive is identity. Design contract: `PLANS/ItemLedger_Plan.md`.

Decision: every **Instanced** item (Instanced iff non-stackable — gear, uniques, angels, Dark Items;
Commons included, since disputes and dupes are not tier-gated) is minted a server-global monotonic
`int64` **Serial** at creation, by a factory that is the only legal way to create items. Serials and
every subsequent custody/state transition are appended to `itemledger.db` — a dedicated SQLite
database (WAL, buffered batched writes on the save cadence). Stackables (gold, potions, ammo) get
aggregate daily flow counters instead: stack merge/split dissolves identity, so the boundary is
principled. Serials never touch the wire — the client cannot even observe the tracking, and the
Item Tiers one-Compatibility-bump law (ADR 0002) is untouched. The ledger is a **passive audit
trail**: the in-RAM world and snapshot saves remain authoritative, and the redundancy is the
feature — reconciling snapshots against the ledger's derived holder state *is* the dupe detector.

## Considered options

- **Upgrade the text logs** (same lines, better sink) — rejected: without identity there is no key
  to join on; "which +7 sword" stays unanswerable and dupe detection stays statistical inference.
- **Full event sourcing** (ledger as system of record, state derived) — rejected: a rewrite of the
  persistence model for negative value; snapshots + audit trail give the forensics without the risk.
- **Serials for Rare+ only** — rejected: Commons are tradeable and dupeable; volume at this
  population is trivial either way.
- **External database (Postgres) for the ledger** — rejected: single-writer append-only workload on
  a single box is SQLite's best case; an out-of-process hop from a blocking single-threaded game
  loop is strictly worse (see the storage assessment in the design contract's origin discussion).

## Consequences

- Creation funnels through the minting factory (all 45 `new CItem` sites); a raw `new CItem` in
  game code becomes a review defect. Coverage is law: every mutation path emits, enforced by a
  coverage-audit gate before any forensic tooling is trusted.
- Persistence carries Serials (schema v8, one epoch shared with ADR 0004's consolidation). The
  project is in Alpha and worlds wipe freely, so **no migration or backfill code ever ships**;
  pre-v8 DBs are refused by the schema gate. The hard deadline is go-live — after real players
  exist, unserialed items would be permanent world facts.
- Ledger events are kept forever (single-digit GB/year worst case); `item_log()` keeps its text
  channels for ops tailing and gains the structured sink.
- Payoff tooling lands behind the coverage gate: reconciliation (holder mismatches, one Serial in
  two inventories), per-Serial Biography with legal-transition replay, and the Loot 2.0 analytics
  pack (actual drop rates, entering-population, despawn attrition).
