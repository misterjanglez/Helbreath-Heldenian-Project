/*
 Navicat Premium Data Transfer

 Source Server         : MapInfo
 Source Server Type    : SQLite
 Source Server Version : 3035005 (3.35.5)
 Source Schema         : main

 Target Server Type    : SQLite
 Target Server Version : 3035005 (3.35.5)
 File Encoding         : 65001

 Date: 16/02/2026 13:22:52
*/

PRAGMA foreign_keys = false;

-- ----------------------------
-- Table structure for map_apocalypse_boss
-- ----------------------------
DROP TABLE IF EXISTS "map_apocalypse_boss";
CREATE TABLE "map_apocalypse_boss" (
  "map_name" TEXT,
  "npc_id" INTEGER NOT NULL,
  "tile_x" INTEGER NOT NULL,
  "tile_y" INTEGER NOT NULL,
  "tile_w" INTEGER NOT NULL,
  "tile_h" INTEGER NOT NULL,
  PRIMARY KEY ("map_name"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_apocalypse_boss
-- ----------------------------
INSERT INTO "map_apocalypse_boss" VALUES ('abaddon', 71, 79, 76, 57, 31);

-- ----------------------------
-- Table structure for map_dynamic_gate
-- ----------------------------
DROP TABLE IF EXISTS "map_dynamic_gate";
CREATE TABLE "map_dynamic_gate" (
  "map_name" TEXT,
  "gate_type" INTEGER NOT NULL,
  "tile_x" INTEGER NOT NULL,
  "tile_y" INTEGER NOT NULL,
  "tile_w" INTEGER NOT NULL,
  "tile_h" INTEGER NOT NULL,
  "dest_map" TEXT NOT NULL,
  "dest_x" INTEGER NOT NULL,
  "dest_y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (length(dest_map) <= 10)
);

-- ----------------------------
-- Records of map_dynamic_gate
-- ----------------------------
INSERT INTO "map_dynamic_gate" VALUES ('inferniaa', 2, 115, 105, 0, 0, 'maze', -1, -1);
INSERT INTO "map_dynamic_gate" VALUES ('inferniab', 2, 39, 119, 0, 0, 'maze', -1, -1);
INSERT INTO "map_dynamic_gate" VALUES ('procella', 2, 59, 196, 1, 1, 'abaddon', -1, -1);

-- ----------------------------
-- Table structure for map_energy_sphere_creation
-- ----------------------------
DROP TABLE IF EXISTS "map_energy_sphere_creation";
CREATE TABLE "map_energy_sphere_creation" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "sphere_type" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_energy_sphere_creation
-- ----------------------------
INSERT INTO "map_energy_sphere_creation" VALUES ('fightzone5', 0, 1, 66, 37);
INSERT INTO "map_energy_sphere_creation" VALUES ('fightzone6', 0, 1, 58, 45);
INSERT INTO "map_energy_sphere_creation" VALUES ('fightzone7', 0, 1, 56, 43);
INSERT INTO "map_energy_sphere_creation" VALUES ('fightzone8', 0, 1, 59, 45);
INSERT INTO "map_energy_sphere_creation" VALUES ('middleland', 0, 1, 161, 219);

-- ----------------------------
-- Table structure for map_energy_sphere_goal
-- ----------------------------
DROP TABLE IF EXISTS "map_energy_sphere_goal";
CREATE TABLE "map_energy_sphere_goal" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "result" INTEGER NOT NULL,
  "aresden_x" INTEGER NOT NULL,
  "aresden_y" INTEGER NOT NULL,
  "elvine_x" INTEGER NOT NULL,
  "elvine_y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_energy_sphere_goal
-- ----------------------------
INSERT INTO "map_energy_sphere_goal" VALUES ('fightzone5', 0, 1, 43, 29, 76, 54);
INSERT INTO "map_energy_sphere_goal" VALUES ('fightzone6', 0, 1, 34, 60, 80, 30);
INSERT INTO "map_energy_sphere_goal" VALUES ('fightzone7', 0, 1, 41, 60, 72, 27);
INSERT INTO "map_energy_sphere_goal" VALUES ('fightzone8', 0, 1, 57, 30, 59, 60);
INSERT INTO "map_energy_sphere_goal" VALUES ('middleland', 0, 1, 237, 182, 179, 284);

-- ----------------------------
-- Table structure for map_fish_points
-- ----------------------------
DROP TABLE IF EXISTS "map_fish_points";
CREATE TABLE "map_fish_points" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_fish_points
-- ----------------------------
INSERT INTO "map_fish_points" VALUES ('arefarm', 0, 222, 17);
INSERT INTO "map_fish_points" VALUES ('arefarm', 1, 235, 30);
INSERT INTO "map_fish_points" VALUES ('arefarm', 2, 236, 58);
INSERT INTO "map_fish_points" VALUES ('arefarm', 3, 231, 78);
INSERT INTO "map_fish_points" VALUES ('arefarm', 4, 234, 224);
INSERT INTO "map_fish_points" VALUES ('arefarm', 5, 224, 234);
INSERT INTO "map_fish_points" VALUES ('arefarm', 6, 196, 229);
INSERT INTO "map_fish_points" VALUES ('arefarm', 7, 97, 228);
INSERT INTO "map_fish_points" VALUES ('arefarm', 8, 60, 229);
INSERT INTO "map_fish_points" VALUES ('arefarm', 9, 33, 236);
INSERT INTO "map_fish_points" VALUES ('arefarm', 10, 11, 223);
INSERT INTO "map_fish_points" VALUES ('arefarm', 11, 18, 190);
INSERT INTO "map_fish_points" VALUES ('arefarm', 12, 21, 164);
INSERT INTO "map_fish_points" VALUES ('arefarm', 13, 15, 149);
INSERT INTO "map_fish_points" VALUES ('arefarm', 14, 15, 106);
INSERT INTO "map_fish_points" VALUES ('aresden', 0, 59, 99);
INSERT INTO "map_fish_points" VALUES ('aresden', 1, 60, 98);
INSERT INTO "map_fish_points" VALUES ('aresden', 2, 60, 99);
INSERT INTO "map_fish_points" VALUES ('aresden', 3, 76, 88);
INSERT INTO "map_fish_points" VALUES ('aresden', 4, 77, 87);
INSERT INTO "map_fish_points" VALUES ('aresden', 5, 78, 87);
INSERT INTO "map_fish_points" VALUES ('aresden', 6, 90, 91);
INSERT INTO "map_fish_points" VALUES ('aresden', 7, 91, 92);
INSERT INTO "map_fish_points" VALUES ('aresden', 8, 79, 98);
INSERT INTO "map_fish_points" VALUES ('aresden', 9, 79, 98);
INSERT INTO "map_fish_points" VALUES ('aresden', 10, 80, 98);
INSERT INTO "map_fish_points" VALUES ('aresden', 11, 81, 98);
INSERT INTO "map_fish_points" VALUES ('aresden', 12, 89, 107);
INSERT INTO "map_fish_points" VALUES ('aresden', 13, 89, 108);
INSERT INTO "map_fish_points" VALUES ('aresden', 14, 90, 107);
INSERT INTO "map_fish_points" VALUES ('aresden', 15, 108, 103);
INSERT INTO "map_fish_points" VALUES ('aresden', 16, 109, 104);
INSERT INTO "map_fish_points" VALUES ('aresden', 17, 67, 108);
INSERT INTO "map_fish_points" VALUES ('aresden', 18, 68, 109);
INSERT INTO "map_fish_points" VALUES ('aresden', 19, 69, 109);
INSERT INTO "map_fish_points" VALUES ('aresden', 20, 85, 120);
INSERT INTO "map_fish_points" VALUES ('aresden', 21, 86, 121);
INSERT INTO "map_fish_points" VALUES ('aresden', 22, 87, 121);
INSERT INTO "map_fish_points" VALUES ('aresden', 23, 99, 113);
INSERT INTO "map_fish_points" VALUES ('aresden', 24, 100, 112);
INSERT INTO "map_fish_points" VALUES ('aresden', 25, 252, 277);
INSERT INTO "map_fish_points" VALUES ('aresden', 26, 252, 278);
INSERT INTO "map_fish_points" VALUES ('aresden', 27, 273, 275);
INSERT INTO "map_fish_points" VALUES ('aresden', 28, 274, 275);
INSERT INTO "map_fish_points" VALUES ('bisle', 0, 204, 104);
INSERT INTO "map_fish_points" VALUES ('bisle', 1, 206, 105);
INSERT INTO "map_fish_points" VALUES ('bisle', 2, 217, 102);
INSERT INTO "map_fish_points" VALUES ('bisle', 3, 224, 98);
INSERT INTO "map_fish_points" VALUES ('bisle', 4, 229, 102);
INSERT INTO "map_fish_points" VALUES ('bisle', 5, 210, 30);
INSERT INTO "map_fish_points" VALUES ('bisle', 6, 212, 29);
INSERT INTO "map_fish_points" VALUES ('bisle', 7, 148, 36);
INSERT INTO "map_fish_points" VALUES ('bisle', 8, 146, 36);
INSERT INTO "map_fish_points" VALUES ('bisle', 9, 103, 221);
INSERT INTO "map_fish_points" VALUES ('bisle', 10, 105, 222);
INSERT INTO "map_fish_points" VALUES ('bisle', 11, 102, 220);
INSERT INTO "map_fish_points" VALUES ('bisle', 12, 186, 200);
INSERT INTO "map_fish_points" VALUES ('bisle', 13, 187, 202);
INSERT INTO "map_fish_points" VALUES ('bisle', 14, 35, 40);
INSERT INTO "map_fish_points" VALUES ('bisle', 15, 34, 39);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 0, 235, 151);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 1, 236, 175);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 2, 236, 187);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 3, 238, 202);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 4, 234, 216);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 5, 234, 230);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 6, 220, 237);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 7, 206, 235);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 8, 13, 114);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 9, 16, 87);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 10, 17, 52);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 11, 54, 20);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 12, 82, 13);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 13, 129, 12);
INSERT INTO "map_fish_points" VALUES ('elvfarm', 14, 144, 18);
INSERT INTO "map_fish_points" VALUES ('elvine', 0, 186, 190);
INSERT INTO "map_fish_points" VALUES ('elvine', 1, 186, 191);
INSERT INTO "map_fish_points" VALUES ('elvine', 2, 187, 190);
INSERT INTO "map_fish_points" VALUES ('elvine', 3, 199, 182);
INSERT INTO "map_fish_points" VALUES ('elvine', 4, 200, 181);
INSERT INTO "map_fish_points" VALUES ('elvine', 5, 206, 177);
INSERT INTO "map_fish_points" VALUES ('elvine', 6, 207, 177);
INSERT INTO "map_fish_points" VALUES ('elvine', 7, 208, 178);
INSERT INTO "map_fish_points" VALUES ('elvine', 8, 217, 184);
INSERT INTO "map_fish_points" VALUES ('elvine', 9, 219, 185);
INSERT INTO "map_fish_points" VALUES ('elvine', 10, 229, 194);
INSERT INTO "map_fish_points" VALUES ('elvine', 11, 230, 193);
INSERT INTO "map_fish_points" VALUES ('elvine', 12, 219, 201);
INSERT INTO "map_fish_points" VALUES ('elvine', 13, 220, 200);
INSERT INTO "map_fish_points" VALUES ('elvine', 14, 221, 199);
INSERT INTO "map_fish_points" VALUES ('elvine', 15, 206, 209);
INSERT INTO "map_fish_points" VALUES ('elvine', 16, 207, 209);
INSERT INTO "map_fish_points" VALUES ('elvine', 17, 208, 208);
INSERT INTO "map_fish_points" VALUES ('elvine', 18, 193, 212);
INSERT INTO "map_fish_points" VALUES ('elvine', 19, 195, 213);
INSERT INTO "map_fish_points" VALUES ('elvine', 20, 178, 202);
INSERT INTO "map_fish_points" VALUES ('elvine', 21, 179, 202);
INSERT INTO "map_fish_points" VALUES ('elvine', 22, 180, 203);
INSERT INTO "map_fish_points" VALUES ('elvine', 23, 196, 19);
INSERT INTO "map_fish_points" VALUES ('elvine', 24, 197, 19);
INSERT INTO "map_fish_points" VALUES ('elvine', 25, 238, 25);
INSERT INTO "map_fish_points" VALUES ('elvine', 26, 238, 26);
INSERT INTO "map_fish_points" VALUES ('middleland', 0, 35, 241);
INSERT INTO "map_fish_points" VALUES ('middleland', 1, 64, 251);
INSERT INTO "map_fish_points" VALUES ('middleland', 2, 83, 248);
INSERT INTO "map_fish_points" VALUES ('middleland', 3, 95, 234);
INSERT INTO "map_fish_points" VALUES ('middleland', 4, 136, 235);
INSERT INTO "map_fish_points" VALUES ('middleland', 5, 167, 243);
INSERT INTO "map_fish_points" VALUES ('middleland', 6, 218, 253);
INSERT INTO "map_fish_points" VALUES ('middleland', 7, 256, 252);
INSERT INTO "map_fish_points" VALUES ('middleland', 8, 246, 233);
INSERT INTO "map_fish_points" VALUES ('middleland', 9, 300, 260);
INSERT INTO "map_fish_points" VALUES ('middleland', 10, 313, 248);
INSERT INTO "map_fish_points" VALUES ('middleland', 11, 424, 289);
INSERT INTO "map_fish_points" VALUES ('middleland', 12, 435, 263);
INSERT INTO "map_fish_points" VALUES ('middleland', 13, 461, 260);
INSERT INTO "map_fish_points" VALUES ('middleland', 14, 469, 291);

