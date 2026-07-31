-- 02_entering_population.sql -- what actually enters the player economy.
--
-- Acceptance criterion 4. A drop is not a gain: an item only joins the population
-- when somebody takes it. The FIRST pickup of a Serial is that moment, and it can
-- never happen twice for the same item, which is what separates this from a count
-- of `get` events (an item dropped and re-taken books two of those).
--
-- Read this against 03_attrition.sql. Together they say, per item type, what
-- share of what the world produced players thought worth carrying.
--
-- Reads itemledger.db. Writes nothing.
-- Issue #85, plan P4.3.

.mode column
.headers on

SELECT '--- entering population: first pickup per item type ---' AS section;

-- event_type 3 is `get`. MIN(event_id) rather than MIN(at) because two events can
-- share a second and event_id is the ledger's own order.
WITH first_pickup AS (
    SELECT serial, MIN(event_id) AS event_id
    FROM item_events
    WHERE event_type = 3
    GROUP BY serial
),
entered AS (
    SELECT i.item_id, i.origin_type, i.origin_detail, i.tier, e.at, e.actor_char
    FROM first_pickup f
    JOIN item_events   e ON e.event_id = f.event_id
    JOIN item_instances i ON i.serial   = f.serial
)
SELECT
    item_id,
    COUNT(*)                                   AS entered,
    COUNT(DISTINCT actor_char)                 AS distinct_takers,
    SUM(CASE WHEN origin_type = 1 THEN 1 ELSE 0 END) AS from_monsters,
    SUM(CASE WHEN tier > 0 THEN 1 ELSE 0 END)  AS tiered,
    datetime(MIN(at), 'unixepoch', 'localtime') AS first_entry,
    datetime(MAX(at), 'unixepoch', 'localtime') AS last_entry
FROM entered
GROUP BY item_id
ORDER BY entered DESC;

SELECT '--- entering population per day ---' AS section;

WITH first_pickup AS (
    SELECT serial, MIN(event_id) AS event_id
    FROM item_events WHERE event_type = 3 GROUP BY serial
)
SELECT
    date(e.at, 'unixepoch', 'localtime') AS day,
    COUNT(*)                             AS entered,
    COUNT(DISTINCT i.item_id)            AS distinct_items,
    COUNT(DISTINCT e.actor_char)         AS distinct_takers
FROM first_pickup f
JOIN item_events   e ON e.event_id = f.event_id
JOIN item_instances i ON i.serial   = f.serial
GROUP BY day
ORDER BY day DESC;

SELECT '--- how long a drop waits on the ground before somebody takes it ---' AS section;

-- A long wait on a common item is the same signal attrition reports from the
-- other side: players walk past it. Measured from birth, because a monster drop
-- is placed on the ground the instant it is minted.
WITH first_pickup AS (
    SELECT serial, MIN(event_id) AS event_id
    FROM item_events WHERE event_type = 3 GROUP BY serial
),
waits AS (
    SELECT i.item_id, (e.at - i.created_at) AS wait_seconds
    FROM first_pickup f
    JOIN item_events   e ON e.event_id = f.event_id
    JOIN item_instances i ON i.serial   = f.serial
    WHERE i.origin_type = 1 AND e.at >= i.created_at
)
SELECT
    item_id,
    COUNT(*)              AS picked_up,
    MIN(wait_seconds)     AS fastest_s,
    CAST(AVG(wait_seconds) AS INTEGER) AS average_s,
    MAX(wait_seconds)     AS slowest_s
FROM waits
GROUP BY item_id
ORDER BY average_s DESC;

SELECT '--- the Counted tier: quantity entering, per day and item ---' AS section;

-- flow_type 3 is `get`. These are quantities, not occurrences: 5,000 means five
-- thousand coins or arrows moved, not five thousand pickups. There is also no
-- first-pickup notion here, because a stack has no identity to have a first of.
SELECT day, item_id, qty AS quantity_picked_up
FROM item_flows
WHERE flow_type = 3
ORDER BY day DESC, qty DESC;
