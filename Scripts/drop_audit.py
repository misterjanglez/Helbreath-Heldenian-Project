#!/usr/bin/env python3
"""drop_audit.py - observed drop rates against authored ones, per (item, monster).

The required deliverable of issue #85 (plan P4.3). It answers one question:

    does the roller agree with its own data?

A material divergence means either the roll disagrees with the numbers it was
given, or a number is set wrong. That is the regression net #63 lacked - every
monster in the game rolled the grade-2 curve for weeks, and it took an owner
playtest of some 500 kills to notice.

THE PREDICTED SIDE IS NOT COMPUTED HERE.
----------------------------------------
It is read from the server's own `dropodds rows` output. The drop arithmetic -
the generosity stack, proportional saturation, the guaranteed head, the tail
divisor - lives in one function that both the roll and the report call
(hb::server::resolve_drop_chances). A second implementation of it in Python that
disagreed with the roller would be worse than no report at all; that was #66's
founding constraint and Scripts/dropodds_offline.py drifting out of date (#101)
is what it looks like when it is ignored.

THE OBSERVED SIDE
-----------------
itemledger.db. `item_instances` rows with origin_type 1 (npc_drop) carry the
monster's name and the tier the item was born with; `npc_kills` carries the
denominator - kills per monster per day, plus the summed reputation factor of
the killers, because that layer scales gear and unique rows per player.

SCOPE
-----
Instanced (non-stackable) rows only. A stackable item has no birth row and so no
source monster: it is aggregated in `item_flows` by day and item, as a QUANTITY
rather than an occurrence count. That covers 2247 of 2328 (item, monster) pairs
in today's data - every gear row among them - and the excluded 81 are Gold plus
30 stackable body parts. Counted rows are reported as SKIP, never as a failure.

Usage
-----
    # drive a built server for the authored side, read the live ledger
    python Scripts/drop_audit.py --server Binaries/Server/Server.exe

    # or against captures taken earlier
    Server.exe --dropodds rows > odds.txt
    python Scripts/drop_audit.py --odds odds.txt --ledger Binaries/Server/itemledger.db

    python Scripts/drop_audit.py --server ... --monster Wyvern   # one monster
    python Scripts/drop_audit.py --server ... --csv out.csv      # machine-readable

Exit status is 0 when nothing diverged, 1 when something did. Nothing is ever
written to either database.
"""

import argparse
import csv
import math
import os
import sqlite3
import subprocess
import sys
from collections import defaultdict

# --- thresholds ------------------------------------------------------------
#
# Observed count X is Binomial(K, p), which at these rates is Poisson(K*p). A
# divergence is only reportable when the sample can carry it, so both numbers
# below are stated in the output beside every verdict rather than applied
# silently.

# How many sigma a deviation must reach before it is called a divergence. 3.0 is
# about 1 in 370 per row by chance; across ~2200 rows that is a handful of false
# positives per full run, which is why the report ranks by sigma and does not
# treat a single flagged row as proof.
SIGMA_THRESHOLD = 3.0

# The error size the sample must be able to see before a row is judged at all.
# A factor of 2 is the smallest error worth a ticket: below that, tuning noise
# and rounding in the ppb tables are the same size as the finding.
DETECTABLE_RATIO = 2.0


def kills_needed(p, ratio=DETECTABLE_RATIO, z=SIGMA_THRESHOLD):
    """Kills required before an error of `ratio` on a row of chance `p` is visible.

    From |K*r*p - K*p| >= z*sqrt(K*r*p):  K >= z^2 * r / (p * (r-1)^2).
    """
    if p <= 0.0:
        return float("inf")
    return (z * z * ratio) / (p * (ratio - 1.0) ** 2)


def sigma_of(observed, expected):
    """How far an observed count sits from its expectation, in Poisson sigmas."""
    if expected <= 0.0:
        return float("inf") if observed > 0 else 0.0
    return abs(observed - expected) / math.sqrt(expected)


# --- the predicted side ----------------------------------------------------

