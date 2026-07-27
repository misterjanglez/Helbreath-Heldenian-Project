# Trading Post items are physically escrowed in a central tradingpost.db

When a character lists items on the Trading Post or places an Offer, the server removes the items from the character's storage and inserts their full column set into a server-owned `tradingpost.db`. We chose this over flagging items in place for two codebase reasons: `CItem` instances have no unique identity (they are keyed only by `character_name + slot`, so nothing can durably reference an in-place item), and character persistence is a destructive snapshot (`SaveCharacterSnapshot` deletes and re-inserts every item row from memory on autosave/logout, silently clobbering any out-of-band row edits for online characters). Physical escrow also makes the gameplay rule "a listed item cannot be used" structural rather than a check scattered across every item-use path.

## Considered options

- **Flag-in-place** (an `escrowed` column on `character_items` / `character_bank_items`) — rejected: no instance IDs to reference from a listing, flags don't survive the snapshot rewrite for online characters, and every item-use path would need an escrow check.
- **Physical escrow in a central store** (chosen) — the Trading Post is the owner of record; character DBs never contain escrowed items.

## Consequences

- Every release of an escrowed item (trade proceeds, refunds, rescinds, delists) flows through one delivery path into the Warehouse/bank: online recipients get an in-memory bank add plus a forced character save; offline recipients get a direct insert into their account DB (safe because account DB connections are opened per-operation and snapshots only run for online characters).
- Cross-store transitions cannot be a single SQLite transaction, so operations are ordered to fail as **loss, never duplication** (remove + save before escrow-insert; escrow-delete before delivery), and every transition is `item_log`ged for manual GM recovery.
- Character deletion must explicitly void the character's Trading Post entries — the account DB's `ON DELETE CASCADE` cannot reach `tradingpost.db`.
- Moving to a different escrow model later means migrating live listing data, not just changing code.
