"""Replace pak title-strip draws with ui_theme::header calls.

Sheet 9 of game_dialogs.pak carries 22 "title strip" frames — one small sprite
per dialog with the lettering painted into it. Those frames are too small for
the size-gated flat-panel swap in CGame::draw_new_dialog_box to touch, so every
dialog still showed a brass title bar on an otherwise flat panel. This rewrites
each call site to draw a themed header with a real string instead.

    draw_new_dialog_box(InterfaceNdText, sX, sY, 6);
    -> hb::client::ui_theme::header(sX, sY, m_size_x, UI_TITLE_SYSTEM_MENU);

Frame 0 is NOT a title — it is the whole 270x376 character panel body — so
DialogBox_Character.cpp is excluded. Overlay_* files are excluded too: they are
not IDialogBox subclasses and have no m_size_x to size the header against.

    python Scripts/migrate_dialog_titles.py --dry-run
    python Scripts/migrate_dialog_titles.py --verify
    python Scripts/migrate_dialog_titles.py
"""
import argparse
import re
import sys
from pathlib import Path

CLIENT = Path('Sources/Client')
OUTPUT = Path('Scripts/output')

# Frame index -> lan_eng macro, transcribed from the art in each frame.
TITLES = {
	1:  'UI_TITLE_SKILLS',
	2:  'UI_TITLE_LEVEL_UP_SETTING',
	3:  'UI_TITLE_PARTY_MENU',
	4:  'UI_TITLE_QUEST',
	5:  'UI_TITLE_ITEM_UPGRADE',
	6:  'UI_TITLE_SYSTEM_MENU',
	7:  'UI_TITLE_MAGIC_LIST',
	8:  'UI_TITLE_TINKERING',
	9:  'UI_TITLE_EXCHANGE_ITEM',
	10: 'UI_TITLE_REPAIR_ITEM',
	11: 'UI_TITLE_ITEM_FOR_SALE',
	12: 'UI_TITLE_USER_AGREEMENT',
	13: 'UI_TITLE_CHANGE_PASSWORD',
	14: 'UI_TITLE_SPELLS',
	15: 'UI_TITLE_COMMANDER_MENU',
	16: 'UI_TITLE_CONSTRUCTOR_MENU',
	17: 'UI_TITLE_SOLDIER_MENU',
	18: 'UI_TITLE_CITYHALL_MENU',
	19: 'UI_TITLE_GUILD_MENU',
	20: 'UI_TITLE_NOTICE',
	21: 'UI_TITLE_ITEMS_IN_STORAGE',
	22: 'UI_TITLE_CHAT_HISTORY',
}

EXCLUDED = {'DialogBox_Character.cpp'}          # frame 0 is the panel body

CALL = re.compile(
	r'(?P<indent>[ \t]*)(?:m_game->)?draw_new_dialog_box\('
	r'InterfaceNdText,\s*(?P<x>[A-Za-z_][A-Za-z0-9_]*),\s*(?P<y>[A-Za-z_][A-Za-z0-9_]*),\s*'
	r'(?P<frame>\d+)\s*(?:,[^;]*?)?\);')

NEEDED_INCLUDES = ('#include "UITheme.h"', '#include "lan_eng.h"')


def targets():
	for path in sorted(CLIENT.glob('DialogBox_*.cpp')):
		if path.name in EXCLUDED:
			continue
		yield path


def ensure_includes(text):
	"""Add any missing include after the last existing #include."""
	added = []
	for inc in NEEDED_INCLUDES:
		if inc in text:
			continue
		added.append(inc)
	if not added:
		return text, added

	lines = text.split('\n')
	last = max(i for i, l in enumerate(lines) if l.startswith('#include'))
	lines[last + 1:last + 1] = added
	return '\n'.join(lines), added


def convert(path):
	"""Return (new_text, [(lineno, before, after)], added_includes) or None."""
	original = path.read_text(encoding='utf-8')
	edits = []

	def repl(m):
		frame = int(m.group('frame'))
		if frame not in TITLES:
			return m.group(0)
		new = (f"{m.group('indent')}hb::client::ui_theme::header("
		       f"{m.group('x')}, {m.group('y')}, m_size_x, {TITLES[frame]});")
		edits.append((m.group(0).strip(), new.strip()))
		return new

	text = CALL.sub(repl, original)
	if not edits:
		return None

	text, added = ensure_includes(text)
	return text, edits, added


def verify():
	"""Pre-flight checks that would make the rewrite unsafe."""
	problems = []

	missing = [m for m in TITLES.values()
	           if m not in Path('Sources/Client/lan_eng.h').read_text(encoding='utf-8')]
	if missing:
		problems.append(f'lan_eng.h is missing macros: {", ".join(missing)}')

	for path in targets():
		text = path.read_text(encoding='utf-8')
		for m in CALL.finditer(text):
			frame = int(m.group('frame'))
			if frame not in TITLES:
				problems.append(f'{path.name}: frame {frame} has no title mapping')
		# m_size_x only exists on IDialogBox subclasses.
		if CALL.search(text) and 'IDialogBox' not in text and ': IDialogBox' not in text:
			if 'm_size_x' not in text:
				problems.append(f'{path.name}: no m_size_x in scope for header width')

	return problems


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument('--dry-run', action='store_true', help='preview; write nothing')
	ap.add_argument('--verify', action='store_true', help='pre-flight checks only')
	args = ap.parse_args()

	if args.verify:
		problems = verify()
		for p in problems:
			print('  PROBLEM:', p)
		print(f'\n{len(problems)} problem(s).')
		return 1 if problems else 0

	OUTPUT.mkdir(parents=True, exist_ok=True)
	log = OUTPUT / 'dialog_titles.log'
	total_files = total_edits = 0

	with log.open('w', encoding='utf-8') as f:
		for path in targets():
			result = convert(path)
			if result is None:
				continue
			text, edits, added = result
			total_files += 1
			total_edits += len(edits)

			print(f'{path.name}: {len(edits)} title(s)'
			      + (f', +{len(added)} include(s)' if added else ''))
			f.write(f'\n=== {path}\n')
			for inc in added:
				f.write(f'  + {inc}\n')
			for before, after in edits:
				f.write(f'  - {before}\n  + {after}\n')

			if not args.dry_run:
				path.write_text(text, encoding='utf-8')

	verb = 'would change' if args.dry_run else 'changed'
	print(f'\n{verb} {total_edits} call site(s) across {total_files} file(s)')
	print(f'detail log: {log}')
	return 0


if __name__ == '__main__':
	sys.exit(main())