class Authored:
    """`dropodds rows` output, parsed. Nothing here is derived - only read."""

    def __init__(self):
        self.mode = None
        self.rating = 0
        self.multipliers = ()
        self.npcs = {}          # npc_id -> {"name":, "grade":}
        self.slots = {}         # (npc_id, stage) -> {"table":, "saturated":, ...}
        self.rows = []          # one dict per authored (item, monster, stage) row
        self.splits = {}        # loot grade -> [share of tier 1..4]

    @classmethod
    def parse(cls, text):
        out = cls()
        for line in text.splitlines():
            if not line.startswith("DROPODDS "):
                continue
            f = line.split(" ")
            kind = f[1]

            if kind == "MODE":
                # DROPODDS MODE <mode> <global> <stage1> <stage2> <rating> <rep>
                out.mode = f[2]
                out.multipliers = tuple(float(x) for x in f[3:6])
                out.rating = int(f[6])

            elif kind == "SPLIT":
                # DROPODDS SPLIT <grade> <tier1> <tier2> <tier3> <tier4>
                out.splits[int(f[2])] = [float(x) for x in f[3:]]

            elif kind == "NPC":
                # DROPODDS NPC <npc_id> <grade> <name...>
                out.npcs[int(f[2])] = {"name": " ".join(f[4:]), "grade": int(f[3])}

            elif kind == "SLOT":
                # DROPODDS SLOT <npc> <stage> <table> <min> <max> <guaranteed>
                #               <saturated> <rows>
                out.slots[(int(f[2]), int(f[3]))] = {
                    "table": int(f[4]),
                    "rolls_min": int(f[5]),
                    "rolls_max": int(f[6]),
                    "guaranteed": int(f[7]),
                    "saturated": f[8] == "1",
                    "rows": int(f[9]),
                }

            elif kind == "ROW":
                # DROPODDS ROW <npc> <stage> <item> <instanced> <gear> <rep>
                #              <per_roll> <per_kill> <name...>
                out.rows.append({
                    "npc_id": int(f[2]),
                    "stage": int(f[3]),
                    "item_id": int(f[4]),
                    "instanced": f[5] == "1",
                    "gear": f[6] == "1",
                    "rep_scaled": f[7] == "1",
                    "per_roll": float(f[8]),
                    "per_kill": float(f[9]),
                    "item_name": " ".join(f[10:]),
                })
        return out

    def check_usable(self):
        problems = []
        if self.mode is None:
            problems.append("no DROPODDS MODE line - is this a `dropodds rows` capture?")
        if not self.rows:
            problems.append("no DROPODDS ROW lines - the capture is empty")
        if self.rating != 0:
            # The reputation layer would then be applied twice: once by the
            # server into per_kill, and again here from the ledger's summed
            # factors. Refused rather than silently doubled.
            problems.append(
                "the capture was taken at rating %d; this tool needs rating 0, "
                "because it applies the reputation layer itself" % self.rating)
        return problems


