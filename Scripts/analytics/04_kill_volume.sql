-- 04_kill_volume.sql -- how much play the ledger has seen, and what it can prove.
--
-- Acceptance criterion 3, the half that belongs in SQL. Confidence in an observed
-- drop rate scales with kill volume, so a "mismatch" read off a thin sample is
-- not a bug report -- it is noise with a decimal point on it.
--
-- The rule, for one row of chance p, to see a rate wrong by a factor r at z
-- sigma:
--
--     kills >= z^2 * r / (p * (r-1)^2)
--
-- For the ordinary case -- a rate wrong by 2x, at 3 sigma -- that is 18 / p:
--
--     gear at 1 in 71        ->     1,278 kills
--     a unique at 1 in 5,000 ->    90,000 kills
--     boss Legendary 1 in 1,878 ->  33,804 kills of that boss
--
-- So this is a live-server instrument. It catches gross structural faults in a
-- few hundred kills and cannot speak about the rare tail for months.
--
-- The per-row version of this arithmetic, against the authored rate of every
-- (item, monster) pair, is in Scripts/drop_audit.py. This file answers the
-- prior question: is there enough data here to ask at all.
--
-- Reads itemledger.db. Writes nothing.
-- Issue #85, plan P4.3.

.mode column
.headers on

SELECT '--- kills per monster, whole window ---' AS section;

SELECT
    npc_name,
    COUNT(DISTINCT npc_id)      AS configs,
    SUM(kills)                  AS kills,
    ROUND(SUM(rep_factor_sum), 2) AS rep_factor_sum,
    -- 1.00 in a world where nobody has voted. Above 1 means the gear and unique
    -- rows were rolled MORE generously than authored, below 1 less so; either way
    -- the gear rows must be priced against rep_factor_sum, not kills.
    ROUND(SUM(rep_factor_sum) / SUM(kills), 3) AS average_rep_factor,
    MIN(day)                    AS first_day,
    MAX(day)                    AS last_day
FROM npc_kills
GROUP BY npc_name
ORDER BY kills DESC;

SELECT '--- what each monsters kill count is enough to prove ---' AS section;

-- The rarest rate a 2x error can be detected in, at 3 sigma, given the kills
-- recorded so far: p_min = 18 / kills, expressed as "1 in N". A row rarer than
-- this cannot yet be judged, and the audit tool marks such rows THIN rather than
-- passing or failing them.
SELECT
    npc_name,
    SUM(kills) AS kills,
    CAST(SUM(kills) / 18.0 AS INTEGER) AS can_judge_down_to_1_in,
    CASE
        WHEN SUM(kills) < 100   THEN 'nothing yet - noise'
        WHEN SUM(kills) < 1278  THEN 'common rows only'
        WHEN SUM(kills) < 20000 THEN 'gear rates readable'
        ELSE 'gear and the near tail readable'
    END AS verdict
FROM npc_kills
GROUP BY npc_name
ORDER BY kills DESC;

SELECT '--- kills per day ---' AS section;

SELECT
    day,
    SUM(kills)                  AS kills,
    COUNT(DISTINCT npc_id)      AS monsters_killed,
    ROUND(SUM(rep_factor_sum) / SUM(kills), 3) AS average_rep_factor
FROM npc_kills
GROUP BY day
ORDER BY day DESC;

SELECT '--- monsters killed that produced no Instanced drop at all ---' AS section;

-- Not a fault on its own: most monsters drop gold and potions far more often than
-- anything with a Serial, and a vermin roster may hold nothing Instanced at all.
-- It is here because a monster with thousands of kills and zero drops IS worth a
-- second look, and this is the query that finds it in one line.
SELECT
    k.npc_name,
    SUM(k.kills) AS kills,
    COALESCE(d.drops, 0) AS instanced_drops
FROM npc_kills k
LEFT JOIN (
    SELECT origin_detail AS monster, COUNT(*) AS drops
    FROM item_instances
    WHERE origin_type = 1 AND origin_detail IS NOT NULL
    GROUP BY origin_detail
) d ON d.monster = k.npc_name
GROUP BY k.npc_name, d.drops
HAVING COALESCE(d.drops, 0) = 0
ORDER BY kills DESC;
