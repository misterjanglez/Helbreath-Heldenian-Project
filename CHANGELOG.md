# Full sprite directory reorganization and holiday tile system

Moved all 82 NPC pak files into a dedicated sprites/npcs/ directory with descriptive names, replacing cryptic abbreviations. Added Christmas holiday tile variant that swaps in automatically when the server enables xmas weather.

Moved all 18 effect pak files into sprites/effects/ directory, renaming crusade and abaddon effect files for clarity.

Organized all remaining sprite paks into subdirectories: tiles/, objects/, players/, pets/, interface/, and items/. Sprites root is now empty — all paks live in categorized folders. Trimmed unused sprite data from item-equipm/w paks and converted legacy Sprites::create calls to open_pak to fix subdirectory path resolution.

Fixed gold drop effect skipping the invisible animation frames so the gold appears on the ground immediately.