-- 00_overview.sql -- what is in this ledger, and where its holes are.
--
-- Run this first. Every other query in the pack is only as trustworthy as the
-- window it reads, and two facts decide that window: how much play the ledger
-- has seen, and how many server runs ended badly enough to lose their tail.
--
-- Reads itemledger.db. Writes nothing.
-- Issue #85, plan P4.3.

.mode column
.headers on

SELECT '--- size ---' AS section;

SELECT 'item_instances' AS "table", COUNT(*) AS rows FROM item_instances
UNION ALL SELECT 'item_events',  COUNT(*) FROM item_events
UNION ALL SELECT 'item_flows',   COUNT(*) FROM item_flows
UNION ALL SELECT 'npc_kills',    COUNT(*) FROM npc_kills;

SELECT '--- window ---' AS section;

SELECT
    datetime(MIN(at), 'unixepoch', 'localtime') AS first_event,
    datetime(MAX(at), 'unixepoch', 'localtime') AS last_event,
    ROUND((MAX(at) - MIN(at)) / 86400.0, 2)     AS days_covered
FROM item_events;

SELECT '--- server runs, and which ones lost their tail ---' AS section;

-- A boundary event (103) is written at every boot with serial 0. Its detail says
-- how the PREVIOUS run ended. A run that ended in a crash lost whatever was still
-- buffered, so an apparent anomaly straddling one of these is a hole, not a fault.
SELECT
    event_id,
    datetime(at, 'unixepoch', 'localtime') AS booted_at,
    detail,
    CASE
        WHEN detail LIKE '%"crash"%' THEN 'PREVIOUS RUN CRASHED - events may be missing here'
        WHEN detail LIKE '%"clean"%' THEN 'clean'
        ELSE 'first run'
    END AS verdict
FROM item_events
WHERE event_type = 103
ORDER BY event_id DESC
LIMIT 40;

SELECT '--- how items entered the world ---' AS section;

-- origin_type is item_origin (Sources/Server/ItemProvenance.h). 1 = npc_drop is
-- the only one the drop analytics read; the rest are named so a world whose
-- population is mostly crafted or GM-minted says so plainly.
SELECT
    origin_type,
    CASE origin_type
        WHEN 1 THEN 'npc_drop'      WHEN 2 THEN 'shop_buy'    WHEN 3 THEN 'craft'
        WHEN 4 THEN 'quest'         WHEN 5 THEN 'fishing'     WHEN 6 THEN 'mining'
        WHEN 7 THEN 'harvest'       WHEN 8 THEN 'lottery'     WHEN 9 THEN 'war_reward'
        WHEN 10 THEN 'hero_reward'  WHEN 11 THEN 'dark_claim' WHEN 12 THEN 'angel_claim'
        WHEN 13 THEN 'magic'        WHEN 14 THEN 'event_reward'
        WHEN 15 THEN 'gm_mint'      WHEN 16 THEN 'restored'
        ELSE 'none'
    END AS origin,
    COUNT(*) AS items,
    -- A drop with no monster name is not a drop: the contract provers mint real
    -- items through the factory as npc_drop with no origin_detail, and counting
    -- those as loot would inflate every observed rate.
    SUM(CASE WHEN origin_detail IS NULL THEN 1 ELSE 0 END) AS unattributed
FROM item_instances
GROUP BY origin_type
ORDER BY items DESC;

SELECT '--- event mix ---' AS section;

SELECT
    event_type,
    COUNT(*)                                    AS events,
    datetime(MIN(at), 'unixepoch', 'localtime') AS first_seen,
    datetime(MAX(at), 'unixepoch', 'localtime') AS last_seen
FROM item_events
GROUP BY event_type
ORDER BY events DESC;
