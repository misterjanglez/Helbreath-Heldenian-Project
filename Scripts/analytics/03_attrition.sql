-- 03_attrition.sql -- what the world produced and nobody wanted.
--
-- Acceptance criterion 5. A despawn with no pickup before it is the clearest
-- statement the ledger can make about loot: the item was created, it lay on the
-- ground, its lifetime ran out, and no player thought it worth the walk. That is
-- the tuning signal Loot 2.0 asked for, and it is why the timed ground cleanup
-- was added in #79 (ground_item_lifetime_ms; 0 disables it, and then this whole
-- query stays empty because nothing ever despawns).
--
-- Reads itemledger.db. Writes nothing.
-- Issue #85, plan P4.3.

.mode column
.headers on

SELECT '--- attrition per item type: despawned without ever being picked up ---' AS section;

-- Three facts per Serial: was it despawned (101), was it ever picked up (3), and
-- when was the pickup relative to the despawn. The last one matters: an item can
-- be taken, dropped again and left to despawn, which is attrition of a different
-- kind -- the player HAD it and let it go. Both are reported, separately.
WITH despawned AS (
    SELECT serial, MIN(event_id) AS despawn_event, MIN(at) AS despawn_at
    FROM item_events WHERE event_type = 101 GROUP BY serial
),
first_pickup AS (
    SELECT serial, MIN(event_id) AS pickup_event
    FROM item_events WHERE event_type = 3 GROUP BY serial
)
SELECT
    i.item_id,
    COUNT(*) AS despawned,
    SUM(CASE WHEN p.pickup_event IS NULL THEN 1 ELSE 0 END) AS never_picked_up,
    SUM(CASE WHEN p.pickup_event IS NOT NULL THEN 1 ELSE 0 END) AS taken_then_abandoned,
    SUM(CASE WHEN i.origin_type = 1 THEN 1 ELSE 0 END) AS from_monsters,
    CAST(AVG(d.despawn_at - i.created_at) AS INTEGER) AS average_life_s
FROM despawned d
JOIN item_instances i ON i.serial = d.serial
LEFT JOIN first_pickup p ON p.serial = d.serial
GROUP BY i.item_id
ORDER BY never_picked_up DESC, despawned DESC;

SELECT '--- attrition rate: of everything a monster dropped, what was left ---' AS section;

-- The share is what makes this actionable. 900 of 1000 Health Potions left to rot
-- says the row is not worth its slot in the table; 2 of 1000 says it is.
WITH loot AS (
    SELECT serial, item_id, origin_detail AS monster
    FROM item_instances
    WHERE origin_type = 1 AND origin_detail IS NOT NULL
),
picked AS (
    SELECT DISTINCT serial FROM item_events WHERE event_type = 3
),
gone AS (
    SELECT DISTINCT serial FROM item_events WHERE event_type = 101
)
SELECT
    l.item_id,
    COUNT(*) AS dropped,
    SUM(CASE WHEN p.serial IS NOT NULL THEN 1 ELSE 0 END) AS ever_taken,
    SUM(CASE WHEN g.serial IS NOT NULL AND p.serial IS NULL THEN 1 ELSE 0 END) AS left_to_despawn,
    -- Still on the ground, or still held, or gone by another exit. Counted rather
    -- than accused: a live world always has some, and calling them attrition
    -- would overstate it.
    SUM(CASE WHEN g.serial IS NULL AND p.serial IS NULL THEN 1 ELSE 0 END) AS unresolved,
    ROUND(100.0 * SUM(CASE WHEN g.serial IS NOT NULL AND p.serial IS NULL THEN 1 ELSE 0 END)
          / COUNT(*), 1) AS pct_wasted
FROM loot l
LEFT JOIN picked p ON p.serial = l.serial
LEFT JOIN gone   g ON g.serial = l.serial
GROUP BY l.item_id
ORDER BY pct_wasted DESC, dropped DESC;

SELECT '--- attrition by source monster ---' AS section;

-- Which monsters produce loot players ignore. A high share here on a monster
-- players farm is a roster problem, not a rate problem.
WITH loot AS (
    SELECT serial, origin_detail AS monster
    FROM item_instances
    WHERE origin_type = 1 AND origin_detail IS NOT NULL
),
picked AS (SELECT DISTINCT serial FROM item_events WHERE event_type = 3),
gone   AS (SELECT DISTINCT serial FROM item_events WHERE event_type = 101)
SELECT
    l.monster,
    COUNT(*) AS dropped,
    SUM(CASE WHEN g.serial IS NOT NULL AND p.serial IS NULL THEN 1 ELSE 0 END) AS left_to_despawn,
    ROUND(100.0 * SUM(CASE WHEN g.serial IS NOT NULL AND p.serial IS NULL THEN 1 ELSE 0 END)
          / COUNT(*), 1) AS pct_wasted
FROM loot l
LEFT JOIN picked p ON p.serial = l.serial
LEFT JOIN gone   g ON g.serial = l.serial
GROUP BY l.monster
ORDER BY pct_wasted DESC, dropped DESC;

SELECT '--- the Counted tier: quantity despawned, per day and item ---' AS section;

-- flow_type 101 is `despawned`. Compare against flow_type 3 (`get`) in
-- 02_entering_population.sql for the stackable half of the same ratio -- but
-- remember both are quantities, so this is coins and arrows, not occurrences.
SELECT day, item_id, qty AS quantity_despawned
FROM item_flows
WHERE flow_type = 101
ORDER BY day DESC, qty DESC;