-- ----------------------------
-- Table structure for map_heldenian_gate_doors
-- ----------------------------
DROP TABLE IF EXISTS "map_heldenian_gate_doors";
CREATE TABLE "map_heldenian_gate_doors" (
  "map_name" TEXT NOT NULL,
  "door_index" INTEGER NOT NULL,
  "direction" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "door_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_heldenian_gate_doors
-- ----------------------------

-- ----------------------------
-- Table structure for map_heldenian_towers
-- ----------------------------
DROP TABLE IF EXISTS "map_heldenian_towers";
CREATE TABLE "map_heldenian_towers" (
  "map_name" TEXT NOT NULL,
  "tower_index" INTEGER NOT NULL,
  "type_id" INTEGER NOT NULL,
  "side" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "tower_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_heldenian_towers
-- ----------------------------
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 0, 87, 1, 53, 212);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 1, 87, 1, 48, 214);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 2, 87, 1, 53, 220);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 3, 87, 1, 48, 222);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 4, 87, 1, 53, 228);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 5, 87, 1, 48, 230);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 6, 87, 1, 53, 236);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 7, 87, 1, 53, 240);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 8, 87, 1, 53, 244);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 9, 87, 1, 57, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 10, 87, 1, 62, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 11, 87, 1, 67, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 12, 87, 1, 70, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 13, 87, 1, 77, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 14, 87, 1, 80, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 15, 87, 1, 87, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 16, 87, 1, 95, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 17, 87, 1, 97, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 18, 87, 1, 117, 144);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 19, 87, 1, 114, 136);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 20, 87, 1, 180, 180);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 21, 87, 1, 182, 188);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 22, 89, 1, 48, 210);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 23, 89, 1, 53, 216);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 24, 89, 1, 48, 218);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 25, 89, 1, 53, 224);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 26, 89, 1, 48, 226);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 27, 89, 1, 53, 232);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 28, 89, 1, 48, 234);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 29, 89, 1, 48, 238);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 30, 89, 1, 53, 240);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 31, 89, 1, 48, 246);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 32, 89, 1, 55, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 33, 89, 1, 62, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 34, 89, 1, 65, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 35, 89, 1, 72, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 36, 89, 1, 75, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 37, 89, 1, 82, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 38, 89, 1, 85, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 39, 89, 1, 90, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 40, 89, 1, 92, 248);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 41, 89, 1, 100, 252);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 42, 89, 1, 120, 141);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 43, 89, 1, 111, 139);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 44, 89, 1, 177, 183);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 45, 89, 1, 185, 185);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 46, 87, 2, 195, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 47, 87, 2, 197, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 48, 87, 2, 205, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 49, 87, 2, 207, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 50, 87, 2, 215, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 51, 87, 2, 222, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 52, 87, 2, 225, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 53, 87, 2, 232, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 54, 87, 2, 235, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 55, 87, 2, 241, 69);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 56, 87, 2, 246, 71);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 57, 87, 2, 241, 77);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 58, 87, 2, 246, 79);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 59, 87, 2, 241, 85);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 60, 87, 2, 246, 91);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 61, 87, 2, 241, 93);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 62, 87, 2, 246, 99);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 63, 87, 2, 241, 101);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 64, 87, 2, 203, 163);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 65, 87, 2, 205, 171);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 66, 87, 2, 134, 122);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 67, 87, 2, 142, 124);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 68, 89, 2, 192, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 69, 89, 2, 200, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 70, 89, 2, 202, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 71, 89, 2, 210, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 72, 89, 2, 212, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 73, 89, 2, 217, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 74, 89, 2, 220, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 75, 89, 2, 227, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 76, 89, 2, 230, 62);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 77, 89, 2, 237, 58);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 78, 89, 2, 246, 67);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 79, 89, 2, 241, 73);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 80, 89, 2, 246, 75);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 81, 89, 2, 241, 81);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 82, 89, 2, 246, 83);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 83, 89, 2, 246, 87);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 84, 89, 2, 241, 89);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 85, 89, 2, 246, 95);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 86, 89, 2, 241, 97);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 87, 89, 2, 246, 103);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 88, 89, 2, 200, 166);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 89, 89, 2, 208, 168);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 90, 89, 2, 137, 119);
INSERT INTO "map_heldenian_towers" VALUES ('btfield', 91, 89, 2, 139, 127);

-- ----------------------------
-- Table structure for map_initial_points
-- ----------------------------
DROP TABLE IF EXISTS "map_initial_points";
CREATE TABLE "map_initial_points" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_initial_points
-- ----------------------------
INSERT INTO "map_initial_points" VALUES ('2ndmiddle', 0, 125, 125);
INSERT INTO "map_initial_points" VALUES ('abaddon', 0, 168, 170);
INSERT INTO "map_initial_points" VALUES ('arebrk11', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('arebrk12', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('arebrk21', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('arebrk22', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('arefarm', 0, 50, 95);
INSERT INTO "map_initial_points" VALUES ('arejail', 0, 105, 55);
INSERT INTO "map_initial_points" VALUES ('aresden', 0, 140, 49);
INSERT INTO "map_initial_points" VALUES ('aresden', 1, 68, 125);
INSERT INTO "map_initial_points" VALUES ('aresden', 2, 170, 145);
INSERT INTO "map_initial_points" VALUES ('aresden', 3, 140, 205);
INSERT INTO "map_initial_points" VALUES ('aresden', 4, 116, 245);
INSERT INTO "map_initial_points" VALUES ('aresdend1', 0, 111, 91);
INSERT INTO "map_initial_points" VALUES ('areuni', 0, 85, 23);
INSERT INTO "map_initial_points" VALUES ('arewrhus', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('bisle', 0, 144, 117);
INSERT INTO "map_initial_points" VALUES ('bsmith_1', 0, 41, 37);
INSERT INTO "map_initial_points" VALUES ('bsmith_1f', 0, 41, 37);
INSERT INTO "map_initial_points" VALUES ('bsmith_2', 0, 41, 37);
INSERT INTO "map_initial_points" VALUES ('bsmith_2f', 0, 41, 37);
INSERT INTO "map_initial_points" VALUES ('btfield', 0, 264, 260);
INSERT INTO "map_initial_points" VALUES ('cath_1', 0, 64, 45);
INSERT INTO "map_initial_points" VALUES ('cath_2', 0, 64, 45);
INSERT INTO "map_initial_points" VALUES ('cityhall_1', 0, 50, 41);
INSERT INTO "map_initial_points" VALUES ('cityhall_2', 0, 50, 41);
INSERT INTO "map_initial_points" VALUES ('cmdhall_1', 0, 40, 52);
INSERT INTO "map_initial_points" VALUES ('cmdhall_2', 0, 40, 52);
INSERT INTO "map_initial_points" VALUES ('default', 0, 134, 37);
INSERT INTO "map_initial_points" VALUES ('dglv2', 0, 384, 143);
INSERT INTO "map_initial_points" VALUES ('dglv3', 0, 187, 212);
INSERT INTO "map_initial_points" VALUES ('dglv4', 0, 62, 241);
INSERT INTO "map_initial_points" VALUES ('druncncity', 0, 108, 129);
INSERT INTO "map_initial_points" VALUES ('elvbrk11', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('elvbrk12', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('elvbrk21', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('elvbrk22', 0, 80, 80);
INSERT INTO "map_initial_points" VALUES ('elvfarm', 0, 124, 151);
INSERT INTO "map_initial_points" VALUES ('elvine', 0, 158, 57);
INSERT INTO "map_initial_points" VALUES ('elvine', 1, 110, 89);
INSERT INTO "map_initial_points" VALUES ('elvine', 2, 170, 145);
INSERT INTO "map_initial_points" VALUES ('elvine', 3, 242, 129);
INSERT INTO "map_initial_points" VALUES ('elvine', 4, 158, 249);
INSERT INTO "map_initial_points" VALUES ('elvined1', 0, 98, 92);
INSERT INTO "map_initial_points" VALUES ('elvjail', 0, 105, 55);
INSERT INTO "map_initial_points" VALUES ('elvuni', 0, 173, 24);
INSERT INTO "map_initial_points" VALUES ('elvwrhus', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('fightzone1', 0, 51, 56);
INSERT INTO "map_initial_points" VALUES ('fightzone2', 0, 51, 56);
INSERT INTO "map_initial_points" VALUES ('fightzone3', 0, 51, 56);
INSERT INTO "map_initial_points" VALUES ('fightzone4', 0, 45, 33);
INSERT INTO "map_initial_points" VALUES ('fightzone4', 1, 36, 39);
INSERT INTO "map_initial_points" VALUES ('fightzone5', 0, 48, 33);
INSERT INTO "map_initial_points" VALUES ('fightzone5', 1, 71, 49);
INSERT INTO "map_initial_points" VALUES ('fightzone6', 0, 70, 37);
INSERT INTO "map_initial_points" VALUES ('fightzone6', 1, 43, 53);
INSERT INTO "map_initial_points" VALUES ('fightzone7', 0, 43, 53);
INSERT INTO "map_initial_points" VALUES ('fightzone7', 1, 72, 33);
INSERT INTO "map_initial_points" VALUES ('fightzone8', 0, 47, 39);
INSERT INTO "map_initial_points" VALUES ('fightzone8', 1, 71, 50);
INSERT INTO "map_initial_points" VALUES ('fightzone9', 0, 51, 56);
INSERT INTO "map_initial_points" VALUES ('gldhall_1', 0, 37, 48);
INSERT INTO "map_initial_points" VALUES ('gldhall_2', 0, 37, 48);
INSERT INTO "map_initial_points" VALUES ('godh', 0, 204, 172);
INSERT INTO "map_initial_points" VALUES ('gshop_1', 0, 51, 41);
INSERT INTO "map_initial_points" VALUES ('gshop_1f', 0, 51, 41);
INSERT INTO "map_initial_points" VALUES ('gshop_2', 0, 51, 41);
INSERT INTO "map_initial_points" VALUES ('gshop_2f', 0, 51, 41);
INSERT INTO "map_initial_points" VALUES ('hrampart', 0, 120, 120);
INSERT INTO "map_initial_points" VALUES ('huntzone1', 0, 50, 122);
INSERT INTO "map_initial_points" VALUES ('huntzone1', 1, 103, 75);
INSERT INTO "map_initial_points" VALUES ('huntzone1', 2, 138, 57);
INSERT INTO "map_initial_points" VALUES ('huntzone2', 0, 44, 90);
INSERT INTO "map_initial_points" VALUES ('huntzone2', 1, 109, 99);
INSERT INTO "map_initial_points" VALUES ('huntzone2', 2, 149, 150);
INSERT INTO "map_initial_points" VALUES ('huntzone3', 0, 47, 165);
INSERT INTO "map_initial_points" VALUES ('huntzone4', 0, 23, 93);
INSERT INTO "map_initial_points" VALUES ('icebound', 0, 264, 260);
INSERT INTO "map_initial_points" VALUES ('inferniaa', 0, 34, 28);
INSERT INTO "map_initial_points" VALUES ('inferniab', 0, 117, 28);
INSERT INTO "map_initial_points" VALUES ('maze', 0, 102, 30);
INSERT INTO "map_initial_points" VALUES ('middled1n', 0, 34, 36);
INSERT INTO "map_initial_points" VALUES ('middled1n', 1, 181, 124);
INSERT INTO "map_initial_points" VALUES ('middled1x', 0, 70, 108);
INSERT INTO "map_initial_points" VALUES ('middleland', 0, 192, 228);
INSERT INTO "map_initial_points" VALUES ('procella', 0, 133, 23);
INSERT INTO "map_initial_points" VALUES ('resurr1', 0, 98, 74);
INSERT INTO "map_initial_points" VALUES ('resurr2', 0, 98, 74);
INSERT INTO "map_initial_points" VALUES ('toh1', 0, 145, 32);
INSERT INTO "map_initial_points" VALUES ('toh2', 0, 39, 39);
INSERT INTO "map_initial_points" VALUES ('toh2', 1, 272, 29);
INSERT INTO "map_initial_points" VALUES ('toh3', 0, 93, 40);
INSERT INTO "map_initial_points" VALUES ('toh3', 1, 147, 32);
INSERT INTO "map_initial_points" VALUES ('wrhus_1', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('wrhus_1f', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('wrhus_2', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('wrhus_2f', 0, 69, 43);
INSERT INTO "map_initial_points" VALUES ('wzdtwr_1', 0, 49, 36);
INSERT INTO "map_initial_points" VALUES ('wzdtwr_2', 0, 49, 36);

-- ----------------------------
-- Table structure for map_item_events
-- ----------------------------
DROP TABLE IF EXISTS "map_item_events";
CREATE TABLE "map_item_events" (
  "map_name" TEXT NOT NULL,
  "event_index" INTEGER NOT NULL,
  "item_name" TEXT NOT NULL,
  "amount" INTEGER NOT NULL,
  "total_num" INTEGER NOT NULL,
  "event_month" INTEGER NOT NULL,
  "event_day" INTEGER NOT NULL,
  "event_type" INTEGER NOT NULL DEFAULT 0,
  "mob_list" TEXT NOT NULL DEFAULT '',
  PRIMARY KEY ("map_name", "event_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (length(item_name) <= 20)
);

-- ----------------------------
-- Records of map_item_events
-- ----------------------------
INSERT INTO "map_item_events" VALUES ('middleland', 0, '����', 1, 200, 9, 8, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 1, '����', 1, 200, 9, 9, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 2, '����', 1, 200, 9, 10, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 3, '����', 1, 200, 9, 11, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 4, '����', 1, 200, 9, 12, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 5, '����', 1, 200, 9, 13, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 6, '����', 1, 200, 9, 14, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 7, '����', 1, 200, 9, 15, 0, 'WereWolf Cyclops Stalker Zombie');
INSERT INTO "map_item_events" VALUES ('middleland', 8, '����Ʈ', 1, 50, 9, 8, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 10, '����Ʈ', 1, 50, 9, 9, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 11, '����Ʈ', 1, 50, 9, 10, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 12, '����Ʈ', 1, 50, 9, 11, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 13, '����Ʈ', 1, 50, 9, 12, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 14, '����Ʈ', 1, 50, 9, 13, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 15, '����Ʈ', 1, 50, 9, 14, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 16, '����Ʈ', 1, 50, 9, 15, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 17, '��', 1, 20, 9, 8, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 18, '��', 1, 20, 9, 9, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 19, '��', 1, 20, 9, 10, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 20, '��', 1, 20, 9, 11, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 21, '��', 1, 20, 9, 12, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 22, '��', 1, 20, 9, 13, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 23, '��', 1, 20, 9, 14, 0, 'WereWolf Cyclops Stalker Orge');
INSERT INTO "map_item_events" VALUES ('middleland', 24, '��', 1, 20, 9, 15, 0, 'WereWolf Cyclops Stalker Orge');

-- ----------------------------
-- Table structure for map_mineral_points
-- ----------------------------
DROP TABLE IF EXISTS "map_mineral_points";
CREATE TABLE "map_mineral_points" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_mineral_points
-- ----------------------------
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 0, 24, 132);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 1, 25, 131);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 2, 26, 130);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 3, 21, 118);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 4, 24, 116);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 5, 26, 114);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 6, 136, 21);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 7, 139, 21);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 8, 142, 23);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 9, 177, 50);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 10, 179, 52);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 11, 173, 48);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 12, 28, 44);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 13, 27, 43);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 14, 27, 42);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 15, 70, 63);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 16, 71, 64);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 17, 71, 65);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 18, 31, 87);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 19, 30, 88);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 20, 30, 89);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 21, 111, 75);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 22, 112, 76);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 23, 113, 76);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 24, 146, 174);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 25, 147, 175);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 26, 148, 176);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 27, 175, 161);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 28, 176, 162);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 29, 176, 163);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 30, 62, 174);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 31, 63, 174);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 32, 65, 173);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 33, 127, 111);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 34, 128, 110);
INSERT INTO "map_mineral_points" VALUES ('aresdend1', 35, 129, 110);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 0, 24, 52);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 1, 22, 53);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 2, 27, 54);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 3, 127, 47);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 4, 129, 46);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 5, 131, 48);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 6, 472, 419);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 7, 469, 417);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 8, 467, 416);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 9, 479, 220);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 10, 477, 218);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 11, 473, 216);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 12, 289, 83);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 13, 291, 82);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 14, 295, 79);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 15, 106, 265);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 16, 108, 264);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 17, 103, 267);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 18, 101, 450);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 19, 44, 452);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 20, 47, 451);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 21, 43, 455);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 22, 130, 153);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 23, 133, 151);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 24, 136, 149);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 25, 350, 338);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 26, 351, 339);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 27, 352, 340);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 29, 371, 456);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 30, 370, 455);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 31, 369, 454);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 32, 176, 394);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 33, 175, 395);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 34, 174, 396);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 35, 404, 34);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 36, 403, 35);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 37, 402, 36);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 38, 453, 54);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 39, 454, 55);
INSERT INTO "map_mineral_points" VALUES ('dglv2', 40, 455, 56);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 0, 25, 131);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 1, 28, 129);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 2, 24, 132);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 3, 21, 118);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 4, 24, 116);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 5, 26, 114);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 6, 178, 51);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 7, 175, 49);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 8, 173, 48);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 9, 179, 116);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 10, 171, 46);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 11, 169, 45);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 12, 53, 32);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 13, 56, 31);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 14, 57, 30);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 15, 26, 55);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 16, 27, 54);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 17, 28, 53);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 18, 82, 84);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 19, 84, 84);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 20, 86, 82);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 21, 165, 166);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 22, 166, 165);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 23, 167, 164);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 24, 87, 154);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 25, 88, 153);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 26, 89, 152);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 27, 119, 160);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 28, 118, 161);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 29, 121, 161);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 30, 124, 113);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 31, 127, 111);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 32, 128, 110);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 33, 57, 142);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 34, 61, 145);
INSERT INTO "map_mineral_points" VALUES ('elvined1', 35, 63, 146);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 0, 129, 146);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 1, 158, 42);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 2, 130, 145);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 3, 159, 47);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 4, 135, 144);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 5, 21, 107);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 6, 36, 155);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 7, 21, 106);
INSERT INTO "map_mineral_points" VALUES ('middled1n', 8, 37, 155);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 0, 34, 32);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 1, 35, 32);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 2, 37, 30);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 3, 40, 31);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 4, 27, 30);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 5, 29, 31);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 6, 28, 33);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 7, 27, 26);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 8, 28, 27);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 9, 29, 28);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 10, 162, 158);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 11, 160, 159);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 12, 164, 158);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 13, 165, 157);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 14, 167, 156);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 15, 169, 157);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 16, 170, 158);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 18, 171, 158);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 19, 172, 159);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 20, 173, 160);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 21, 142, 92);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 22, 141, 93);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 23, 142, 91);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 24, 141, 88);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 25, 139, 88);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 26, 166, 100);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 27, 171, 102);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 28, 172, 103);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 29, 173, 104);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 30, 170, 99);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 31, 170, 96);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 32, 170, 97);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 33, 173, 107);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 34, 173, 109);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 35, 175, 113);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 36, 176, 114);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 37, 168, 129);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 38, 169, 129);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 39, 170, 130);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 40, 171, 131);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 41, 172, 131);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 42, 173, 132);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 43, 173, 133);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 44, 171, 134);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 45, 167, 125);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 46, 161, 138);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 47, 170, 139);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 48, 169, 139);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 49, 161, 52);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 50, 162, 53);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 51, 163, 54);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 52, 164, 54);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 53, 164, 57);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 54, 165, 58);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 55, 105, 36);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 56, 106, 37);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 57, 107, 37);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 58, 108, 36);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 59, 112, 35);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 60, 101, 35);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 61, 99, 34);
INSERT INTO "map_mineral_points" VALUES ('middled1x', 62, 98, 35);

