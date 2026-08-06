/*
 Heldenian castle-siege data delta (issue #95, Phase 0 epic #92)
 Applies IN PLACE to an existing MapInfo.db (safe to re-run).

 - map_heldenian_gate_doors: the 4 hrampart gates from the original HRampart.txt
   (`HeldenianGateDoor <dir> <x> <y>`, identical in repos/HelbreathServer and HB382_CENTUU).
 - map_heldenian_winning_zone: NEW table. The original GodH.txt ships
   `HeldenianWinningZone = 59 72` (single tile, parsed but discarded upstream);
   we store a 3x3 zone centered on that tile.
 - map_teleport_locations: the original HRampart.txt sends the wall-breach
   teleport to GodH (455,204) — out of bounds on the 250x250 GodH map (both
   reference repos agree on the map binary). Redirect to GodH's own
   initial point (204,172), which is walkable and beside the return teleports.
*/

BEGIN TRANSACTION;

-- Door direction selects the art diagonal: 3 (east) = rising, 7 (west) = falling.
-- The original ships 3 for all four, but the painted gatehouse walls differ per
-- castle flank (owner-verified in-game): the east/west gatehouses need the falling
-- sheet, the north/south ones the rising sheet. The walkability layer is identical
-- at all four, so this is purely visual; the server's archway seal derives from
-- the .amd walls, not from this field.
-- Coordinates: the original arch-middle anchors. The half-tile visual seating is
-- carried by the gate.pak frame pivots (+16,-16 vs the original pak), tuned in-game:
-- middle-tile anchor hugged the left wall, the NE end tile overshot.
INSERT OR REPLACE INTO map_heldenian_gate_doors (map_name, door_index, direction, x, y) VALUES
	('hrampart', 0, 3,  98,  49),
	('hrampart', 1, 7, 134,  73),
	('hrampart', 2, 3,  86, 129),
	('hrampart', 3, 7,  74,  73);

CREATE TABLE IF NOT EXISTS map_heldenian_winning_zone (
	map_name TEXT PRIMARY KEY,
	tile_x INTEGER NOT NULL,
	tile_y INTEGER NOT NULL,
	tile_w INTEGER NOT NULL,
	tile_h INTEGER NOT NULL,
	FOREIGN KEY (map_name) REFERENCES maps(map_name) ON DELETE CASCADE
);

INSERT OR REPLACE INTO map_heldenian_winning_zone (map_name, tile_x, tile_y, tile_w, tile_h) VALUES
	('godh', 58, 71, 3, 3);

UPDATE map_teleport_locations
	SET dest_x = 204, dest_y = 172
	WHERE map_name = 'hrampart' AND dest_map_name = 'godh' AND dest_x = 455;

COMMIT;
