# Ledger analytics starter pack

The SQL half of issue #85 (plan P4.3). Every query here reads `Binaries/Server/itemledger.db`
and writes nothing.

| file | question it answers |
|---|---|
| `00_overview.sql` | What is in this ledger — date range, run boundaries, row counts, gaps |
| `01_drop_rates.sql` | Observed drop rates by item, source monster, loot grade and day |
| `02_entering_population.sql` | Entering population: first-pickup events by item and by type |
| `03_attrition.sql` | Despawn-without-pickup attrition — drops nobody thought worth taking |
| `04_kill_volume.sql` | Kills per monster, and whether the sample is large enough to read |

`npc_kills` — the denominator every rate divides by — arrived with **ledger schema v2**. A ledger
recorded before that has drops and no kills; the queries say `no kill data` rather than divide by
zero. There is no migration by design (the plan's D6): delete the file and the server makes a fresh
one.

The observed-vs-authored diff is **not** here. It needs the authored side, which only the server
can state, so it lives in `Scripts/drop_audit.py`:

```bash
# authored side straight from the server, observed side from the live ledger
python Scripts/drop_audit.py --server Binaries/Server/Server.exe

# or from captures taken earlier
Server.exe --dropodds rows > odds.txt
python Scripts/drop_audit.py --odds odds.txt --ledger Binaries/Server/itemledger.db

python Scripts/drop_audit.py --server ... --monster Wyvern   # one monster
python Scripts/drop_audit.py --server ... --csv out.csv      # every row, machine-readable
python Scripts/drop_audit.py --selftest                      # prove the tool's own arithmetic
```

It reports per (item, monster): the authored `1 in N`, the observed `1 in N`, how many sigma apart
they are, and one of five verdicts — `DIVERGE`, `AGREE`, `THIN` (not enough kills yet: neither a
pass nor a failure), `SKIP` (stackable), `NO KILLS`. It never re-derives a rate: the predicted side
is read from the server's own `dropodds rows` output.

**Two halves, and they answer different questions.**

- The **rate** half catches a row that drops too often or too rarely.
- The **tier mix** half catches a monster rolling the wrong loot grade. This is a separate question
  because, while the per-grade generosity multipliers sit at 1.00 — which is where they are, and
  where ADR 0002 intends them to stay — a wrong loot grade moves **no drop rate at all**. It moves
  which tier the item is born with. Bug #63 was every monster in the game rolling the grade-2 curve
  for weeks; the rate half would never have seen it, and the tier half makes it unmissable (a boss
  whose Legendary share reads 0).

## How to run

Plain SQLite (no install beyond the `sqlite3` CLI):

```bash
cd Binaries/Server
sqlite3 -readonly itemledger.db < ../../Scripts/analytics/01_drop_rates.sql
```

On Windows, from the repository root:

```powershell
sqlite3.exe -readonly Binaries\Server\itemledger.db ".read Scripts\analytics\01_drop_rates.sql"
```

DuckDB is optional and reads the SQLite file directly — no service, no import, no copy. Use it
when a query gets heavy (months of events, window functions over every Serial):

```bash
duckdb -c "INSTALL sqlite; LOAD sqlite; ATTACH 'Binaries/Server/itemledger.db' AS L (TYPE sqlite, READ_ONLY);
           SET search_path='L';" -f Scripts/analytics/01_drop_rates.sql
```

Every query in this folder is written in portable SQL that both engines accept.

## Reading a live server

`itemledger.db` is WAL from birth, so an external reader never blocks the server and the server
never blocks it. Two things to know:

- **The tail is in RAM.** Events are buffered and flushed on a cadence (`ledger_flush_ms` /
  `ledger_flush_events`). Run `saveall` on the server console first if you need the last few
  seconds. `Scripts/drop_audit.py` does this for you when it drives a live server.
- **A crash loses a flush window.** `00_overview.sql` lists the run boundaries and says which ones
  followed a crash. Those are where holes can be.

## The event numbers

`item_events.event_type` is one numbering, not two (see `Sources/Server/ItemLedgerStore.h`):

| value | meaning | value | meaning |
|---|---|---|---|
| 1 | Give | 11 | Exchange |
| 2 | Drop | 32 | Use |
| 3 | get (pick up) | 33–39 | Trading Post escrow |
| 4 | Deplete | 40 | GmMint |
| 5 | NewGenDrop (monster drop) | 100 | created |
| 7 | Buy | 101 | despawned |
| 8 | Sell | 102 | destroyed |
| 9 / 10 | Retrieve / Deposit | 103 | boundary (a server run started) |

`item_flows.flow_type` uses the **same** numbers for the Counted (stackable) tier.

## What these queries cannot tell you

- **Stackables have no per-monster rate.** Gold, arrows and 30 body parts are Counted: they get an
  `item_flows` row keyed by day and item, with a *quantity*, and no source monster. The per-monster
  work here covers the 2247 of 2328 (item, monster) pairs that are Instanced — every gear row among
  them. Extending to the Counted tier would need both a monster column *and* an occurrence count,
  because `item_flows.qty` records how much moved, never how often.
- **The rare tail needs volume.** See `04_kill_volume.sql`: a 1-in-1,878 boss Legendary needs of
  the order of 34,000 boss kills before an observed rate means anything. These are live-server
  instruments, not tuning-time ones. The drop tuning tickets (#67, #90, #89) do not wait on this —
  they have exact targets from the original source and `rollsmoke`/`dropodds` to verify against;
  this pack confirms those held once real play volume exists.
- **A few rows will flag in a healthy world.** A 3-sigma gate misfires about once in 370 rows, so a
  full run over ~1,100 judged rows flags roughly three no matter how correct the world is. Read the
  sigma column: a real fault is far out, not just over the line. `drop_audit.py` prints the expected
  count beside its summary.