-- ----------------------------
-- Table structure for map_no_attack_areas
-- ----------------------------
DROP TABLE IF EXISTS "map_no_attack_areas";
CREATE TABLE "map_no_attack_areas" (
  "map_name" TEXT NOT NULL,
  "area_index" INTEGER NOT NULL,
  "tile_x" INTEGER NOT NULL,
  "tile_y" INTEGER NOT NULL,
  "tile_w" INTEGER NOT NULL,
  "tile_h" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "area_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (area_index >= 0 AND area_index < 50)
);

-- ----------------------------
-- Records of map_no_attack_areas
-- ----------------------------
INSERT INTO "map_no_attack_areas" VALUES ('2ndmiddle', 0, 115, 18, 20, 7);
INSERT INTO "map_no_attack_areas" VALUES ('2ndmiddle', 1, 130, 224, 20, 9);
INSERT INTO "map_no_attack_areas" VALUES ('abaddon', 0, 164, 166, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('arefarm', 0, 92, 49, 6, 6);
INSERT INTO "map_no_attack_areas" VALUES ('arejail', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 0, 22, 21, 13, 4);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 1, 252, 21, 14, 3);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 2, 136, 46, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 3, 64, 122, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 4, 166, 142, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 5, 112, 242, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('aresden', 6, 136, 201, 8, 9);
INSERT INTO "map_no_attack_areas" VALUES ('arewrhus', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('bisle', 0, 139, 112, 13, 11);
INSERT INTO "map_no_attack_areas" VALUES ('bsmith_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('bsmith_1f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('bsmith_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('bsmith_2f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cath_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cath_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cityhall_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cityhall_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cmdhall_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('cmdhall_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('default', 0, 131, 35, 7, 5);
INSERT INTO "map_no_attack_areas" VALUES ('dglv2', 0, 26, 32, 10, 10);
INSERT INTO "map_no_attack_areas" VALUES ('dglv2', 1, 460, 453, 10, 10);
INSERT INTO "map_no_attack_areas" VALUES ('dglv2', 2, 206, 255, 7, 6);
INSERT INTO "map_no_attack_areas" VALUES ('dglv2', 3, 261, 254, 5, 6);
INSERT INTO "map_no_attack_areas" VALUES ('dglv3', 0, 183, 207, 12, 11);
INSERT INTO "map_no_attack_areas" VALUES ('druncncity', 0, 104, 125, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('elvfarm', 0, 103, 66, 6, 6);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 0, 21, 272, 12, 5);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 1, 248, 269, 12, 4);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 2, 154, 54, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 3, 106, 86, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 4, 166, 142, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 5, 238, 126, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('elvine', 6, 154, 246, 9, 7);
INSERT INTO "map_no_attack_areas" VALUES ('elvjail', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('elvwrhus', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gldhall_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gldhall_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gshop_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gshop_1f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gshop_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('gshop_2f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('icebound', 0, 258, 259, 14, 4);
INSERT INTO "map_no_attack_areas" VALUES ('inferniaa', 0, 30, 24, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('inferniab', 0, 30, 24, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('maze', 0, 98, 26, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('middled1n', 0, 24, 29, 19, 16);
INSERT INTO "map_no_attack_areas" VALUES ('middled1n', 1, 174, 116, 20, 20);
INSERT INTO "map_no_attack_areas" VALUES ('middled1x', 0, 64, 75, 15, 38);
INSERT INTO "map_no_attack_areas" VALUES ('middleland', 0, 346, 499, 9, 4);
INSERT INTO "map_no_attack_areas" VALUES ('middleland', 1, 99, 20, 8, 5);
INSERT INTO "map_no_attack_areas" VALUES ('middleland', 2, 309, 20, 8, 5);
INSERT INTO "map_no_attack_areas" VALUES ('middleland', 3, 148, 499, 8, 4);
INSERT INTO "map_no_attack_areas" VALUES ('procella', 0, 30, 24, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('resurr1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('resurr2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('toh1', 0, 141, 27, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh1', 1, 31, 216, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh1', 2, 213, 211, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh2', 0, 35, 34, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh2', 1, 268, 24, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh2', 2, 97, 266, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh2', 3, 247, 238, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh3', 0, 89, 36, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('toh3', 1, 143, 28, 8, 8);
INSERT INTO "map_no_attack_areas" VALUES ('wrhus_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('wrhus_1f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('wrhus_2', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('wrhus_2f', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('wzdtwr_1', 0, -10, -10, 0, 0);
INSERT INTO "map_no_attack_areas" VALUES ('wzdtwr_2', 0, -10, -10, 0, 0);

-- ----------------------------
-- Table structure for map_npc_avoid_rects
-- ----------------------------
DROP TABLE IF EXISTS "map_npc_avoid_rects";
CREATE TABLE "map_npc_avoid_rects" (
  "map_name" TEXT NOT NULL,
  "rect_index" INTEGER NOT NULL,
  "tile_x" INTEGER NOT NULL,
  "tile_y" INTEGER NOT NULL,
  "tile_w" INTEGER NOT NULL,
  "tile_h" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "rect_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (rect_index >= 0 AND rect_index < 50)
);

-- ----------------------------
-- Records of map_npc_avoid_rects
-- ----------------------------
INSERT INTO "map_npc_avoid_rects" VALUES ('aresden', 0, 76, 70, 133, 149);
INSERT INTO "map_npc_avoid_rects" VALUES ('elvine', 0, 86, 67, 128, 146);

-- ----------------------------
-- Table structure for map_npcs
-- ----------------------------
DROP TABLE IF EXISTS "map_npcs";
CREATE TABLE "map_npcs" (
  "id" INTEGER PRIMARY KEY AUTOINCREMENT,
  "map_name" TEXT NOT NULL,
  "npc_config_id" INTEGER NOT NULL,
  "move_type" INTEGER NOT NULL,
  "waypoint_list" TEXT NOT NULL DEFAULT '',
  "name_prefix" TEXT NOT NULL DEFAULT '',
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_npcs
-- ----------------------------
INSERT INTO "map_npcs" VALUES (1, 'arewrhus', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (2, 'bsmith_1', 62, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (3, 'bsmith_1f', 62, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (4, 'bsmith_2', 62, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (5, 'bsmith_2f', 62, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (6, 'cityhall_1', 63, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (7, 'cityhall_2', 63, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (8, 'cmdhall_1', 65, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (9, 'cmdhall_2', 65, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (10, 'elvwrhus', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (11, 'gldhall_1', 64, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (12, 'gldhall_2', 64, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (13, 'gshop_1', 56, 2, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (14, 'gshop_1f', 56, 2, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (15, 'gshop_2', 56, 2, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (16, 'gshop_2f', 56, 2, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (17, 'wrhus_1', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (18, 'wrhus_1f', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (19, 'wrhus_2', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (20, 'wrhus_2f', 58, 1, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (21, 'wzdtwr_1', 57, 2, '1,2,3,4,5,6,7,8,9,10', '');
INSERT INTO "map_npcs" VALUES (22, 'wzdtwr_2', 57, 2, '1,2,3,4,5,6,7,8,9,10', '');

-- ----------------------------
-- Table structure for map_spot_mob_generators
-- ----------------------------
DROP TABLE IF EXISTS "map_spot_mob_generators";
CREATE TABLE "map_spot_mob_generators" (
  "map_name" TEXT NOT NULL,
  "generator_index" INTEGER NOT NULL,
  "generator_type" INTEGER NOT NULL,
  "tile_x" INTEGER NOT NULL DEFAULT 0,
  "tile_y" INTEGER NOT NULL DEFAULT 0,
  "tile_w" INTEGER NOT NULL DEFAULT 0,
  "tile_h" INTEGER NOT NULL DEFAULT 0,
  "waypoints" TEXT NOT NULL DEFAULT '',
  "npc_config_id" INTEGER NOT NULL,
  "max_mobs" INTEGER NOT NULL,
  "prob_sa" INTEGER NOT NULL DEFAULT 15,
  "kind_sa" INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY ("map_name", "generator_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (generator_index >= 0 AND generator_index < 100),
   (generator_type IN (1, 2))
);

-- ----------------------------
-- Records of map_spot_mob_generators
-- ----------------------------
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 0, 1, 112, 110, 27, 27, '', 18, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 1, 1, 112, 110, 27, 27, '', 17, 10, 25, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 2, 1, 38, 157, 35, 16, '', 9, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 3, 1, 45, 85, 21, 15, '', 9, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 4, 1, 81, 198, 19, 12, '', 10, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 5, 1, 152, 51, 15, 16, '', 10, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 6, 1, 47, 38, 16, 19, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 7, 1, 167, 179, 18, 21, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 8, 1, 192, 107, 25, 15, '', 12, 10, 20, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 9, 1, 192, 144, 26, 18, '', 13, 10, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 10, 1, 109, 63, 32, 20, '', 8, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('2ndmiddle', 11, 1, 126, 193, 24, 17, '', 8, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 0, 1, 74, 23, 64, 29, '', 38, 20, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 1, 1, 30, 115, 56, 31, '', 38, 20, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 2, 1, 141, 68, 32, 50, '', 38, 20, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 3, 1, 23, 71, 45, 46, '', 5, 20, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 4, 1, 120, 43, 54, 30, '', 5, 20, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 5, 1, 127, 113, 51, 32, '', 5, 20, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 6, 1, 34, 45, 60, 30, '', 34, 20, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 7, 1, 75, 134, 58, 36, '', 39, 20, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 8, 1, 71, 64, 72, 48, '', 33, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 9, 1, 71, 64, 72, 48, '', 47, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('abaddon', 10, 1, 71, 64, 72, 48, '', 27, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk11', 0, 1, 57, 32, 53, 78, '', 6, 60, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk11', 1, 1, 57, 32, 53, 78, '', 6, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk12', 0, 1, 57, 32, 53, 78, '', 6, 60, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk12', 1, 1, 57, 32, 53, 78, '', 6, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk21', 0, 1, 57, 32, 53, 78, '', 6, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk21', 1, 1, 57, 32, 53, 78, '', 7, 50, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk22', 0, 1, 57, 32, 53, 78, '', 6, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arebrk22', 1, 1, 57, 32, 53, 78, '', 7, 50, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 0, 1, 116, 80, 22, 22, '', 0, 15, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 1, 1, 193, 102, 24, 17, '', 0, 15, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 2, 1, 68, 22, 127, 49, '', 0, 15, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 3, 1, 65, 173, 19, 41, '', 9, 15, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 4, 1, 137, 203, 26, 12, '', 9, 15, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 5, 1, 146, 88, 36, 28, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 6, 1, 84, 149, 24, 26, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 7, 1, 146, 26, 38, 20, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 8, 1, 206, 56, 12, 20, '', 4, 10, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 9, 1, 173, 199, 20, 18, '', 4, 10, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('arefarm', 10, 1, 185, 80, 35, 35, '', 13, 10, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 0, 1, 106, 43, 17, 12, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 1, 1, 33, 167, 27, 16, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 2, 1, 25, 187, 23, 16, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 3, 1, 132, 235, 16, 17, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 4, 1, 243, 191, 21, 17, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 5, 1, 176, 233, 20, 17, '', 4, 20, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 6, 1, 199, 225, 30, 24, '', 4, 20, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 7, 1, 88, 31, 29, 15, '', 9, 20, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 8, 1, 135, 44, 11, 11, '', 59, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 9, 1, 63, 121, 11, 10, '', 59, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 10, 1, 165, 140, 11, 11, '', 59, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 11, 1, 135, 201, 10, 9, '', 59, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 12, 1, 111, 241, 10, 9, '', 59, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 13, 1, 142, 127, 24, 17, '', 53, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 14, 1, 116, 166, 21, 11, '', 54, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 15, 1, 96, 181, 21, 12, '', 55, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('aresden', 16, 1, 150, 195, 22, 11, '', 53, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 0, 1, 81, 72, 1, 1, '', 61, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 1, 1, 129, 74, 1, 1, '', 61, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 2, 1, 100, 80, 9, 7, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 3, 1, 105, 119, 14, 8, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 4, 1, 118, 114, 12, 13, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 5, 1, 86, 89, 11, 7, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 6, 1, 61, 86, 10, 7, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('default', 7, 1, 68, 94, 8, 8, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 0, 1, 348, 329, 48, 28, '', 18, 5, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 1, 1, 159, 21, 41, 34, '', 18, 5, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 2, 1, 82, 361, 50, 46, '', 18, 5, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 3, 1, 162, 237, 42, 79, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 4, 1, 255, 181, 40, 44, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 5, 1, 391, 223, 57, 18, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 6, 1, 427, 437, 35, 27, '', 13, 5, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 7, 1, 37, 30, 43, 47, '', 13, 5, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 8, 1, 21, 270, 66, 23, '', 13, 5, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 9, 1, 323, 32, 34, 40, '', 13, 5, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 10, 1, 27, 322, 79, 99, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 11, 1, 257, 311, 65, 54, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 12, 1, 323, 79, 117, 140, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 13, 1, 209, 230, 59, 45, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 14, 1, 177, 99, 41, 37, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 15, 1, 33, 178, 35, 23, '', 10, 8, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 16, 1, 222, 75, 47, 36, '', 10, 8, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 17, 1, 144, 384, 18, 40, '', 10, 8, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 18, 1, 415, 321, 12, 61, '', 10, 8, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 19, 1, 33, 187, 20, 48, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 20, 1, 190, 450, 42, 25, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 21, 1, 86, 418, 52, 39, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 22, 1, 412, 262, 61, 20, '', 5, 5, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 23, 1, 228, 226, 20, 50, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 24, 1, 26, 445, 106, 24, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 25, 1, 229, 130, 47, 54, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 26, 1, 84, 199, 127, 23, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 27, 1, 291, 282, 48, 39, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 28, 1, 210, 20, 72, 22, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 29, 1, 369, 375, 101, 48, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 30, 1, 114, 138, 39, 42, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 31, 1, 162, 416, 77, 34, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 32, 1, 359, 116, 56, 51, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 33, 1, 221, 307, 71, 28, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 34, 1, 38, 60, 113, 68, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv2', 35, 1, 85, 257, 36, 15, '', 5, 8, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv4', 0, 1, 180, 180, 40, 40, '', 10, 50, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv4', 1, 1, 70, 70, 240, 240, '', 36, 50, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('dglv4', 2, 1, 170, 170, 60, 60, '', 36, 5, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('druncncity', 0, 1, 27, 55, 27, 53, '', 5, 50, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('druncncity', 1, 1, 140, 28, 27, 45, '', 5, 50, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('druncncity', 2, 1, 29, 24, 25, 24, '', 5, 50, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('druncncity', 3, 1, 152, 138, 20, 25, '', 5, 50, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk11', 0, 1, 57, 32, 53, 78, '', 6, 60, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk11', 1, 1, 57, 32, 53, 78, '', 6, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk12', 0, 1, 57, 32, 53, 78, '', 6, 60, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk12', 1, 1, 57, 32, 53, 78, '', 6, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk21', 0, 1, 57, 32, 53, 78, '', 6, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk21', 1, 1, 57, 32, 53, 78, '', 7, 50, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk22', 0, 1, 57, 32, 53, 78, '', 6, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvbrk22', 1, 1, 57, 32, 53, 78, '', 7, 50, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 0, 1, 130, 115, 16, 30, '', 0, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 1, 1, 124, 39, 38, 66, '', 0, 30, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 2, 1, 124, 201, 29, 13, '', 9, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 3, 1, 164, 195, 27, 14, '', 9, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 4, 1, 198, 90, 18, 21, '', 9, 10, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 5, 1, 43, 158, 26, 20, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 6, 1, 165, 43, 20, 33, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 7, 1, 111, 81, 20, 20, '', 3, 15, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 8, 1, 211, 194, 16, 33, '', 4, 15, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvfarm', 9, 1, 71, 34, 26, 19, '', 4, 15, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 0, 1, 147, 30, 19, 12, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 1, 1, 126, 236, 16, 23, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 2, 1, 36, 56, 23, 15, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 3, 1, 252, 174, 17, 17, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 4, 1, 227, 210, 30, 13, '', 0, 20, 5, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 5, 1, 34, 176, 27, 20, '', 4, 20, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 6, 1, 42, 205, 24, 19, '', 4, 20, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 7, 1, 120, 27, 19, 16, '', 9, 20, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 8, 1, 153, 52, 11, 11, '', 60, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 9, 1, 105, 84, 11, 11, '', 60, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 10, 1, 165, 140, 11, 11, '', 60, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 11, 1, 237, 124, 11, 11, '', 60, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 12, 1, 153, 244, 10, 11, '', 60, 2, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 13, 1, 142, 131, 33, 18, '', 53, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 14, 1, 216, 148, 24, 15, '', 54, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 15, 1, 192, 121, 21, 12, '', 55, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('elvine', 16, 1, 222, 105, 22, 12, '', 53, 1, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 0, 1, 28, 23, 53, 36, '', 18, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 1, 1, 51, 85, 54, 51, '', 28, 10, 30, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 2, 1, 225, 63, 44, 46, '', 28, 10, 30, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 3, 1, 159, 79, 38, 92, '', 28, 10, 30, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 4, 1, 137, 113, 61, 51, '', 31, 10, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 5, 1, 179, 67, 58, 36, '', 31, 10, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 6, 1, 45, 202, 38, 31, '', 31, 10, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 7, 1, 215, 137, 47, 29, '', 10, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 8, 1, 108, 240, 57, 21, '', 10, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 9, 1, 35, 165, 36, 69, '', 10, 10, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 10, 1, 174, 26, 62, 57, '', 20, 10, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 11, 1, 102, 219, 62, 58, '', 20, 10, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 12, 1, 179, 217, 47, 43, '', 1, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 13, 1, 89, 148, 38, 28, '', 1, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 14, 1, 198, 170, 45, 36, '', 13, 10, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 15, 1, 50, 72, 36, 65, '', 13, 10, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('icebound', 16, 1, 94, 54, 58, 44, '', 13, 10, 25, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 0, 1, 22, 21, 42, 23, '', 8, 50, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 1, 1, 74, 23, 51, 29, '', 18, 80, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 2, 1, 31, 42, 71, 36, '', 47, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 3, 1, 24, 82, 49, 41, '', 47, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 4, 1, 58, 60, 71, 31, '', 33, 100, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 5, 1, 100, 57, 26, 34, '', 36, 60, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniaa', 6, 1, 93, 99, 34, 24, '', 39, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 0, 1, 22, 20, 27, 40, '', 8, 50, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 1, 1, 69, 38, 46, 35, '', 18, 80, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 2, 1, 40, 20, 88, 26, '', 47, 40, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 3, 1, 24, 92, 62, 34, '', 28, 40, 30, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 4, 1, 22, 48, 62, 40, '', 33, 100, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 5, 1, 86, 54, 39, 48, '', 36, 60, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('inferniab', 6, 1, 36, 101, 38, 18, '', 39, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('maze', 0, 1, 30, 39, 139, 53, '', 1, 20, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('maze', 1, 1, 75, 94, 56, 38, '', 26, 20, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('maze', 2, 1, 24, 147, 44, 29, '', 27, 20, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 0, 1, 53, 380, 48, 29, '', 22, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 1, 1, 419, 315, 34, 38, '', 22, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 2, 1, 34, 69, 66, 14, '', 22, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 3, 1, 397, 194, 73, 41, '', 22, 10, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 5, 1, 92, 451, 30, 27, '', 8, 20, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 6, 1, 317, 38, 22, 32, '', 8, 20, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 7, 1, 360, 391, 51, 23, '', 9, 1, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 8, 1, 79, 114, 332, 300, '', 9, 3, 15, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 9, 1, 268, 301, 24, 22, '', 4, 5, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 10, 1, 214, 182, 16, 14, '', 4, 5, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 11, 1, 213, 181, 16, 14, '', 4, 5, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 12, 1, 169, 373, 34, 20, '', 10, 20, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 13, 1, 49, 159, 38, 19, '', 10, 20, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 14, 1, 205, 93, 44, 34, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 15, 1, 287, 286, 35, 17, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 16, 1, 437, 374, 26, 19, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 17, 1, 424, 152, 21, 14, '', 5, 10, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 18, 1, 284, 216, 16, 12, '', 3, 20, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 19, 1, 181, 266, 18, 8, '', 3, 20, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 20, 1, 80, 285, 13, 6, '', 3, 20, 10, 2);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 21, 1, 285, 242, 108, 210, '', 23, 15, 25, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 22, 1, 354, 108, 54, 33, '', 30, 10, 25, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 23, 1, 34, 350, 101, 60, '', 18, 18, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 24, 1, 269, 156, 34, 52, '', 18, 18, 35, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 25, 1, 1, 1, 499, 499, '', 5, 1, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 26, 1, 1, 1, 499, 499, '', 5, 1, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 27, 1, 255, 92, 51, 40, '', 23, 10, 25, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 28, 1, 266, 420, 53, 41, '', 30, 15, 25, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 29, 1, 107, 202, 311, 102, '', 29, 40, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 30, 1, 1, 1, 499, 499, '', 38, 1, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 31, 1, 420, 293, 15, 27, '', 36, 3, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('middleland', 32, 1, 143, 208, 82, 40, '', 35, 6, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 0, 1, 24, 20, 197, 28, '', 29, 60, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 1, 1, 30, 202, 198, 24, '', 29, 40, 20, 3);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 2, 1, 44, 82, 63, 42, '', 48, 60, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 3, 1, 109, 169, 66, 36, '', 48, 80, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 4, 1, 179, 29, 33, 80, '', 25, 80, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 5, 1, 43, 126, 56, 83, '', 25, 80, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 6, 1, 149, 115, 63, 46, '', 21, 60, 20, 5);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 7, 1, 29, 49, 94, 144, '', 26, 60, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 8, 1, 133, 49, 88, 141, '', 27, 60, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('procella', 9, 1, 173, 76, 4, 89, '', 40, 80, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 0, 1, 82, 195, 12, 10, '', 5, 1, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 1, 1, 82, 195, 12, 10, '', 20, 2, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 2, 1, 82, 195, 12, 10, '', 1, 3, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 3, 1, 82, 195, 12, 10, '', 31, 8, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 4, 1, 35, 265, 10, 10, '', 5, 1, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 5, 1, 35, 265, 10, 10, '', 20, 6, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 6, 1, 35, 265, 10, 10, '', 1, 6, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 7, 1, 35, 265, 10, 10, '', 31, 1, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 8, 1, 225, 248, 10, 9, '', 5, 1, 15, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 9, 1, 225, 248, 10, 9, '', 20, 8, 20, 8);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 10, 1, 225, 248, 10, 9, '', 1, 4, 20, 1);
INSERT INTO "map_spot_mob_generators" VALUES ('toh3', 11, 1, 225, 248, 10, 9, '', 31, 1, 20, 8);

-- ----------------------------
-- Table structure for map_strategic_points
-- ----------------------------
DROP TABLE IF EXISTS "map_strategic_points";
CREATE TABLE "map_strategic_points" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "side" INTEGER NOT NULL,
  "point_value" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_strategic_points
-- ----------------------------
INSERT INTO "map_strategic_points" VALUES ('middleland', 0, 0, 2, 176, 224);
INSERT INTO "map_strategic_points" VALUES ('middleland', 1, 0, 1, 456, 213);
INSERT INTO "map_strategic_points" VALUES ('middleland', 2, 0, 1, 275, 286);

-- ----------------------------
-- Table structure for map_strike_points
-- ----------------------------
DROP TABLE IF EXISTS "map_strike_points";
CREATE TABLE "map_strike_points" (
  "map_name" TEXT NOT NULL,
  "point_index" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  "hp" INTEGER NOT NULL,
  "effect_x1" INTEGER NOT NULL,
  "effect_y1" INTEGER NOT NULL,
  "effect_x2" INTEGER NOT NULL,
  "effect_y2" INTEGER NOT NULL,
  "effect_x3" INTEGER NOT NULL,
  "effect_y3" INTEGER NOT NULL,
  "effect_x4" INTEGER NOT NULL,
  "effect_y4" INTEGER NOT NULL,
  "effect_x5" INTEGER NOT NULL,
  "effect_y5" INTEGER NOT NULL,
  "related_map_name" TEXT NOT NULL,
  PRIMARY KEY ("map_name", "point_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (length(related_map_name) <= 10)
);

-- ----------------------------
-- Records of map_strike_points
-- ----------------------------
INSERT INTO "map_strike_points" VALUES ('aresden', 0, 147, 124, 13, 139, 106, 130, 110, 139, 112, 147, 114, 131, 120, 'cityhall_1');
INSERT INTO "map_strike_points" VALUES ('aresden', 1, 130, 164, 13, 127, 154, 123, 156, 132, 159, 125, 161, 120, 162, 'gshop_1');
INSERT INTO "map_strike_points" VALUES ('aresden', 2, 166, 198, 13, 162, 188, 167, 189, 157, 191, 161, 194, 155, 197, 'bsmith_1');
INSERT INTO "map_strike_points" VALUES ('aresden', 3, 107, 183, 13, 102, 168, 112, 173, 101, 174, 108, 175, 101, 179, 'wrhus_1');
INSERT INTO "map_strike_points" VALUES ('elvine', 0, 148, 128, 13, 140, 110, 131, 114, 140, 116, 148, 118, 132, 124, 'cityhall_2');
INSERT INTO "map_strike_points" VALUES ('elvine', 1, 229, 150, 13, 226, 140, 222, 142, 231, 145, 224, 147, 219, 148, 'gshop_2');
INSERT INTO "map_strike_points" VALUES ('elvine', 2, 239, 110, 13, 235, 100, 240, 101, 230, 103, 234, 106, 228, 109, 'bsmith_2');
INSERT INTO "map_strike_points" VALUES ('elvine', 3, 204, 127, 13, 199, 112, 209, 117, 198, 118, 199, 119, 198, 123, 'wrhus_2');

-- ----------------------------
-- Table structure for map_teleport_locations
-- ----------------------------
DROP TABLE IF EXISTS "map_teleport_locations";
CREATE TABLE "map_teleport_locations" (
  "map_name" TEXT NOT NULL,
  "teleport_index" INTEGER NOT NULL,
  "src_x" INTEGER NOT NULL,
  "src_y" INTEGER NOT NULL,
  "dest_map_name" TEXT NOT NULL,
  "dest_x" INTEGER NOT NULL,
  "dest_y" INTEGER NOT NULL,
  "direction" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "teleport_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION,
   (length(dest_map_name) <= 10),
   (direction >= 0 AND direction <= 8)
);

-- ----------------------------
-- Records of map_teleport_locations
-- ----------------------------
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 0, 135, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 1, 136, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 2, 137, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 3, 138, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 4, 139, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 5, 140, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 6, 141, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 7, 142, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 8, 143, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 9, 144, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 10, 145, 228, 'arefarm', 120, 25, 5);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 11, 121, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 12, 122, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 13, 123, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 14, 124, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 15, 125, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 16, 126, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 17, 127, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 18, 128, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 19, 129, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 20, 130, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('2ndmiddle', 21, 131, 21, 'elvfarm', 160, 227, 1);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 0, 26, 41, 'aresden', 253, 159, 5);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 1, 27, 41, 'aresden', 253, 159, 5);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 2, 26, 42, 'aresden', 253, 159, 5);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 3, 104, 36, 'arebrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 4, 105, 36, 'arebrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 5, 105, 37, 'arebrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 6, 67, 70, 'arebrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 7, 68, 70, 'arebrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk11', 8, 68, 71, 'arebrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk12', 0, 32, 33, 'arebrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk12', 1, 33, 33, 'arebrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk12', 2, 32, 34, 'arebrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 0, 67, 69, 'arebrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 1, 67, 70, 'arebrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 2, 68, 70, 'arebrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 3, 104, 36, 'arebrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 4, 105, 36, 'arebrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk21', 5, 105, 37, 'arebrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('arebrk22', 0, 38, 29, 'arebrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk22', 1, 39, 29, 'arebrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arebrk22', 2, 38, 30, 'arebrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 0, 20, 22, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 1, 20, 23, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 2, 20, 24, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 3, 20, 25, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 4, 20, 26, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 5, 20, 27, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 6, 20, 28, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 7, 20, 29, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 8, 20, 30, 'aresden', 275, 205, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 9, 44, 101, 'aresden', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 10, 44, 102, 'aresden', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 11, 45, 101, 'aresden', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 12, 45, 102, 'aresden', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 13, 73, 87, 'bsmith_1f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 14, 74, 87, 'bsmith_1f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 15, 75, 87, 'bsmith_1f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 16, 75, 86, 'bsmith_1f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 17, 63, 92, 'bsmith_1f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 18, 63, 93, 'bsmith_1f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 19, 64, 93, 'bsmith_1f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 20, 34, 88, 'wrhus_1f', 59, 36, 6);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 21, 35, 89, 'wrhus_1f', 59, 36, 6);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 22, 36, 90, 'wrhus_1f', 59, 36, 6);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 23, 40, 90, 'wrhus_1f', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 24, 59, 69, 'gshop_1f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 25, 59, 70, 'gshop_1f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 26, 60, 70, 'gshop_1f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 27, 63, 70, 'gshop_1f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 28, 64, 69, 'gshop_1f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 29, 114, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 30, 115, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 31, 116, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 32, 117, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 33, 118, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 34, 119, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 35, 120, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 36, 121, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 37, 122, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 38, 123, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 39, 124, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 40, 125, 23, '2ndmiddle', 140, 225, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 41, 53, 133, 'arebrk11', 28, 43, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 42, 54, 133, 'arebrk11', 28, 43, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 43, 55, 133, 'arebrk11', 28, 43, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 44, 55, 132, 'arebrk11', 28, 43, 5);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 45, 78, 71, 'middled1n', 181, 124, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 46, 79, 69, 'middled1n', 181, 124, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 47, 78, 70, 'middled1n', 181, 124, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 48, 80, 69, 'middled1n', 181, 124, 1);
INSERT INTO "map_teleport_locations" VALUES ('arefarm', 49, 79, 70, 'middled1n', 181, 124, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 0, 135, 129, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 1, 136, 129, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 2, 137, 129, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 3, 137, 128, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 4, 145, 122, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 5, 144, 123, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 6, 145, 123, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 7, 146, 123, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 8, 146, 122, 'cityhall_1', 55, 44, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 9, 185, 93, 'cath_1', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 10, 186, 93, 'cath_1', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 11, 186, 92, 'cath_1', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 12, 187, 92, 'cath_1', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 13, 167, 195, 'bsmith_1', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 14, 168, 195, 'bsmith_1', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 15, 169, 195, 'bsmith_1', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 16, 169, 194, 'bsmith_1', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 17, 157, 200, 'bsmith_1', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 18, 157, 201, 'bsmith_1', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 19, 158, 201, 'bsmith_1', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 20, 101, 183, 'wrhus_1', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 21, 102, 184, 'wrhus_1', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 22, 103, 185, 'wrhus_1', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 23, 107, 185, 'wrhus_1', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 24, 217, 132, 'arewrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 25, 218, 133, 'arewrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 26, 219, 134, 'arewrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 27, 223, 134, 'arewrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 28, 112, 97, 'gldhall_1', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 29, 113, 96, 'gldhall_1', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 30, 113, 97, 'gldhall_1', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 31, 114, 97, 'gldhall_1', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 32, 55, 118, 'wzdtwr_1', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 33, 56, 118, 'wzdtwr_1', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 34, 57, 117, 'wzdtwr_1', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 35, 126, 166, 'gshop_1', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 36, 126, 167, 'gshop_1', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 37, 127, 167, 'gshop_1', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 38, 130, 167, 'gshop_1', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 39, 131, 166, 'gshop_1', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 40, 27, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 41, 28, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 42, 29, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 43, 30, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 44, 31, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 45, 32, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 46, 33, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 47, 34, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 48, 35, 20, 'middleland', 152, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 49, 258, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 50, 259, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 51, 260, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 52, 261, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 53, 262, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 54, 263, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 55, 264, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 56, 265, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 57, 266, 20, 'middleland', 353, 500, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 58, 26, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 59, 27, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 60, 28, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 61, 29, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 62, 30, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 63, 31, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 64, 32, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 65, 33, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 66, 34, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 67, 35, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 68, 36, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 69, 37, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 70, 38, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 71, 39, 279, 'huntzone2', 68, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 72, 78, 210, 'aresdend1', 96, 39, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 73, 78, 211, 'aresdend1', 96, 39, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 74, 79, 209, 'aresdend1', 96, 39, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 75, 79, 210, 'aresdend1', 96, 39, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 76, 80, 209, 'aresdend1', 96, 39, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 77, 279, 203, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 78, 279, 204, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 79, 279, 205, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 80, 279, 206, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 81, 279, 207, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 82, 279, 208, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 83, 279, 209, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 84, 279, 210, 'arefarm', 23, 27, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 85, 94, 161, 'cmdhall_1', 51, 50, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 86, 95, 161, 'cmdhall_1', 51, 50, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 87, 95, 160, 'cmdhall_1', 51, 50, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 88, 101, 159, 'cmdhall_1', 40, 52, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 89, 102, 159, 'cmdhall_1', 40, 52, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresden', 90, 103, 159, 'cmdhall_1', 40, 52, 5);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 0, 99, 38, 'aresden', 108, 213, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 1, 100, 38, 'aresden', 108, 213, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 2, 100, 39, 'aresden', 108, 213, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 3, 97, 85, 'huntzone2', 102, 105, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 4, 98, 85, 'huntzone2', 102, 105, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 5, 99, 85, 'huntzone2', 102, 105, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 6, 97, 86, 'huntzone2', 102, 105, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 7, 37, 33, 'dglv2', 464, 460, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 8, 38, 33, 'dglv2', 464, 460, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 9, 39, 33, 'dglv2', 464, 460, 1);
INSERT INTO "map_teleport_locations" VALUES ('aresdend1', 10, 37, 34, 'dglv2', 464, 460, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 0, 78, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 1, 79, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 2, 80, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 3, 81, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 4, 82, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 5, 83, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 6, 84, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 7, 85, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 8, 86, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 9, 87, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 10, 88, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 11, 89, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 12, 90, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 13, 91, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 14, 92, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 15, 93, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 16, 94, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('areuni', 17, 95, 20, 'huntzone2', 116, 176, 1);
INSERT INTO "map_teleport_locations" VALUES ('arewrhus', 0, 54, 33, 'aresden', 222, 136, 4);
INSERT INTO "map_teleport_locations" VALUES ('arewrhus', 1, 53, 34, 'aresden', 222, 136, 4);
INSERT INTO "map_teleport_locations" VALUES ('arewrhus', 2, 54, 34, 'aresden', 222, 136, 4);
INSERT INTO "map_teleport_locations" VALUES ('arewrhus', 3, 55, 34, 'aresden', 222, 136, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 0, 33, 34, 'aresden', 168, 197, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 1, 32, 35, 'aresden', 168, 197, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 2, 33, 35, 'aresden', 168, 197, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 3, 43, 30, 'aresden', 156, 202, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 4, 44, 29, 'aresden', 156, 202, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1', 5, 44, 30, 'aresden', 156, 202, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 0, 33, 34, 'arefarm', 76, 88, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 1, 32, 35, 'arefarm', 76, 88, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 2, 33, 35, 'arefarm', 76, 88, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 3, 43, 30, 'arefarm', 64, 95, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 4, 44, 29, 'arefarm', 64, 95, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_1f', 5, 44, 30, 'arefarm', 64, 95, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 0, 33, 34, 'elvine', 241, 109, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 1, 32, 35, 'elvine', 241, 109, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 2, 33, 35, 'elvine', 241, 109, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 3, 43, 30, 'elvine', 229, 114, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 4, 44, 29, 'elvine', 229, 114, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2', 5, 44, 30, 'elvine', 229, 114, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 0, 33, 34, 'elvfarm', 124, 188, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 1, 32, 35, 'elvfarm', 124, 188, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 2, 33, 35, 'elvfarm', 124, 188, 4);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 3, 43, 30, 'elvfarm', 112, 195, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 4, 44, 29, 'elvfarm', 112, 195, 6);
INSERT INTO "map_teleport_locations" VALUES ('bsmith_2f', 5, 44, 30, 'elvfarm', 112, 195, 6);
INSERT INTO "map_teleport_locations" VALUES ('cath_1', 0, 38, 39, 'aresden', 189, 96, 4);
INSERT INTO "map_teleport_locations" VALUES ('cath_1', 1, 37, 40, 'aresden', 189, 96, 4);
INSERT INTO "map_teleport_locations" VALUES ('cath_1', 2, 37, 39, 'aresden', 189, 96, 4);
INSERT INTO "map_teleport_locations" VALUES ('cath_2', 0, 38, 39, 'elvine', 135, 80, 4);
INSERT INTO "map_teleport_locations" VALUES ('cath_2', 1, 37, 40, 'elvine', 135, 80, 4);
INSERT INTO "map_teleport_locations" VALUES ('cath_2', 2, 37, 39, 'elvine', 135, 80, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_1', 0, 60, 43, 'aresden', 149, 127, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_1', 1, 59, 42, 'aresden', 149, 127, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_1', 2, 58, 42, 'aresden', 149, 127, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_2', 0, 60, 43, 'elvine', 149, 131, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_2', 1, 59, 42, 'elvine', 149, 131, 4);
INSERT INTO "map_teleport_locations" VALUES ('cityhall_2', 2, 58, 42, 'elvine', 149, 131, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 0, 38, 49, 'aresden', 97, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 1, 39, 49, 'aresden', 98, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 2, 39, 50, 'aresden', 98, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 3, 40, 50, 'aresden', 97, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 4, 49, 48, 'aresden', 98, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 5, 50, 48, 'aresden', 97, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 6, 50, 47, 'aresden', 98, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_1', 7, 51, 47, 'aresden', 97, 161, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 0, 38, 49, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 1, 39, 49, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 2, 39, 50, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 3, 40, 50, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 4, 49, 48, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 5, 50, 48, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 6, 50, 47, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('cmdhall_2', 7, 51, 47, 'elvine', 216, 89, 4);
INSERT INTO "map_teleport_locations" VALUES ('default', 0, 127, 78, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 1, 128, 78, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 2, 129, 78, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 3, 127, 79, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 4, 128, 79, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 5, 129, 79, 'elvfarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 6, 80, 75, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 7, 81, 75, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 8, 82, 75, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 9, 80, 76, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 10, 81, 76, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('default', 11, 82, 76, 'arefarm', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 0, 465, 458, 'aresdend1', 39, 35, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 1, 466, 458, 'aresdend1', 39, 35, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 2, 466, 459, 'aresdend1', 39, 35, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 3, 31, 37, 'elvined1', 95, 42, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 4, 32, 37, 'elvined1', 95, 42, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 5, 33, 37, 'elvined1', 95, 42, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 6, 31, 38, 'elvined1', 95, 42, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 7, 211, 286, 'dglv2', 209, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 8, 211, 285, 'dglv2', 209, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 9, 212, 285, 'dglv2', 209, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 10, 213, 285, 'dglv2', 209, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 11, 210, 256, 'dglv2', 214, 287, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 12, 211, 256, 'dglv2', 214, 287, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 13, 211, 257, 'dglv2', 214, 287, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 14, 264, 256, 'dglv2', 273, 227, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 15, 265, 256, 'dglv2', 273, 227, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 16, 265, 257, 'dglv2', 273, 227, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 17, 271, 226, 'dglv2', 263, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 18, 271, 225, 'dglv2', 263, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 19, 272, 225, 'dglv2', 263, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 20, 273, 225, 'dglv2', 263, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 21, 157, 85, 'dglv2', 41, 34, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 22, 157, 86, 'dglv2', 41, 34, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 23, 157, 87, 'dglv2', 41, 34, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 24, 157, 88, 'dglv2', 41, 34, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 25, 157, 89, 'dglv2', 41, 34, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 26, 80, 198, 'dglv2', 56, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 27, 80, 197, 'dglv2', 56, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 28, 80, 196, 'dglv2', 56, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 29, 81, 196, 'dglv2', 56, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 30, 81, 195, 'dglv2', 56, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 31, 210, 108, 'dglv2', 40, 175, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 32, 211, 108, 'dglv2', 40, 175, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 33, 211, 109, 'dglv2', 40, 175, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 34, 122, 190, 'dglv2', 83, 179, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 35, 122, 191, 'dglv2', 83, 179, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 36, 122, 192, 'dglv2', 83, 179, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 37, 122, 193, 'dglv2', 83, 179, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 38, 122, 194, 'dglv2', 83, 179, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 39, 37, 173, 'dglv2', 208, 110, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 40, 38, 173, 'dglv2', 208, 110, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 41, 39, 173, 'dglv2', 208, 110, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 42, 37, 174, 'dglv2', 208, 110, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 43, 180, 39, 'dglv2', 212, 469, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 44, 181, 39, 'dglv2', 212, 469, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 45, 181, 38, 'dglv2', 212, 469, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 46, 364, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 47, 365, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 48, 366, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 49, 367, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 50, 368, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 51, 369, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 52, 370, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 53, 371, 253, 'dglv2', 437, 339, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 54, 438, 342, 'dglv2', 359, 267, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 55, 439, 342, 'dglv2', 359, 267, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 56, 438, 343, 'dglv2', 359, 267, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 57, 234, 399, 'dglv2', 392, 418, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 58, 234, 398, 'dglv2', 392, 418, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 59, 235, 398, 'dglv2', 392, 418, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 60, 228, 256, 'dglv3', 187, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 61, 229, 256, 'dglv3', 187, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv2', 62, 229, 257, 'dglv3', 187, 212, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 0, 189, 210, 'dglv2', 226, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 1, 190, 210, 'dglv2', 226, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 2, 190, 211, 'dglv2', 226, 258, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 3, 61, 243, 'dglv4', 63, 241, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 4, 60, 244, 'dglv4', 63, 241, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 5, 61, 244, 'dglv4', 63, 241, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 6, 342, 192, 'dglv4', 337, 232, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 7, 343, 191, 'dglv4', 337, 232, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 8, 343, 192, 'dglv4', 337, 232, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 9, 193, 87, 'dglv4', 206, 97, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 10, 192, 87, 'dglv4', 206, 97, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv3', 11, 193, 88, 'dglv4', 206, 97, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 0, 63, 238, 'dglv3', 59, 246, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 1, 64, 238, 'dglv3', 59, 246, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 2, 64, 237, 'dglv3', 59, 246, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 3, 208, 93, 'dglv3', 190, 90, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 4, 207, 94, 'dglv3', 190, 90, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 5, 208, 94, 'dglv3', 190, 90, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 6, 339, 230, 'dglv3', 341, 194, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 7, 340, 229, 'dglv3', 341, 194, 5);
INSERT INTO "map_teleport_locations" VALUES ('dglv4', 8, 340, 230, 'dglv3', 341, 194, 5);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 0, 33, 82, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 1, 34, 82, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 2, 35, 82, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 3, 35, 83, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 4, 36, 83, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 5, 36, 84, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 6, 147, 62, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 7, 148, 62, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 8, 149, 62, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 9, 149, 63, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 10, 150, 63, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('druncncity', 11, 150, 64, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 0, 26, 41, 'elvine', 73, 98, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 1, 27, 41, 'elvine', 73, 98, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 2, 26, 42, 'elvine', 73, 98, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 3, 104, 36, 'elvbrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 4, 105, 36, 'elvbrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 5, 105, 37, 'elvbrk12', 34, 35, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 6, 67, 70, 'elvbrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 7, 68, 70, 'elvbrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk11', 8, 68, 71, 'elvbrk21', 66, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk12', 0, 32, 33, 'elvbrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk12', 1, 33, 33, 'elvbrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk12', 2, 32, 34, 'elvbrk11', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 0, 67, 69, 'elvbrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 1, 67, 70, 'elvbrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 2, 68, 70, 'elvbrk11', 65, 72, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 3, 104, 36, 'elvbrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 4, 105, 36, 'elvbrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk21', 5, 105, 37, 'elvbrk22', 40, 31, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk22', 0, 38, 29, 'elvbrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk22', 1, 39, 29, 'elvbrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvbrk22', 2, 38, 30, 'elvbrk21', 102, 39, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 0, 20, 147, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 1, 20, 148, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 2, 20, 149, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 3, 20, 150, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 4, 20, 151, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 5, 20, 152, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 6, 20, 153, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 7, 20, 154, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 8, 20, 155, 'elvine', 274, 196, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 9, 116, 157, 'elvine', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 10, 117, 157, 'elvine', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 11, 116, 158, 'elvine', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 12, 117, 158, 'elvine', -1, -1, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 13, 121, 187, 'bsmith_2f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 14, 122, 187, 'bsmith_2f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 15, 123, 187, 'bsmith_2f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 16, 123, 186, 'bsmith_2f', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 17, 111, 192, 'bsmith_2f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 18, 111, 193, 'bsmith_2f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 19, 112, 193, 'bsmith_2f', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 20, 66, 195, 'wrhus_2f', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 21, 67, 196, 'wrhus_2f', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 22, 68, 197, 'wrhus_2f', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 23, 72, 197, 'wrhus_2f', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 24, 88, 178, 'gshop_2f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 25, 88, 179, 'gshop_2f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 26, 89, 179, 'gshop_2f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 27, 92, 179, 'gshop_2f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 28, 93, 178, 'gshop_2f', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 29, 156, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 30, 157, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 31, 158, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 32, 159, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 33, 160, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 34, 161, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 35, 162, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 36, 163, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvfarm', 37, 164, 229, '2ndmiddle', 126, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 0, 135, 133, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 1, 136, 133, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 2, 137, 133, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 3, 137, 132, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 4, 144, 127, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 5, 145, 127, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 6, 145, 126, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 7, 146, 126, 'cityhall_2', 57, 43, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 8, 131, 77, 'cath_2', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 9, 132, 77, 'cath_2', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 10, 132, 76, 'cath_2', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 11, 133, 76, 'cath_2', 40, 40, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 12, 239, 107, 'bsmith_2', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 13, 240, 107, 'bsmith_2', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 14, 241, 107, 'bsmith_2', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 15, 241, 106, 'bsmith_2', 34, 37, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 16, 229, 112, 'bsmith_2', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 17, 229, 113, 'bsmith_2', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 18, 230, 113, 'bsmith_2', 43, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 19, 197, 127, 'wrhus_2', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 20, 198, 128, 'wrhus_2', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 21, 199, 129, 'wrhus_2', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 22, 203, 129, 'wrhus_2', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 23, 87, 174, 'elvwrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 24, 88, 175, 'elvwrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 25, 89, 176, 'elvwrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 26, 93, 176, 'elvwrhus', 56, 36, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 27, 76, 141, 'gldhall_2', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 28, 77, 140, 'gldhall_2', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 29, 77, 141, 'gldhall_2', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 30, 78, 141, 'gldhall_2', 54, 41, 6);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 31, 180, 77, 'wzdtwr_2', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 32, 181, 77, 'wzdtwr_2', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 33, 181, 76, 'wzdtwr_2', 43, 34, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 34, 225, 151, 'gshop_2', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 35, 225, 152, 'gshop_2', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 36, 226, 152, 'gshop_2', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 37, 229, 152, 'gshop_2', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 38, 230, 151, 'gshop_2', 50, 39, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 39, 21, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 40, 22, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 41, 23, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 42, 24, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 43, 25, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 44, 26, 277, 'middleland', 103, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 45, 250, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 46, 251, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 47, 252, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 48, 253, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 49, 254, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 50, 255, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 51, 256, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 52, 257, 274, 'middleland', 314, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 53, 218, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 54, 219, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 55, 220, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 56, 221, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 57, 222, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 58, 223, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 59, 224, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 60, 225, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 61, 226, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 62, 227, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 63, 228, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 64, 229, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 65, 230, 20, 'huntzone1', 53, 174, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 66, 258, 82, 'elvined1', 105, 159, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 67, 258, 83, 'elvined1', 105, 159, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 68, 259, 82, 'elvined1', 105, 159, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 69, 259, 81, 'elvined1', 105, 159, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 70, 260, 81, 'elvined1', 105, 159, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 71, 277, 192, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 72, 277, 193, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 73, 277, 194, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 74, 277, 195, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 75, 277, 196, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 76, 277, 197, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 77, 277, 198, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 78, 277, 199, 'elvfarm', 23, 149, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 79, 213, 89, 'cmdhall_2', 51, 50, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 80, 214, 89, 'cmdhall_2', 51, 50, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 81, 214, 88, 'cmdhall_2', 51, 50, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 82, 220, 87, 'cmdhall_2', 40, 52, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 83, 221, 87, 'cmdhall_2', 40, 52, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvine', 84, 222, 87, 'cmdhall_2', 40, 52, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 0, 103, 157, 'elvine', 257, 80, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 1, 104, 157, 'elvine', 257, 80, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 2, 105, 157, 'elvine', 257, 80, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 3, 103, 158, 'elvine', 257, 80, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 4, 85, 93, 'huntzone1', 114, 113, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 5, 86, 93, 'huntzone1', 114, 113, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 6, 87, 93, 'huntzone1', 114, 113, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 7, 85, 94, 'huntzone1', 114, 113, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 8, 96, 40, 'dglv2', 35, 38, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 9, 97, 40, 'dglv2', 35, 38, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvined1', 10, 97, 41, 'dglv2', 35, 38, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 0, 176, 20, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 1, 176, 21, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 2, 176, 22, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 3, 176, 23, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 4, 176, 24, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 5, 176, 25, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 6, 176, 26, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 7, 176, 27, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvuni', 8, 176, 28, 'huntzone1', 23, 53, 1);
INSERT INTO "map_teleport_locations" VALUES ('elvwrhus', 0, 54, 33, 'elvine', 92, 177, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvwrhus', 1, 53, 34, 'elvine', 92, 177, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvwrhus', 2, 54, 34, 'elvine', 92, 177, 4);
INSERT INTO "map_teleport_locations" VALUES ('elvwrhus', 3, 55, 34, 'elvine', 92, 177, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_1', 0, 59, 41, 'aresden', 114, 99, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_1', 1, 60, 42, 'aresden', 114, 99, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_1', 2, 60, 41, 'aresden', 114, 99, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_2', 0, 59, 41, 'elvine', 78, 143, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_2', 1, 60, 42, 'elvine', 78, 143, 4);
INSERT INTO "map_teleport_locations" VALUES ('gldhall_2', 2, 60, 41, 'elvine', 78, 143, 4);
INSERT INTO "map_teleport_locations" VALUES ('godh', 0, 213, 179, 'hrampart', 49, 43, 2);
INSERT INTO "map_teleport_locations" VALUES ('godh', 1, 214, 179, 'hrampart', 49, 43, 2);
INSERT INTO "map_teleport_locations" VALUES ('godh', 2, 215, 179, 'hrampart', 49, 43, 2);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1', 0, 49, 36, 'aresden', 129, 169, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1', 1, 50, 36, 'aresden', 129, 169, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1', 2, 49, 37, 'aresden', 129, 169, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1', 3, 50, 37, 'aresden', 129, 169, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1', 4, 51, 37, 'aresden', 129, 169, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1f', 0, 49, 36, 'arefarm', 63, 72, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1f', 1, 50, 36, 'arefarm', 63, 72, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1f', 2, 49, 37, 'arefarm', 63, 72, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1f', 3, 50, 37, 'arefarm', 63, 72, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_1f', 4, 51, 37, 'arefarm', 63, 72, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2', 0, 49, 36, 'elvine', 228, 153, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2', 1, 50, 36, 'elvine', 228, 153, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2', 2, 49, 37, 'elvine', 228, 153, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2', 3, 50, 37, 'elvine', 228, 153, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2', 4, 51, 37, 'elvine', 228, 153, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2f', 0, 49, 36, 'elvfarm', 92, 181, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2f', 1, 50, 36, 'elvfarm', 92, 181, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2f', 2, 49, 37, 'elvfarm', 92, 181, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2f', 3, 50, 37, 'elvfarm', 92, 181, 4);
INSERT INTO "map_teleport_locations" VALUES ('gshop_2f', 4, 51, 37, 'elvfarm', 92, 181, 4);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 0, 43, 39, 'godh', 455, 204, 3);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 1, 44, 39, 'godh', 455, 204, 3);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 2, 44, 38, 'godh', 455, 204, 4);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 3, 45, 38, 'godh', 455, 204, 5);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 4, 46, 38, 'godh', 455, 204, 1);
INSERT INTO "map_teleport_locations" VALUES ('hrampart', 5, 46, 37, 'godh', 455, 204, 2);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 0, 44, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 1, 45, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 2, 46, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 3, 47, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 4, 48, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 5, 49, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 6, 50, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 7, 51, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 8, 52, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 9, 53, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 10, 54, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 11, 55, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 12, 56, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 13, 57, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 14, 58, 179, 'elvine', 223, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 15, 115, 115, 'elvined1', 87, 95, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 16, 116, 115, 'elvined1', 87, 95, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 17, 116, 114, 'elvined1', 87, 95, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 18, 171, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 19, 172, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 20, 173, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 21, 174, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 22, 175, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 23, 176, 20, 'huntzone3', 50, 166, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 24, 20, 48, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 25, 20, 49, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 26, 20, 50, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 27, 20, 51, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 28, 20, 52, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 29, 20, 53, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 30, 20, 54, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 31, 20, 55, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone1', 32, 20, 56, 'elvuni', 173, 24, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 0, 66, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 1, 67, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 2, 68, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 3, 69, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 4, 70, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 5, 71, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 6, 72, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 7, 73, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 8, 74, 20, 'aresden', 32, 274, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 9, 103, 107, 'aresdend1', 97, 87, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 10, 104, 107, 'aresdend1', 97, 87, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 11, 104, 106, 'aresdend1', 97, 87, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 12, 179, 100, 'huntzone4', 23, 93, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 13, 179, 101, 'huntzone4', 23, 93, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 14, 179, 102, 'huntzone4', 23, 93, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 15, 179, 103, 'huntzone4', 23, 93, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 16, 179, 104, 'huntzone4', 23, 93, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 17, 109, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 18, 110, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 19, 111, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 20, 112, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 21, 113, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 22, 114, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 23, 115, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 24, 116, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 25, 117, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 26, 118, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 27, 119, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 28, 120, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 29, 121, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone2', 30, 122, 179, 'areuni', 85, 23, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 0, 45, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 1, 46, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 2, 47, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 3, 48, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 4, 49, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone3', 5, 50, 168, 'huntzone1', 174, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 0, 20, 91, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 1, 20, 92, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 2, 20, 93, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 3, 20, 94, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 4, 20, 95, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 5, 20, 96, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 6, 20, 97, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 7, 20, 98, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('huntzone4', 8, 20, 99, 'huntzone2', 176, 99, 1);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 0, 260, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 1, 261, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 2, 262, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 3, 263, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 4, 264, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 5, 265, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 6, 266, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 7, 267, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 8, 268, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('icebound', 9, 269, 264, 'middleland', 455, 279, 5);
INSERT INTO "map_teleport_locations" VALUES ('inferniaa', 0, 119, 29, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('inferniab', 0, 31, 28, 'inferniaa', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('maze', 0, 38, 164, 'procella', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 0, 31, 34, 'elvine', 56, 118, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 1, 32, 34, 'elvine', 56, 118, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 2, 32, 33, 'elvine', 56, 118, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 3, 33, 33, 'elvine', 56, 118, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 4, 183, 122, 'aresden', 257, 168, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 5, 184, 122, 'aresden', 257, 168, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 6, 184, 123, 'aresden', 257, 168, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1n', 7, 185, 123, 'aresden', 257, 168, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1x', 0, 67, 106, 'middleland', 198, 233, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1x', 1, 68, 106, 'middleland', 198, 233, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1x', 2, 68, 105, 'middleland', 198, 233, 5);
INSERT INTO "map_teleport_locations" VALUES ('middled1x', 3, 69, 105, 'middleland', 198, 233, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 0, 147, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 1, 148, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 2, 149, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 3, 150, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 4, 151, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 5, 152, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 6, 153, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 7, 154, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 8, 155, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 9, 156, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 10, 157, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 11, 158, 503, 'aresden', 31, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 12, 344, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 13, 345, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 14, 346, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 15, 347, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 16, 348, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 17, 349, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 18, 350, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 19, 351, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 20, 352, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 21, 353, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 22, 354, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 23, 355, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 24, 356, 503, 'aresden', 259, 23, 5);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 25, 99, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 26, 100, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 27, 101, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 28, 102, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 29, 103, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 30, 104, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 31, 105, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 32, 106, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 33, 107, 20, 'elvine', 27, 271, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 34, 309, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 35, 310, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 36, 311, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 37, 312, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 38, 313, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 39, 314, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 40, 315, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 41, 316, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 42, 317, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 43, 318, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 44, 319, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 45, 320, 20, 'elvine', 254, 267, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 46, 199, 235, 'middled1x', 70, 108, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 47, 200, 235, 'middled1x', 70, 108, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 48, 200, 234, 'middled1x', 70, 108, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 49, 381, 284, 'toh1', 145, 31, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 50, 382, 284, 'toh1', 145, 31, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 51, 383, 284, 'toh1', 145, 31, 1);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 52, 452, 281, 'icebound', 264, 260, 8);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 53, 453, 281, 'icebound', 264, 260, 8);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 54, 452, 282, 'icebound', 264, 260, 8);
INSERT INTO "map_teleport_locations" VALUES ('middleland', 55, 453, 282, 'icebound', 264, 260, 8);
INSERT INTO "map_teleport_locations" VALUES ('procella', 0, 119, 29, 'inferniab', -1, -1, 7);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 0, 38, 33, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 1, 39, 33, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 2, 38, 34, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 3, 39, 34, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 4, 158, 33, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 5, 159, 33, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 6, 158, 34, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 7, 159, 34, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 8, 38, 113, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 9, 39, 113, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 10, 38, 114, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 11, 39, 114, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 12, 158, 113, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 13, 159, 113, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 14, 158, 114, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr1', 15, 159, 114, 'aresden', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 0, 38, 33, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 1, 39, 33, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 2, 38, 34, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 3, 39, 34, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 4, 158, 33, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 5, 159, 33, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 6, 158, 34, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 7, 159, 34, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 8, 38, 113, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 9, 39, 113, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 10, 38, 114, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 11, 39, 114, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 12, 158, 113, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 13, 159, 113, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 14, 158, 114, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('resurr2', 15, 159, 114, 'elvine', -1, -1, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 0, 146, 29, 'middleland', 382, 286, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 1, 147, 29, 'middleland', 382, 286, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 2, 147, 30, 'middleland', 382, 286, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 3, 37, 218, 'toh2', 39, 38, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 4, 37, 219, 'toh2', 39, 38, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 5, 38, 219, 'toh2', 39, 38, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 6, 218, 213, 'toh2', 272, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 7, 219, 213, 'toh2', 272, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh1', 8, 219, 214, 'toh2', 272, 28, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 0, 40, 36, 'toh1', 35, 220, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 1, 41, 36, 'toh1', 35, 220, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 2, 41, 37, 'toh1', 35, 220, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 3, 273, 26, 'toh1', 217, 215, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 4, 274, 26, 'toh1', 217, 215, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 5, 274, 27, 'toh1', 217, 215, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 6, 102, 268, 'toh3', 93, 40, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 7, 103, 268, 'toh3', 93, 40, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 8, 103, 269, 'toh3', 93, 40, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 9, 252, 240, 'toh3', 147, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 10, 253, 240, 'toh3', 147, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh2', 11, 253, 241, 'toh3', 147, 32, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 0, 93, 38, 'toh2', 101, 270, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 1, 94, 38, 'toh2', 101, 270, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 2, 94, 39, 'toh2', 101, 270, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 3, 147, 30, 'toh2', 251, 240, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 4, 148, 30, 'toh2', 251, 240, 5);
INSERT INTO "map_teleport_locations" VALUES ('toh3', 5, 148, 31, 'toh2', 251, 240, 5);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1', 0, 54, 33, 'aresden', 107, 186, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1', 1, 53, 34, 'aresden', 107, 186, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1', 2, 54, 34, 'aresden', 107, 186, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1', 3, 55, 34, 'aresden', 107, 186, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 0, 54, 33, 'arefarm', 38, 92, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 1, 53, 34, 'arefarm', 38, 92, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 2, 54, 34, 'arefarm', 38, 92, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 3, 55, 34, 'arefarm', 38, 92, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 4, 61, 35, 'arefarm', 34, 90, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_1f', 5, 61, 34, 'arefarm', 34, 90, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2', 0, 54, 33, 'elvine', 203, 130, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2', 1, 53, 34, 'elvine', 203, 130, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2', 2, 54, 34, 'elvine', 203, 130, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2', 3, 55, 34, 'elvine', 203, 130, 4);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2f', 0, 54, 33, 'elvfarm', 71, 199, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2f', 1, 53, 34, 'elvfarm', 71, 199, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2f', 2, 54, 34, 'elvfarm', 71, 199, 6);
INSERT INTO "map_teleport_locations" VALUES ('wrhus_2f', 3, 55, 34, 'elvfarm', 71, 199, 6);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_1', 0, 41, 32, 'aresden', 58, 119, 4);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_1', 1, 40, 32, 'aresden', 58, 119, 4);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_1', 2, 40, 33, 'aresden', 58, 119, 4);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_2', 0, 41, 32, 'elvine', 181, 78, 4);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_2', 1, 40, 32, 'elvine', 181, 78, 4);
INSERT INTO "map_teleport_locations" VALUES ('wzdtwr_2', 2, 40, 33, 'elvine', 181, 78, 4);

-- ----------------------------
-- Table structure for map_waypoints
-- ----------------------------
DROP TABLE IF EXISTS "map_waypoints";
CREATE TABLE "map_waypoints" (
  "map_name" TEXT NOT NULL,
  "waypoint_index" INTEGER NOT NULL,
  "x" INTEGER NOT NULL,
  "y" INTEGER NOT NULL,
  PRIMARY KEY ("map_name", "waypoint_index"),
  FOREIGN KEY ("map_name") REFERENCES "maps" ("map_name") ON DELETE CASCADE ON UPDATE NO ACTION
);

-- ----------------------------
-- Records of map_waypoints
-- ----------------------------
INSERT INTO "map_waypoints" VALUES ('2ndmiddle', 0, 75, 75);
INSERT INTO "map_waypoints" VALUES ('2ndmiddle', 1, 75, 200);
INSERT INTO "map_waypoints" VALUES ('2ndmiddle', 2, 200, 200);
INSERT INTO "map_waypoints" VALUES ('2ndmiddle', 3, 200, 75);
INSERT INTO "map_waypoints" VALUES ('2ndmiddle', 4, 125, 125);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk11', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk12', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk21', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('arebrk22', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('arefarm', 1, 36, 214);
INSERT INTO "map_waypoints" VALUES ('arefarm', 2, 130, 207);
INSERT INTO "map_waypoints" VALUES ('arefarm', 3, 208, 205);
INSERT INTO "map_waypoints" VALUES ('arefarm', 4, 193, 151);
INSERT INTO "map_waypoints" VALUES ('arefarm', 5, 121, 155);
INSERT INTO "map_waypoints" VALUES ('arefarm', 6, 43, 113);
INSERT INTO "map_waypoints" VALUES ('arefarm', 7, 133, 90);
INSERT INTO "map_waypoints" VALUES ('arefarm', 8, 210, 73);
INSERT INTO "map_waypoints" VALUES ('arefarm', 9, 134, 30);
INSERT INTO "map_waypoints" VALUES ('arefarm', 10, 60, 40);
INSERT INTO "map_waypoints" VALUES ('aresden', 0, 60, 55);
INSERT INTO "map_waypoints" VALUES ('aresden', 1, 140, 60);
INSERT INTO "map_waypoints" VALUES ('aresden', 2, 250, 75);
INSERT INTO "map_waypoints" VALUES ('aresden', 3, 60, 125);
INSERT INTO "map_waypoints" VALUES ('aresden', 4, 150, 130);
INSERT INTO "map_waypoints" VALUES ('aresden', 5, 200, 130);
INSERT INTO "map_waypoints" VALUES ('aresden', 6, 255, 135);
INSERT INTO "map_waypoints" VALUES ('aresden', 7, 70, 220);
INSERT INTO "map_waypoints" VALUES ('aresden', 8, 170, 230);
INSERT INTO "map_waypoints" VALUES ('aresden', 9, 255, 225);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('aresdend1', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('areuni', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('areuni', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('areuni', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('areuni', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('areuni', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('areuni', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('areuni', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('areuni', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('areuni', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('areuni', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('arewrhus', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('bisle', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('bisle', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('bisle', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('bisle', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('bisle', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('bisle', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('bisle', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('bisle', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('bisle', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('bisle', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1', 10, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_1f', 10, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2', 10, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('bsmith_2f', 10, 49, 33);
INSERT INTO "map_waypoints" VALUES ('btfield', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('btfield', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('btfield', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('btfield', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('btfield', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('btfield', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('btfield', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('btfield', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('btfield', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('btfield', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('cath_1', 1, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 2, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 3, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 4, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 5, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 6, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 7, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 8, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 9, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_1', 10, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 1, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 2, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 3, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 4, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 5, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 6, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 7, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 8, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 9, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cath_2', 10, 60, 50);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 1, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 2, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 3, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 4, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 5, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 6, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 7, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 8, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 9, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_1', 10, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 1, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 2, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 3, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 4, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 5, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 6, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 7, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 8, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 9, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cityhall_2', 10, 45, 43);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 1, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 2, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 3, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 4, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 5, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 6, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 7, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 8, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 9, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_1', 10, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 1, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 2, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 3, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 4, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 5, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 6, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 7, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 8, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 9, 57, 50);
INSERT INTO "map_waypoints" VALUES ('cmdhall_2', 10, 57, 50);
INSERT INTO "map_waypoints" VALUES ('dglv2', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('dglv2', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('dglv2', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('dglv2', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('dglv2', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('dglv2', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('dglv2', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('dglv2', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('dglv2', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('dglv2', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('dglv3', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('dglv3', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('dglv3', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('dglv3', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('dglv3', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('dglv3', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('dglv3', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('dglv3', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('dglv3', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('dglv3', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('dglv4', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('dglv4', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('dglv4', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('dglv4', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('dglv4', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('dglv4', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('dglv4', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('dglv4', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('dglv4', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('dglv4', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('druncncity', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('druncncity', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('druncncity', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('druncncity', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('druncncity', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('druncncity', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('druncncity', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('druncncity', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('druncncity', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('druncncity', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk11', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk12', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk21', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('elvbrk22', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 1, 44, 43);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 2, 138, 51);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 3, 201, 61);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 4, 76, 98);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 5, 152, 128);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 6, 217, 151);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 7, 50, 169);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 8, 134, 180);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 9, 194, 197);
INSERT INTO "map_waypoints" VALUES ('elvfarm', 10, 73, 218);
INSERT INTO "map_waypoints" VALUES ('elvine', 0, 45, 55);
INSERT INTO "map_waypoints" VALUES ('elvine', 1, 150, 80);
INSERT INTO "map_waypoints" VALUES ('elvine', 2, 255, 70);
INSERT INTO "map_waypoints" VALUES ('elvine', 3, 30, 130);
INSERT INTO "map_waypoints" VALUES ('elvine', 4, 110, 135);
INSERT INTO "map_waypoints" VALUES ('elvine', 5, 190, 145);
INSERT INTO "map_waypoints" VALUES ('elvine', 6, 250, 140);
INSERT INTO "map_waypoints" VALUES ('elvine', 7, 80, 215);
INSERT INTO "map_waypoints" VALUES ('elvine', 8, 165, 235);
INSERT INTO "map_waypoints" VALUES ('elvine', 9, 230, 230);
INSERT INTO "map_waypoints" VALUES ('elvined1', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('elvined1', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('elvined1', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('elvined1', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('elvined1', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('elvined1', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('elvined1', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('elvined1', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('elvined1', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('elvined1', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('elvuni', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('elvuni', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('elvuni', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('elvuni', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('elvuni', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('elvuni', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('elvuni', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('elvuni', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('elvuni', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('elvuni', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('elvwrhus', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone1', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone2', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone3', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone4', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 0, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 1, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 2, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 3, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 4, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 5, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 6, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 7, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 8, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone5', 9, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 0, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 1, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 2, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 3, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 4, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 5, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 6, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 7, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 8, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone6', 9, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 0, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 1, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 2, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 3, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 4, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 5, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 6, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 7, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 8, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone7', 9, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 0, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 1, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 2, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 3, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 4, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 5, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 6, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 7, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 8, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone8', 9, 30, 30);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 1, 55, 39);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 2, 50, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 3, 60, 38);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 4, 65, 40);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 5, 63, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 6, 58, 47);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 7, 52, 48);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 8, 49, 44);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 9, 47, 41);
INSERT INTO "map_waypoints" VALUES ('fightzone9', 10, 55, 41);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 1, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 2, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 3, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 4, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 5, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 6, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 7, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 8, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 9, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_1', 10, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 1, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 2, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 3, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 4, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 5, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 6, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 7, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 8, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 9, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gldhall_2', 10, 42, 44);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 1, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 2, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 3, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 4, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 5, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 6, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 7, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 8, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 9, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1', 10, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 1, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 2, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 3, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 4, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 5, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 6, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 7, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 8, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 9, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_1f', 10, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 1, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 2, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 3, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 4, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 5, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 6, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 7, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 8, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 9, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2', 10, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 1, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 2, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 3, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 4, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 5, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 6, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 7, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 8, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 9, 59, 42);
INSERT INTO "map_waypoints" VALUES ('gshop_2f', 10, 59, 42);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('huntzone1', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('huntzone2', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('huntzone3', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('huntzone4', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('icebound', 0, 55, 94);
INSERT INTO "map_waypoints" VALUES ('icebound', 1, 113, 76);
INSERT INTO "map_waypoints" VALUES ('icebound', 2, 156, 104);
INSERT INTO "map_waypoints" VALUES ('icebound', 3, 187, 140);
INSERT INTO "map_waypoints" VALUES ('icebound', 4, 161, 164);
INSERT INTO "map_waypoints" VALUES ('icebound', 5, 135, 169);
INSERT INTO "map_waypoints" VALUES ('icebound', 6, 109, 191);
INSERT INTO "map_waypoints" VALUES ('icebound', 7, 98, 162);
INSERT INTO "map_waypoints" VALUES ('icebound', 8, 64, 137);
INSERT INTO "map_waypoints" VALUES ('icebound', 9, 198, 105);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('inferniaa', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('inferniab', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('inferniab', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('inferniab', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('inferniab', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('inferniab', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('inferniab', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('inferniab', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('inferniab', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('inferniab', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('inferniab', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('maze', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('maze', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('maze', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('maze', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('maze', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('maze', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('maze', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('maze', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('maze', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('maze', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('middled1n', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('middled1n', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('middled1n', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('middled1n', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('middled1n', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('middled1n', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('middled1n', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('middled1n', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('middled1n', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('middled1n', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('middled1x', 0, 107, 105);
INSERT INTO "map_waypoints" VALUES ('middled1x', 1, 145, 116);
INSERT INTO "map_waypoints" VALUES ('middled1x', 2, 200, 146);
INSERT INTO "map_waypoints" VALUES ('middled1x', 3, 184, 176);
INSERT INTO "map_waypoints" VALUES ('middled1x', 4, 151, 204);
INSERT INTO "map_waypoints" VALUES ('middled1x', 5, 134, 137);
INSERT INTO "map_waypoints" VALUES ('middled1x', 6, 37, 152);
INSERT INTO "map_waypoints" VALUES ('middled1x', 7, 123, 188);
INSERT INTO "map_waypoints" VALUES ('middled1x', 8, 129, 126);
INSERT INTO "map_waypoints" VALUES ('middled1x', 9, 113, 153);
INSERT INTO "map_waypoints" VALUES ('middleland', 0, 50, 140);
INSERT INTO "map_waypoints" VALUES ('middleland', 1, 135, 105);
INSERT INTO "map_waypoints" VALUES ('middleland', 2, 260, 100);
INSERT INTO "map_waypoints" VALUES ('middleland', 3, 380, 65);
INSERT INTO "map_waypoints" VALUES ('middleland', 4, 450, 130);
INSERT INTO "map_waypoints" VALUES ('middleland', 5, 40, 210);
INSERT INTO "map_waypoints" VALUES ('middleland', 6, 170, 175);
INSERT INTO "map_waypoints" VALUES ('middleland', 7, 260, 190);
INSERT INTO "map_waypoints" VALUES ('middleland', 8, 360, 215);
INSERT INTO "map_waypoints" VALUES ('middleland', 9, 455, 235);
INSERT INTO "map_waypoints" VALUES ('middleland', 10, 50, 355);
INSERT INTO "map_waypoints" VALUES ('middleland', 11, 140, 295);
INSERT INTO "map_waypoints" VALUES ('middleland', 12, 270, 335);
INSERT INTO "map_waypoints" VALUES ('middleland', 13, 330, 395);
INSERT INTO "map_waypoints" VALUES ('middleland', 14, 450, 380);
INSERT INTO "map_waypoints" VALUES ('middleland', 15, 140, 470);
INSERT INTO "map_waypoints" VALUES ('middleland', 16, 215, 475);
INSERT INTO "map_waypoints" VALUES ('middleland', 17, 310, 455);
INSERT INTO "map_waypoints" VALUES ('middleland', 18, 400, 455);
INSERT INTO "map_waypoints" VALUES ('middleland', 19, 450, 525);
INSERT INTO "map_waypoints" VALUES ('procella', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('procella', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('procella', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('procella', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('procella', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('procella', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('procella', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('procella', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('procella', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('procella', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('toh1', 0, 64, 54);
INSERT INTO "map_waypoints" VALUES ('toh1', 1, 126, 54);
INSERT INTO "map_waypoints" VALUES ('toh1', 2, 195, 49);
INSERT INTO "map_waypoints" VALUES ('toh1', 3, 75, 97);
INSERT INTO "map_waypoints" VALUES ('toh1', 4, 121, 109);
INSERT INTO "map_waypoints" VALUES ('toh1', 5, 197, 149);
INSERT INTO "map_waypoints" VALUES ('toh1', 6, 40, 153);
INSERT INTO "map_waypoints" VALUES ('toh1', 7, 45, 214);
INSERT INTO "map_waypoints" VALUES ('toh1', 8, 107, 179);
INSERT INTO "map_waypoints" VALUES ('toh1', 9, 215, 219);
INSERT INTO "map_waypoints" VALUES ('toh2', 0, 49, 58);
INSERT INTO "map_waypoints" VALUES ('toh2', 1, 117, 35);
INSERT INTO "map_waypoints" VALUES ('toh2', 2, 241, 45);
INSERT INTO "map_waypoints" VALUES ('toh2', 3, 75, 117);
INSERT INTO "map_waypoints" VALUES ('toh2', 4, 185, 80);
INSERT INTO "map_waypoints" VALUES ('toh2', 5, 265, 178);
INSERT INTO "map_waypoints" VALUES ('toh2', 6, 163, 159);
INSERT INTO "map_waypoints" VALUES ('toh2', 7, 43, 250);
INSERT INTO "map_waypoints" VALUES ('toh2', 8, 149, 249);
INSERT INTO "map_waypoints" VALUES ('toh2', 9, 239, 243);
INSERT INTO "map_waypoints" VALUES ('toh3', 0, 81, 50);
INSERT INTO "map_waypoints" VALUES ('toh3', 1, 156, 66);
INSERT INTO "map_waypoints" VALUES ('toh3', 2, 260, 40);
INSERT INTO "map_waypoints" VALUES ('toh3', 3, 85, 119);
INSERT INTO "map_waypoints" VALUES ('toh3', 4, 138, 155);
INSERT INTO "map_waypoints" VALUES ('toh3', 5, 260, 142);
INSERT INTO "map_waypoints" VALUES ('toh3', 6, 86, 197);
INSERT INTO "map_waypoints" VALUES ('toh3', 7, 44, 262);
INSERT INTO "map_waypoints" VALUES ('toh3', 8, 167, 239);
INSERT INTO "map_waypoints" VALUES ('toh3', 9, 226, 252);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_1f', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 1, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 2, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 3, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 4, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 5, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 6, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 7, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 8, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 9, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wrhus_2f', 10, 66, 38);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_1', 10, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 1, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 2, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 3, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 4, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 5, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 6, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 7, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 8, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 9, 49, 33);
INSERT INTO "map_waypoints" VALUES ('wzdtwr_2', 10, 49, 33);

-- ----------------------------
-- Table structure for maps
-- ----------------------------
DROP TABLE IF EXISTS "maps";
CREATE TABLE "maps" (
  "map_name" TEXT,
  "location_name" TEXT NOT NULL DEFAULT '',
  "maximum_object" INTEGER NOT NULL DEFAULT 1000,
  "level_limit" INTEGER NOT NULL DEFAULT 0,
  "upper_level_limit" INTEGER NOT NULL DEFAULT 0,
  "map_type" INTEGER NOT NULL DEFAULT 0,
  "random_mob_generator_enabled" INTEGER NOT NULL DEFAULT 0,
  "random_mob_generator_level" INTEGER NOT NULL DEFAULT 0,
  "mineral_generator_enabled" INTEGER NOT NULL DEFAULT 0,
  "mineral_generator_level" INTEGER NOT NULL DEFAULT 0,
  "max_fish" INTEGER NOT NULL DEFAULT 0,
  "max_mineral" INTEGER NOT NULL DEFAULT 0,
  "fixed_day_mode" INTEGER NOT NULL DEFAULT 0,
  "recall_impossible" INTEGER NOT NULL DEFAULT 0,
  "apocalypse_map" INTEGER NOT NULL DEFAULT 0,
  "apocalypse_mob_gen_type" INTEGER NOT NULL DEFAULT 0,
  "citizen_limit" INTEGER NOT NULL DEFAULT 0,
  "is_fight_zone" INTEGER NOT NULL DEFAULT 0,
  "heldenian_map" INTEGER NOT NULL DEFAULT 0,
  "heldenian_mode_map" INTEGER NOT NULL DEFAULT 0,
  "mob_event_amount" INTEGER NOT NULL DEFAULT 15,
  "energy_sphere_auto_creation" INTEGER NOT NULL DEFAULT 0,
  "pk_mode" INTEGER NOT NULL DEFAULT 0,
  "attack_enabled" INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY ("map_name"),
   (length(map_name) <= 10),
   (length(location_name) <= 10)
);

-- ----------------------------
-- Records of maps
-- ----------------------------
INSERT INTO "maps" VALUES ('2ndmiddle', '2ndmiddle', 400, 0, 120, 0, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('abaddon', 'abaddon', 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arebrk11', 'aresden', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arebrk12', 'aresden', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arebrk21', 'aresden', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arebrk22', 'aresden', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arefarm', 'aresden', 300, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arejail', 'arejail', 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('aresden', 'aresden', 600, 0, 0, 0, 1, 1, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('aresdend1', 'aresden', 180, 20, 0, 0, 1, 4, 1, 4, 0, 26, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('areuni', 'aresden', 350, 0, 0, 0, 1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('arewrhus', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('bisle', 'bisle', 50, 0, 0, 1, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('bsmith_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('bsmith_1f', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('bsmith_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('bsmith_2f', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('btfield', 'btfield', 900, 1, 0, 0, 1, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('cath_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('cath_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('cityhall_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('cityhall_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('cmdhall_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('cmdhall_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('default', 'default', 122, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('dglv2', 'dglv2', 999, 40, 0, 0, 1, 5, 1, 6, 0, 39, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('dglv3', 'dglv3', 700, 50, 0, 0, 1, 9, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('dglv4', 'dglv4', 600, 60, 0, 0, 1, 10, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('druncncity', 'druncncity', 100, 0, 0, 0, 1, 18, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvbrk11', 'elvine', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvbrk12', 'elvine', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvbrk21', 'elvine', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvbrk22', 'elvine', 200, 0, 50, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvfarm', 'elvine', 300, 0, 0, 0, 1, 1, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvine', 'elvine', 600, 0, 0, 0, 1, 1, 0, 0, 25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvined1', 'elvine', 180, 20, 0, 0, 1, 4, 1, 4, 0, 26, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvjail', 'elvjail', 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvuni', 'elvine', 350, 0, 0, 0, 1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('elvwrhus', 'elvine', 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('fightzone1', 'fightzone1', 50, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone2', 'fightzone2', 50, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone3', 'fightzone3', 50, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone4', 'fightzone4', 50, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone5', 'fightzone5', 999, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone6', 'fightzone6', 999, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone7', 'fightzone7', 999, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone8', 'fightzone8', 999, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('fightzone9', 'fightzone1', 65, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('gldhall_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('gldhall_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('godh', 'godh', 999, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('gshop_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('gshop_1f', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('gshop_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('gshop_2f', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('hrampart', 'hrampart', 999, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('huntzone1', 'elvine', 350, 20, 0, 0, 1, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('huntzone2', 'aresden', 350, 20, 0, 0, 1, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('huntzone3', 'elvine', 350, 30, 0, 0, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('huntzone4', 'aresden', 350, 30, 0, 0, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('icebound', 'icebound', 500, 20, 0, 0, 1, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('inferniaa', 'interniaa', 400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('inferniab', 'interniab', 400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('maze', 'maze', 100, 0, 0, 0, 1, 19, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('middled1n', 'middled1n', 150, 30, 100, 0, 1, 12, 1, 4, 0, 4, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('middled1x', 'middled1x', 150, 0, 0, 0, 1, 5, 1, 6, 0, 30, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('middleland', 'middleland', 750, 20, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('procella', 'procella', 700, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('resurr1', 'aresden', 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('resurr2', 'elvine', 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('toh1', 'toh1', 300, 0, 0, 0, 1, 13, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('toh2', 'toh2', 200, 0, 0, 0, 1, 14, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('toh3', 'toh3', 180, 180, 0, 0, 1, 15, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 1);
INSERT INTO "maps" VALUES ('wrhus_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('wrhus_1f', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('wrhus_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('wrhus_2f', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('wzdtwr_1', 'aresden', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);
INSERT INTO "maps" VALUES ('wzdtwr_2', 'elvine', 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0);

-- ----------------------------
-- Table structure for meta
-- ----------------------------
DROP TABLE IF EXISTS "meta";
CREATE TABLE "meta" (
  "key" TEXT,
  "value" TEXT NOT NULL,
  PRIMARY KEY ("key")
);

-- ----------------------------
-- Records of meta
-- ----------------------------
INSERT INTO "meta" VALUES ('schema_version', '2');

-- ----------------------------
-- Table structure for sqlite_sequence
-- ----------------------------
DROP TABLE IF EXISTS "sqlite_sequence";
CREATE TABLE "sqlite_sequence" (
  "name",
  "seq"
);

-- ----------------------------
-- Records of sqlite_sequence
-- ----------------------------
INSERT INTO "sqlite_sequence" VALUES ('map_npcs', 22);

-- ----------------------------
-- Indexes structure for table map_energy_sphere_creation
-- ----------------------------
CREATE INDEX "idx_sphere_creation_map"
ON "map_energy_sphere_creation" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_energy_sphere_goal
-- ----------------------------
CREATE INDEX "idx_sphere_goal_map"
ON "map_energy_sphere_goal" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_fish_points
-- ----------------------------
CREATE INDEX "idx_fish_map"
ON "map_fish_points" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_heldenian_gate_doors
-- ----------------------------
CREATE INDEX "idx_heldenian_doors_map"
ON "map_heldenian_gate_doors" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_heldenian_towers
-- ----------------------------
CREATE INDEX "idx_heldenian_towers_map"
ON "map_heldenian_towers" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_initial_points
-- ----------------------------
CREATE INDEX "idx_initial_points_map"
ON "map_initial_points" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_item_events
-- ----------------------------
CREATE INDEX "idx_item_events_map"
ON "map_item_events" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_mineral_points
-- ----------------------------
CREATE INDEX "idx_mineral_map"
ON "map_mineral_points" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_no_attack_areas
-- ----------------------------
CREATE INDEX "idx_no_attack_map"
ON "map_no_attack_areas" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_npc_avoid_rects
-- ----------------------------
CREATE INDEX "idx_npc_avoid_map"
ON "map_npc_avoid_rects" (
  "map_name" ASC
);

-- ----------------------------
-- Auto increment value for map_npcs
-- ----------------------------
UPDATE "sqlite_sequence" SET seq = 22 WHERE name = 'map_npcs';

-- ----------------------------
-- Indexes structure for table map_npcs
-- ----------------------------
CREATE INDEX "idx_npcs_map"
ON "map_npcs" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_spot_mob_generators
-- ----------------------------
CREATE INDEX "idx_spot_mob_map"
ON "map_spot_mob_generators" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_strategic_points
-- ----------------------------
CREATE INDEX "idx_strategic_map"
ON "map_strategic_points" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_strike_points
-- ----------------------------
CREATE INDEX "idx_strike_map"
ON "map_strike_points" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_teleport_locations
-- ----------------------------
CREATE INDEX "idx_teleport_map"
ON "map_teleport_locations" (
  "map_name" ASC
);

-- ----------------------------
-- Indexes structure for table map_waypoints
-- ----------------------------
CREATE INDEX "idx_waypoints_map"
ON "map_waypoints" (
  "map_name" ASC
);

PRAGMA foreign_keys = true;
