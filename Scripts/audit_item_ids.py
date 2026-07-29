"""Audit the ItemId:: namespace in ItemEnums.h against the shipped items table.

Every ItemId:: constant names an item. This checks that the row actually
living at that id is the item the constant claims, so a constant can never
again silently point at an unrelated item (issue #71: ZemstoneofSacrifice
was 753, which is Wizard Hat (M)).

Constant names are CamelCase run-together spellings of the original item
data; the shipped names are the display names. Most pairs match after
normalising (case, spaces, punctuation, "Plus" for '+'). The ones that
diverge for real -- possessives, spelling fixes, expanded abbreviations --
are listed in KNOWN_ALIASES with their exact expected name, so a rename on
either side fails the audit rather than passing on a fuzzy score.

    python Scripts/audit_item_ids.py          # audit, exit 1 on any failure
    python Scripts/audit_item_ids.py --all    # also list every matching pair

Exit code 0 clean, 1 on any mismatch or missing row.
"""

import argparse
import difflib
import os
import re
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEM_ENUMS_H = os.path.join(ROOT, 'Sources', 'Dependencies', 'Shared', 'Item', 'ItemEnums.h')
DEFAULT_DB = os.path.join(ROOT, 'Binaries', 'Server', 'gamedata.db')

# Constant name -> exact shipped name, for pairs that do not normalise equal.
# Each entry is a reviewed statement that the two names mean the same item.
KNOWN_ALIASES = {
	'Excaliber':            'Excalibur',                        # misspelling kept from the original headers
	'AresdenHeroCape':      "Aresden-Hero's Cape",              # possessive
	'ElvineHeroCape':       "Elvine-Hero's Cape",
	'AresdenHeroCapePlus1': "Aresden-Hero's Cape +1",
	'ElvineHeroCapePlus1':  "Elvine-Hero's Cape +1",
	'XelimaBlade':          "Xelima's Blade",
	'XelimaAxe':            "Xelima's Axe",
	'XelimaRapier':         "Xelima's Rapier",
	'DarkElfBow':           "Dark Elf's Bow",
	'MerienShield':         "Merien's Shield",
	'MerienPlateMailM':     "Merien's Plate Mail (M)",
	'MerienPlateMailW':     "Merien's Plate Mail (W)",
	'RingofArcmage':        'Ring of Arch Mage',                # Arcmage -> Arch Mage
	'BerserkWandMS20':      'Berserk Magic Wand (MS.20)',       # Wand -> Magic Wand
	'BerserkWandMS10':      'Berserk Magic Wand (MS.10)',
	'KlonessWandMS20':      'Kloness Magic Wand (MS.20)',
	'KlonessWandMS10':      'Kloness Magic Wand (MS.10)',
	'ResurWandMS20':        'Resurrection Magic Wand (MS.20)',  # Resur -> Resurrection
	'ResurWandMS10':        'Resurrection Magic Wand (MS.10)',
	'AcientTabletLU':       'An Ancient Piece of Stone (UL)',   # LU/LD/RU/RD == UL/LL/UR/LR
	'AcientTabletLD':       'An Ancient Piece of Stone (LL)',
	'AcientTabletRU':       'An Ancient Piece of Stone (UR)',
	'AcientTabletRD':       'An Ancient Piece of Stone (LR)',
	'DarkKnightSword':      'Dark Knight Templar',              # noted at the constant
}


def normalize(name):
	"""Lowercase, drop 'Plus', strip everything that is not alphanumeric."""
	return re.sub(r'[^a-z0-9]', '', name.lower().replace('plus', ''))


def spellings(constant):
	"""Normalised spellings of a constant name the shipped name might use."""
	base = normalize(constant)
	return {
		base,
		re.sub(r'p(\d)', r'\1', base),          # DFp10 -> df10 ('p' as '+')
		base.replace('acient', 'ancient'),      # misspelling kept from the original headers
	}


def read_constants(header_path):
	"""[(name, id)] for every constant in the ItemId namespace, in file order."""
	source = open(header_path, encoding='utf-8').read()
	match = re.search(r'namespace ItemId\s*\{(.*?)\n\}', source, re.S)
	if match is None:
		raise SystemExit(f'{header_path}: no ItemId namespace found')
	return [(name, int(value)) for name, value
	        in re.findall(r'constexpr\s+short\s+(\w+)\s*=\s*(\d+)\s*;', match.group(1))]


def nearest_rows(constant, rows, count=3):
	"""The shipped rows whose names read closest to this constant name."""
	wanted = spellings(constant)

	def score(row):
		return max(difflib.SequenceMatcher(None, s, normalize(row[1])).ratio()
		           for s in wanted)
	return sorted(rows.items(), key=score, reverse=True)[:count]


def audit(db_path, show_all):
	constants = read_constants(ITEM_ENUMS_H)
	rows = dict(sqlite3.connect(db_path).execute('select item_id, name from items'))

	claimed = {}
	for name, item_id in constants:
		claimed.setdefault(item_id, []).append(name)
	duplicates = {i: names for i, names in claimed.items() if len(names) > 1}

	failures = []
	for name, item_id in constants:
		shipped = rows.get(item_id)
		if shipped is None:
			failures.append((name, item_id, None, 'no items row at this id'))
			continue

		alias = KNOWN_ALIASES.get(name)
		if alias is not None:
			if shipped == alias:
				if show_all:
					print(f'  alias  {name:28s} {item_id:5d}  {shipped}')
			else:
				failures.append((name, item_id, shipped,
				                 f'known alias expects {alias!r}'))
			continue

		normalized = normalize(shipped)
		if any(s == normalized or s in normalized or normalized in s for s in spellings(name)):
			if show_all:
				print(f'  ok     {name:28s} {item_id:5d}  {shipped}')
		else:
			failures.append((name, item_id, shipped, 'name does not match the row at this id'))

	print(f'\n{len(constants)} ItemId constants checked against {len(rows)} shipped items')

	if duplicates:
		print(f'\n{len(duplicates)} id(s) claimed by more than one constant:')
		for item_id, names in sorted(duplicates.items()):
			print(f'  {item_id:5d}  {", ".join(names)}')

	if failures:
		print(f'\n{len(failures)} mismatch(es):\n')
		for name, item_id, shipped, why in failures:
			print(f'  {name} = {item_id}  ->  {shipped!r}')
			print(f'      {why}')
			for row_id, row_name in nearest_rows(name, rows):
				print(f'      closest shipped name: {row_id:5d}  {row_name!r}')
			print()
		print('Fix the constant, or add a reviewed entry to KNOWN_ALIASES if the two')
		print('names really do mean the same item.')
	elif not duplicates:
		print('No mismatches.')

	return 1 if failures or duplicates else 0


def main():
	parser = argparse.ArgumentParser(description=__doc__,
	                                 formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument('--db', default=DEFAULT_DB,
	                    help=f'gamedata.db path (default {DEFAULT_DB})')
	parser.add_argument('--all', action='store_true',
	                    help='list every constant, not just the mismatches')
	args = parser.parse_args()
	return audit(args.db, args.all)


if __name__ == '__main__':
	sys.exit(main())
