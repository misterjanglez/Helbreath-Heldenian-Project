-- 01_drop_rates.sql -- observed drop rates by item, source monster and day.
--
-- The OBSERVED half of acceptance criterion 1. It states what actually dropped
-- and how often, per monster and over time. It does not state what SHOULD have
-- dropped -- that is the authored side, which only the server can resolve, and
-- `Scripts/drop_audit.py` is what joins the two.
--
-- The denominator is npc_kills (#85). Without it there is no rate at all: drop
-- rarity is authored as a chance per kill, so a count of drops divides by
-- nothing until kills are counted.
--
-- SCOPE: Instanced items only. A stackable item has no birth row and therefore no
-- source monster; it is counted in item_flows by day and item alone. See the
-- README.
--
-- Reads itemledger.db. Writes nothing.
-- Issue #85, plan P4.3.

.mode column
.headers on

SELECT '--- observed drops per (item, monster), whole window ---' AS section;

-- origin_detail is the monster name, written by the drop path at the mint
-- (EntityManager::spawn_npc_drop_item). It is NULL for every other venue and for
-- the contract provers' probe items, so the NOT NULL test is what keeps this a
-- loot report rather than a creation report.
WITH drops AS (
    SELECT origin_detail AS monster, item_id, tier, created_at
    FROM item_instances
    WHERE origin_type = 1 AND origin_detail IS NOT NULL
)
SELECT
    monster,
    item_id,
    COUNT(*) AS drops,
    -- Tier is stamped at birth by the roll that produced the item, so a drop
    -- report doubles as a check on the tier curve the monster's loot grade
    -- selects. All zeros in tiered mode means the grade never reached the roll.
    SUM(CASE WHEN tier = 1 THEN 1 ELSE 0 END) AS common,
    SUM(CASE WHEN tier = 2 THEN 1 ELSE 0 END) AS rare,
    SUM(CASE WHEN tier = 3 THEN 1 ELSE 0 END) AS epic,
    SUM(CASE WHEN tier = 4 THEN 1 ELSE 0 END) AS legendary,
    SUM(CASE WHEN tier = 0 THEN 1 ELSE 0 END) AS untiered,
    datetime(MIN(created_at), 'unixepoch', 'localtime') AS first_drop,
    datetime(MAX(created_at), 'unixepoch', 'localtime') AS last_drop
FROM drops
GROUP BY monster, item_id
ORDER BY drops DESC, monster, item_id;

SELECT '--- the same, as an observed 1 in N kills ---' AS section;

-- The join key is the monster NAME. A birth row records the monster that dropped
-- the item by name (origin_detail, which is what a Biography shows a human), and
-- npc_kills carries the name beside the config id for exactly this reason -- the
-- ledger has to be readable on its own, never against a copy of gamedata.db.
--
-- Where one name covers several npc_configs (14 Catapults, 3 Guards) both sides
-- pool identically, so the rate is a kill-weighted average over them. That is the
-- honest reading and it is stated rather than hidden: `configs` below is how many
-- were pooled.
--
-- A monster absent from npc_kills has been killed zero times in this ledger.
-- The kill counter arrived with ledger schema v2, so a window recorded before it
-- has drops and no kills; those rows say 'no kill data' rather than divide by
-- zero.
WITH drops AS (
    SELECT origin_detail AS monster, item_id, COUNT(*) AS drops
    FROM item_instances
    WHERE origin_type = 1 AND origin_detail IS NOT NULL
    GROUP BY origin_detail, item_id
),
kills AS (
    SELECT npc_name              AS monster,
           SUM(kills)            AS kills,
           SUM(rep_factor_sum)   AS rep_factor_sum,
           COUNT(DISTINCT npc_id) AS configs
    FROM npc_kills
    GROUP BY npc_name
)
SELECT
    d.monster,
    d.item_id,
    d.drops,
    k.kills,
    k.configs,
    CASE WHEN k.kills IS NULL THEN 'no kill data'
         ELSE CAST(CAST(ROUND(k.kills * 1.0 / d.drops) AS INTEGER) AS TEXT)
    END AS observed_1_in_n,
    -- The reputation layer scales gear and unique rows per killer (#88), so the
    -- honest denominator for those rows is the summed factor, not the raw count.
    -- Printed beside the count so a divergence caused by WHO was farming is
    -- visible rather than mysterious. It equals `kills` in a world where nobody
    -- has voted.
    ROUND(COALESCE(k.rep_factor_sum, 0), 2) AS rep_factor_sum
FROM drops d
LEFT JOIN kills k ON k.monster = d.monster
ORDER BY d.drops DESC, d.monster, d.item_id;

-- The authored side is deliberately absent from this file. Only the server can
-- resolve it -- the generosity stack, the saturation clamp and the guaranteed
-- head all live in code -- and a second implementation of that arithmetic that
-- disagreed with the roller would be worse than no report at all. Run
-- `Scripts/drop_audit.py` for the joined observed-vs-authored view; it consumes
-- the server's own `dropodds rows` output.

SELECT '--- observed drops per day, by monster ---' AS section;

SELECT
    date(created_at, 'unixepoch', 'localtime') AS day,
    origin_detail                              AS monster,
    COUNT(*)                                   AS drops,
    COUNT(DISTINCT item_id)                    AS distinct_items
FROM item_instances
WHERE origin_type = 1 AND origin_detail IS NOT NULL
GROUP BY day, monster
ORDER BY day DESC, drops DESC;

SELECT '--- observed tier mix per day (the loot-grade curve, as played) ---' AS section;

-- The signal that caught #63: every monster rolling one grade's curve. A tier mix
-- that does not move between a vermin and a boss is the shape of that fault.
SELECT
    date(created_at, 'unixepoch', 'localtime') AS day,
    SUM(CASE WHEN tier = 1 THEN 1 ELSE 0 END) AS common,
    SUM(CASE WHEN tier = 2 THEN 1 ELSE 0 END) AS rare,
    SUM(CASE WHEN tier = 3 THEN 1 ELSE 0 END) AS epic,
    SUM(CASE WHEN tier = 4 THEN 1 ELSE 0 END) AS legendary,
    COUNT(*)                                  AS tiered_drops
FROM item_instances
WHERE origin_type = 1 AND origin_detail IS NOT NULL AND tier > 0
GROUP BY day
ORDER BY day DESC;

SELECT '--- the Counted tier, for completeness (no monster available) ---' AS section;

-- item_flows stores a QUANTITY, not an occurrence count: a gold drop books the
-- number of coins, not the fact that one drop happened. So these rows say how
-- much moved, never how often -- which is why the per-monster work above is
-- Instanced-only.
SELECT
    day,
    item_id,
    flow_type,
    qty
FROM item_flows
WHERE flow_type = 5          -- NewGenDrop
ORDER BY day DESC, qty DESC;