def run_dropodds(server_path, monster=None):
    """Drive the built server headlessly for the authored side."""
    server_path = os.path.abspath(server_path)
    workdir = os.path.dirname(server_path)
    command = "rows" if monster is None else "rows %s" % monster
    result = subprocess.run(
        [server_path, "--dropodds"] + command.split(" "),
        cwd=workdir, capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        raise SystemExit("dropodds failed (exit %d):\n%s"
                         % (result.returncode, result.stderr or result.stdout))
    return result.stdout


# --- the observed side -----------------------------------------------------

def read_ledger(path):
    """Observed drops and kills. Read-only; the server may be running."""
    if not os.path.exists(path):
        raise SystemExit("no ledger at %s" % path)

    uri = "file:%s?mode=ro" % path.replace("?", "%3f").replace("#", "%23")
    db = sqlite3.connect(uri, uri=True)

    tables = {r[0] for r in db.execute(
        "SELECT name FROM sqlite_master WHERE type='table'")}
    if "npc_kills" not in tables:
        raise SystemExit(
            "%s has no npc_kills table - it predates ledger schema v2, which is\n"
            "where the kill counter (the denominator) was added. There is no\n"
            "migration by design: delete it and let the server make a fresh one."
            % path)

    # (monster name, item_id) -> observed drops, and the tier mix per monster.
    drops = defaultdict(int)
    tiers = defaultdict(lambda: [0, 0, 0, 0, 0])
    for monster, item_id, tier, count in db.execute(
            "SELECT origin_detail, item_id, tier, COUNT(*)"
            " FROM item_instances"
            " WHERE origin_type = 1 AND origin_detail IS NOT NULL"
            " GROUP BY origin_detail, item_id, tier"):
        drops[(monster, item_id)] += count
        if 0 <= tier <= 4:
            tiers[monster][tier] += count

    # monster name -> kills and summed reputation factor. Pooled by NAME because
    # that is the only key a birth row carries; where one name covers several
    # configs both sides pool the same way.
    kills = {}
    for name, k, rep, configs in db.execute(
            "SELECT npc_name, SUM(kills), SUM(rep_factor_sum), COUNT(DISTINCT npc_id)"
            " FROM npc_kills GROUP BY npc_name"):
        kills[name] = {"kills": k, "rep_factor_sum": rep, "configs": configs}

    window = db.execute(
        "SELECT MIN(day), MAX(day), SUM(kills) FROM npc_kills").fetchone()

    # Crash boundaries: a run that ended badly lost its buffered tail, so both
    # sides of this comparison can be short by a flush window.
    crashes = db.execute(
        "SELECT COUNT(*) FROM item_events"
        " WHERE event_type = 103 AND detail LIKE '%\"crash\"%'").fetchone()[0]

    db.close()
    return {"drops": drops, "tiers": tiers, "kills": kills,
            "window": window, "crashes": crashes}


# --- the join --------------------------------------------------------------

VERDICT_ORDER = {"DIVERGE": 0, "AGREE": 1, "THIN": 2, "SKIP": 3, "NO KILLS": 4}

TIER_NAMES = ["Common", "Rare", "Epic", "Legendary"]


def audit_tier_mix(authored, ledger):
    """Observed tier mix per loot grade, against the authored tier curve.

    This is the #63 detector, and it is a separate question from the rate audit
    for a reason the rate audit cannot state: while the per-grade generosity
    multipliers sit at 1.00 -- which is where they are, and where the design
    intends them to stay (ADR 0002 puts grade differentiation in quality and
    roster, not frequency) -- a monster rolling the WRONG loot grade drops
    everything at exactly the authored rate. Nothing in the rate columns moves.
    What moves is which tier the item is born with, and the ledger records that
    on the birth row.

    #63 was every monster in the game rolling the grade-2 curve for weeks. In
    this view it is unmissable: a boss whose Legendary share reads 0.
    """
    # Which grade each monster belongs to, from the capture. Pooled by name for
    # the same reason the rates are: a birth row carries the name and nothing
    # else, so two configs under one name are one population here.
    grade_of = {}
    for npc in authored.npcs.values():
        grade_of.setdefault(npc["name"], npc["grade"])

    observed = defaultdict(lambda: [0, 0, 0, 0, 0])
    for monster, counts in ledger["tiers"].items():
        grade = grade_of.get(monster)
        if grade is None:
            continue
        for tier in range(5):
            observed[grade][tier] += counts[tier]

    findings = []
    for grade in sorted(authored.splits):
        counts = observed.get(grade)
        shares = authored.splits[grade]
        tiered = sum(counts[1:]) if counts else 0

        row = {"grade": grade, "tiered_drops": tiered,
               "untiered": counts[0] if counts else 0,
               "tiers": [], "verdict": "AGREE", "note": ""}

        if tiered == 0:
            # Not a fault by itself: a legacy world tier-rolls nothing at all,
            # and a grade nobody has farmed has no sample.
            row["verdict"] = "NO DATA"
            row["note"] = "no tiered drops recorded at this grade"
            findings.append(row)
            continue

        worst = 0.0
        for tier in range(4):
            share = shares[tier] if tier < len(shares) else 0.0
            expected = tiered * share
            seen = counts[tier + 1]
            sigma = sigma_of(seen, expected)
            # A tier the curve cannot reach at all (a zero weight is the
            # staircase hard gate, spec S8) is a different statement from a rare
            # one: ANY sighting is a fault, and no sample size is needed to say so.
            if share == 0.0 and seen > 0:
                sigma = float("inf")
            row["tiers"].append({"tier": TIER_NAMES[tier], "observed": seen,
                                 "expected": expected, "share": share,
                                 "sigma": sigma})
            if sigma != float("inf"):
                worst = max(worst, sigma)
            else:
                worst = float("inf")

        if worst == float("inf"):
            row["verdict"] = "DIVERGE"
            row["note"] = "a tier this grade cannot reach was rolled"
        elif worst >= SIGMA_THRESHOLD:
            row["verdict"] = "DIVERGE"
            row["note"] = "%.1f sigma on the worst tier" % worst
        elif tiered < 100:
            row["verdict"] = "THIN"
            row["note"] = "only %d tiered drops - too few to read a curve" % tiered
        findings.append(row)

    return findings


def report_tier_mix(findings):
    print()
    print("Tier mix by loot grade - observed against the authored curve")
    print("-" * 78)
    print("  This is where a wrong loot grade shows. It does not move a drop RATE")
    print("  while the per-grade multipliers are 1.00; it moves which tier the item")
    print("  is born with (#63).")
    print()
    header = "  %-6s %8s %10s %10s %10s %10s  %s" % (
        "grade", "tiered", TIER_NAMES[0], TIER_NAMES[1], TIER_NAMES[2],
        TIER_NAMES[3], "verdict")
    print(header)
    print("  " + "-" * (len(header) - 2))
    for f in findings:
        cells = []
        for tier in f["tiers"] or [None] * 4:
            if tier is None:
                cells.append("-")
            else:
                cells.append("%d/%.0f" % (tier["observed"], tier["expected"]))
        print("  %-6d %8d %10s %10s %10s %10s  %s%s"
              % (f["grade"], f["tiered_drops"], cells[0], cells[1], cells[2],
                 cells[3], f["verdict"],
                 (" - " + f["note"]) if f["note"] else ""))
    print("  (cells are observed/expected)")
    return sum(1 for f in findings if f["verdict"] == "DIVERGE")


def audit(authored, ledger):
    findings = []

    for row in authored.rows:
        npc = authored.npcs.get(row["npc_id"])
        if npc is None:
            continue
        monster = npc["name"]
        slot = authored.slots.get((row["npc_id"], row["stage"]), {})

        observed = ledger["drops"].get((monster, row["item_id"]), 0)
        killrow = ledger["kills"].get(monster)

        finding = {
            "monster": monster,
            "npc_id": row["npc_id"],
            "grade": npc["grade"],
            "stage": row["stage"],
            "item_id": row["item_id"],
            "item_name": row["item_name"],
            "authored_per_kill": row["per_kill"],
            "authored_1_in": (1.0 / row["per_kill"]) if row["per_kill"] > 0 else None,
            "observed": observed,
            "kills": killrow["kills"] if killrow else 0,
            "expected": None,
            "observed_1_in": None,
            "sigma": None,
            "ratio": None,
            "kills_needed": kills_needed(row["per_kill"]),
            "saturated": slot.get("saturated", False),
            "verdict": None,
            "note": "",
        }

        # Counted rows never get a birth row naming the monster, so an observed
        # count of 0 says nothing about them. Reported, never judged.
        if not row["instanced"]:
            finding["verdict"] = "SKIP"
            finding["note"] = "stackable - no per-monster record exists"
            findings.append(finding)
            continue

        if killrow is None or killrow["kills"] == 0:
            finding["verdict"] = "NO KILLS"
            finding["note"] = "this monster has not been killed in this ledger"
            findings.append(finding)
            continue

        kills = killrow["kills"]

        # The reputation layer multiplies gear and unique rows per killer (#88),
        # and it is linear, so the summed factor IS the right denominator for
        # those rows. Everything else divides by the plain count.
        effective = killrow["rep_factor_sum"] if row["rep_scaled"] else kills
        expected = effective * row["per_kill"]

        finding["expected"] = expected
        finding["observed_1_in"] = (kills / observed) if observed > 0 else None
        finding["sigma"] = sigma_of(observed, expected)
        finding["ratio"] = (observed / expected) if expected > 0 else None

        if kills < finding["kills_needed"]:
            finding["verdict"] = "THIN"
            finding["note"] = ("needs %s kills to judge a %gx error; have %s"
                               % (fmt_int(finding["kills_needed"]),
                                  DETECTABLE_RATIO, fmt_int(kills)))
        elif finding["sigma"] >= SIGMA_THRESHOLD:
            finding["verdict"] = "DIVERGE"
            finding["note"] = "%.1f sigma from the authored rate" % finding["sigma"]
        else:
            finding["verdict"] = "AGREE"

        # A saturated table was scaled proportionally back to 1.0, which breaks
        # the linearity the reputation term is priced with. Named on the row so a
        # divergence there is read as "the model does not apply here" rather than
        # as a rate fault.
        if finding["saturated"]:
            finding["note"] = (finding["note"] + "; " if finding["note"] else "") + \
                "table is SATURATED - rates were clamped"

        findings.append(finding)

    # A drop the authored side never priced: the monster produced an item its
    # tables do not list. That is the strongest finding the tool can make, so it
    # is derived rather than left to a reader noticing an absence.
    #
    # Only for monsters this capture actually priced. With `--monster` the
    # capture holds one of them, and every other monster's drops are outside the
    # question being asked rather than unpriced.
    priced = {(authored.npcs[r["npc_id"]]["name"], r["item_id"])
              for r in authored.rows if r["npc_id"] in authored.npcs}
    covered = {npc["name"] for npc in authored.npcs.values()}
    for (monster, item_id), observed in sorted(ledger["drops"].items()):
        if monster not in covered or (monster, item_id) in priced:
            continue
        findings.append({
            "monster": monster, "npc_id": -1, "grade": 0, "stage": 0,
            "item_id": item_id, "item_name": "?",
            "authored_per_kill": 0.0, "authored_1_in": None,
            "observed": observed,
            "kills": ledger["kills"].get(monster, {}).get("kills", 0),
            "expected": 0.0, "observed_1_in": None,
            "sigma": float("inf"), "ratio": None, "kills_needed": 0.0,
            "saturated": False, "verdict": "DIVERGE",
            "note": "UNPRICED - this monster's tables do not list this item",
        })

    findings.sort(key=lambda f: (VERDICT_ORDER[f["verdict"]],
                                 -(f["sigma"] or 0.0), -f["observed"]))
    return findings


# --- output ----------------------------------------------------------------

def fmt_int(value):
    if value is None or value == float("inf"):
        return "-"
    return "{:,}".format(int(round(value)))


def fmt_one_in(value):
    if value is None:
        return "-"
    if value >= 1_000_000:
        return "%.2g" % value
    return fmt_int(value)


def report(findings, authored, ledger, show_all):
    counts = defaultdict(int)
    for f in findings:
        counts[f["verdict"]] += 1

    print()
    print("Drop-rate audit - observed against authored, per (item, monster)")
    print("=" * 78)
    print("  item_system      : %s" % authored.mode)
    print("  generosity       : global x%.2f, stage 1 x%.2f, stage 2 x%.2f"
          % authored.multipliers)
    print("  priced rows      : %d across %d monsters"
          % (len(authored.rows), len(authored.npcs)))
    first_day, last_day, total_kills = ledger["window"]
    print("  ledger kills     : %s, over days %s..%s"
          % (fmt_int(total_kills or 0), first_day or "-", last_day or "-"))
    if ledger["crashes"]:
        print("  WARNING          : %d crash boundary(ies) in this ledger - a flush"
              % ledger["crashes"])
        print("                     window was lost at each, so both sides may be short")
    print("  divergence gate  : %.1f sigma, for an error of %gx or more"
          % (SIGMA_THRESHOLD, DETECTABLE_RATIO))
    print()

    header = ("%-22s %5s %-26s %11s %11s %7s %7s %6s  %s"
              % ("monster", "item", "name", "authored", "observed",
                 "drops", "kills", "sigma", "verdict"))
    print(header)
    print("-" * len(header))

    shown = 0
    for f in findings:
        if not show_all and f["verdict"] in ("AGREE", "SKIP"):
            continue
        shown += 1
        print("%-22.22s %5d %-26.26s %11s %11s %7d %7s %6s  %s%s"
              % (f["monster"], f["item_id"], f["item_name"],
                 "1 in " + fmt_one_in(f["authored_1_in"]),
                 ("1 in " + fmt_one_in(f["observed_1_in"])) if f["observed_1_in"] else "-",
                 f["observed"], fmt_int(f["kills"]),
                 ("%.1f" % f["sigma"]) if f["sigma"] not in (None, float("inf")) else "-",
                 f["verdict"],
                 (" - " + f["note"]) if f["note"] else ""))
    if shown == 0:
        print("  (nothing to report at this filter)")

    print()
    print("Summary: " + ", ".join("%s %d" % (v, counts[v])
                                  for v in sorted(counts, key=VERDICT_ORDER.get)))

    # How many DIVERGE rows a HEALTHY world is expected to produce anyway. A 3
    # sigma gate misfires about once in 370 rows by chance, so a full run over
    # two thousand rows flags a handful no matter how correct the world is.
    # Printed rather than left for a reader to work out: without it, two
    # marginal rows read as two faults, and the tool gets a reputation for crying
    # wolf on its first run.
    judged = counts["DIVERGE"] + counts["AGREE"]
    if judged:
        expected_noise = judged * 0.0027
        print("  Of %d judged rows, about %.1f are expected to flag by chance alone."
              % (judged, expected_noise))
        print("  Read the sigma column: a real fault is far out, not just over the line.")
    print()
    print("  DIVERGE  the observed rate is %.0f sigma or more from the authored one"
          % SIGMA_THRESHOLD)
    print("  AGREE    enough kills to have seen a %gx error, and there is none"
          % DETECTABLE_RATIO)
    print("  THIN     not enough kills yet - this is not a pass and not a failure")
    print("  SKIP     stackable: no per-monster record exists (see the README)")
    print("  NO KILLS this monster has not died in this ledger")
    if not show_all:
        print()
        print("  AGREE and SKIP rows are hidden; pass --all to see them.")
    return counts


def write_csv(path, findings):
    columns = ["monster", "npc_id", "grade", "stage", "item_id", "item_name",
               "authored_per_kill", "authored_1_in", "observed", "kills",
               "expected", "observed_1_in", "sigma", "ratio", "kills_needed",
               "saturated", "verdict", "note"]
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for f in findings:
            writer.writerow({c: f.get(c) for c in columns})
    print("  wrote %s (%d rows)" % (path, len(findings)))


# --- the prover -------------------------------------------------------------
#
# The C++ half of this ticket is proved by `analyticscheck`: that the kill counter
# aggregates and accumulates, and that a drop joins to a kill by name. What that
# cannot reach is the arithmetic here -- which denominator a row gets, how many
# sigma an observed count sits from its expectation, and which verdict follows.
# A wrong verdict is the failure mode that matters: a tool that calls a real
# divergence THIN is worse than no tool, because it is read as evidence of health.
#
# So this drives the real audit() over a synthetic ledger and a synthetic capture,
# both built here, and states what each row must come back as. Machine lines, one
# per check, for the same Windows-vs-Linux byte comparison every other prover in
# this project is gated by.

def selftest():
    import tempfile

    passed = 0
    total = 0

    def record(name, ok, detail=""):
        nonlocal passed, total
        total += 1
        if ok:
            passed += 1
        print("DROPAUDIT %s %s%s" % (name, "PASS" if ok else "FAIL",
                                     (" " + detail) if detail else ""))

    # --- the synthetic authored side ---------------------------------------
    # Two monsters. Alpha is killed a great deal, Beta not at all. Rates are
    # chosen so the kill counts below land either side of the detection
    # threshold rather than near it.
    capture = "\n".join([
        "DROPODDS MODE tiered 1.00 1.00 1.00 0 1.00",
        # Grade 3 can reach every tier; grade 1 is Common-only, which is the
        # staircase hard gate a vermin roster sits behind.
        "DROPODDS SPLIT 3 0.50000000 0.30000000 0.15000000 0.05000000",
        "DROPODDS SPLIT 1 1.00000000 0.00000000 0.00000000 0.00000000",
        "DROPODDS NPC 1 3 Alpha",
        "DROPODDS SLOT 1 1 100 1 1 0 0 4",
        # item 10: a plain consumable at 1 in 10, priced against the kill count
        "DROPODDS ROW 1 1 10 1 0 0 0.10000000 0.10000000 Plain Potion",
        # item 11: gear at 1 in 100, priced against the summed reputation factor
        "DROPODDS ROW 1 1 11 1 1 1 0.01000000 0.01000000 Probe Sword",
        # item 12: a stackable - no per-monster record can exist
        "DROPODDS ROW 1 1 12 0 0 0 0.20000000 0.20000000 Probe Gold",
        # item 13: rarer than this sample can judge
        "DROPODDS ROW 1 1 13 1 1 1 0.00001000 0.00001000 Probe Relic",
        "DROPODDS NPC 2 1 Beta",
        "DROPODDS SLOT 2 1 101 1 1 0 0 1",
        "DROPODDS ROW 2 1 10 1 0 0 0.10000000 0.10000000 Plain Potion",
        "DROPODDS ROWS 2",
    ])
    authored = Authored.parse(capture)
    record("capture_parses",
           authored.mode == "tiered" and len(authored.rows) == 5
           and len(authored.npcs) == 2 and authored.rating == 0)

    # A capture taken at a non-zero rating must be refused, not silently used:
    # the server would have folded the reputation layer into per_kill and this
    # tool folds it in again, squaring it on every gear row.
    scaled = Authored.parse(capture.replace(
        "DROPODDS MODE tiered 1.00 1.00 1.00 0 1.00",
        "DROPODDS MODE tiered 1.00 1.00 1.00 200 2.00"))
    record("non_neutral_capture_refused", len(scaled.check_usable()) == 1)

    # --- the synthetic ledger ----------------------------------------------
    # 10,000 Alpha kills at an average reputation factor of 1.5, so the gear
    # denominator (15,000) differs from the kill count and a row priced against
    # the wrong one cannot pass by coincidence.
    kills = 10000
    rep_sum = 15000.0
    drops = {
        10: 1000,   # exactly 10,000 x 0.10  -> AGREE
        11: 150,    # exactly 15,000 x 0.01  -> AGREE only if rep-scaled
        13: 0,      # too rare to judge      -> THIN
        99: 5,      # not in the table       -> DIVERGE, UNPRICED
    }

    handle, path = tempfile.mkstemp(suffix=".db", prefix="dropaudit_selftest_")
    os.close(handle)
    os.unlink(path)
    db = sqlite3.connect(path)
    db.executescript(
        "CREATE TABLE item_instances (serial INTEGER PRIMARY KEY, item_id INTEGER,"
        " created_at INTEGER, origin_type INTEGER, origin_detail TEXT, map TEXT,"
        " x INTEGER, y INTEGER, tier INTEGER);"
        "CREATE TABLE item_events (event_id INTEGER PRIMARY KEY, serial INTEGER,"
        " at INTEGER, event_type INTEGER, detail TEXT);"
        "CREATE TABLE npc_kills (day INTEGER, npc_id INTEGER, npc_name TEXT,"
        " kills INTEGER, rep_factor_sum REAL, PRIMARY KEY (day, npc_id));")
    serial = 1
    for item_id, count in drops.items():
        for _ in range(count):
            db.execute("INSERT INTO item_instances VALUES (?,?,?,?,?,?,?,?,?)",
                       (serial, item_id, 0, 1, "Alpha", "probemap", 0, 0, 0))
            serial += 1
    # One item minted with no monster - the shape every contract prover leaves
    # behind. It must not be counted as loot.
    db.execute("INSERT INTO item_instances VALUES (?,?,?,?,?,?,?,?,?)",
               (serial, 10, 0, 1, None, "probemap", 0, 0, 0))
    db.execute("INSERT INTO npc_kills VALUES (?,?,?,?,?)",
               (20260731, 1, "Alpha", kills, rep_sum))
    db.commit()
    db.close()

    ledger = read_ledger(path)
    record("ledger_excludes_unattributed_mints",
           ledger["drops"].get(("Alpha", 10)) == 1000)

    findings = {(f["monster"], f["item_id"]): f for f in audit(authored, ledger)}

    def verdict(monster, item_id):
        found = findings.get((monster, item_id))
        return found["verdict"] if found else "MISSING"

    record("matching_rate_agrees", verdict("Alpha", 10) == "AGREE")

    # The load-bearing one, and it discriminates rather than merely passing.
    # 150 gear drops against a 1-in-100 row: divided by the 10,000 kills that is
    # an expectation of 100 against an observed 150, five sigma, and the tool
    # reports a fault that is not there. Divided by the summed reputation factor
    # -- 15,000, because the killers were well regarded and gear rolls faster for
    # them -- the expectation is exactly 150.
    record("gear_row_uses_reputation_sum", verdict("Alpha", 11) == "AGREE",
           "150 drops, 10000 kills, rep sum 15000")

    record("stackable_row_skipped", verdict("Alpha", 12) == "SKIP")
    record("rare_row_is_thin", verdict("Alpha", 13) == "THIN")
    record("unpriced_drop_diverges", verdict("Alpha", 99) == "DIVERGE")
    record("unkilled_monster_reported", verdict("Beta", 10) == "NO KILLS")

    # A rate that is genuinely wrong must be caught. Same ledger, an authored
    # rate half what the world produced.
    doubled = Authored.parse(capture.replace(
        "DROPODDS ROW 1 1 10 1 0 0 0.10000000 0.10000000 Plain Potion",
        "DROPODDS ROW 1 1 10 1 0 0 0.05000000 0.05000000 Plain Potion"))
    caught = {(f["monster"], f["item_id"]): f for f in audit(doubled, ledger)}
    hit = caught[("Alpha", 10)]
    record("doubled_rate_diverges",
           hit["verdict"] == "DIVERGE" and hit["sigma"] > 20.0,
           "%.0f sigma" % hit["sigma"])

    # The threshold arithmetic itself, at the figures the README and the SQL pack
    # quote. A change to either constant that was not meant must move these.
    record("kills_needed_for_gear",
           round(kills_needed(1.0 / 71.0)) == 1278, "1 in 71")
    record("kills_needed_for_boss_legendary",
           round(kills_needed(1.0 / 1878.0)) == 33804, "1 in 1878")
    record("thin_verdict_tracks_the_threshold",
           kills_needed(0.00001) > kills and kills_needed(0.10) < kills)

    os.unlink(path)

    # --- the tier mix, which is the #63 detector ---------------------------
    # Driven on three tier populations rather than by inspecting the function:
    # a grade rolling its own curve, a grade rolling somebody else's, and a
    # grade producing a tier its curve sets to zero.
    def tier_ledger(alpha_counts, beta_counts=None):
        return {"drops": {}, "kills": {}, "window": (0, 0, 0), "crashes": 0,
                "tiers": {"Alpha": alpha_counts,
                          "Beta": beta_counts or [0, 0, 0, 0, 0]}}

    def tier_verdict(alpha_counts):
        found = audit_tier_mix(authored, tier_ledger(alpha_counts))
        return {f["grade"]: f["verdict"] for f in found}

    # 10,000 tiered drops split exactly 50/30/15/5 - the authored grade-3 curve.
    healthy = tier_verdict([0, 5000, 3000, 1500, 500])
    record("tier_mix_agrees_on_the_authored_curve", healthy.get(3) == "AGREE")

    # #63 itself: the same monsters rolling a curve that never reaches Legendary.
    # Every drop RATE is untouched - this is the only view that can see it.
    caught63 = tier_verdict([0, 9000, 1000, 0, 0])
    record("tier_mix_catches_a_wrong_curve", caught63.get(3) == "DIVERGE",
           "no Legendary, no Epic")

    # A tier the grade's curve sets to zero is a hard gate, not a rare event:
    # one sighting is a fault and no sample size is needed to say so.
    gate = audit_tier_mix(authored, tier_ledger([0, 0, 0, 0, 0], [0, 99, 0, 0, 1]))
    record("tier_mix_catches_an_unreachable_tier",
           {f["grade"]: f["verdict"] for f in gate}.get(1) == "DIVERGE")

    # A legacy world tier-rolls nothing at all, and an unfarmed grade has no
    # sample. Neither is a fault, and calling it one would make the whole view
    # unreadable on the day the pack is first run.
    record("tier_mix_says_nothing_without_data",
           tier_verdict([0, 0, 0, 0, 0]).get(3) == "NO DATA")
    record("tier_mix_is_thin_on_a_small_sample",
           tier_verdict([0, 5, 3, 1, 1]).get(3) == "THIN")

    print("DROPAUDIT RESULT %d %d" % (passed, total))
    if passed == total:
        print("drop_audit selftest: %d/%d checks passed." % (passed, total))
    else:
        print("drop_audit selftest: %d/%d checks passed - the audit is broken."
              % (passed, total))
    return 0 if passed == total else 1


def main():
    parser = argparse.ArgumentParser(
        description="Observed drop rates against authored ones, per (item, monster).")
    parser.add_argument("--server", help="path to Server.exe / HelbreathServer; "
                                         "run headlessly for the authored side")
    parser.add_argument("--odds", help="a saved `dropodds rows` capture instead")
    parser.add_argument("--ledger", help="itemledger.db (default: beside the server)")
    parser.add_argument("--monster", help="restrict to one monster id or name")
    parser.add_argument("--csv", help="also write every row to this CSV")
    parser.add_argument("--all", action="store_true",
                        help="show AGREE and SKIP rows too")
    parser.add_argument("--selftest", action="store_true",
                        help="prove this tool's own arithmetic and exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if not args.server and not args.odds:
        parser.error("give --server (to run dropodds) or --odds (a saved capture)")

    if args.odds:
        with open(args.odds, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    else:
        text = run_dropodds(args.server, args.monster)

    authored = Authored.parse(text)
    problems = authored.check_usable()
    if problems:
        for problem in problems:
            print("ERROR: %s" % problem, file=sys.stderr)
        return 2

    ledger_path = args.ledger
    if not ledger_path:
        if not args.server:
            parser.error("--odds needs --ledger too")
        ledger_path = os.path.join(os.path.dirname(os.path.abspath(args.server)),
                                   "itemledger.db")
    ledger = read_ledger(ledger_path)

    findings = audit(authored, ledger)
    counts = report(findings, authored, ledger, args.all)

    tier_findings = audit_tier_mix(authored, ledger)
    tier_diverged = report_tier_mix(tier_findings)

    if args.csv:
        write_csv(args.csv, findings)

    return 1 if (counts["DIVERGE"] or tier_diverged) else 0


if __name__ == "__main__":
    sys.exit(main())
