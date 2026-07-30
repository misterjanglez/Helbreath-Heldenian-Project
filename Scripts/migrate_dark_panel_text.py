#!/usr/bin/env python3
"""
migrate_dark_panel_text.py - repoint text that is still drawn black onto the
now-dark dialog panels.

`GameColors::UIBlack` was never inverted with the rest of the palette (see
CommonTypes.h) because it is also a real black: drop shadows behind HUD numbers,
sprite tint parameters, the minimap. But it is *also* the colour ~30 dialogs draw
body text in, and the default argument of both put_aligned_string overloads —
which is why roughly 170 colourless calls render black on a near-black panel.

So this cannot be a palette edit; it has to be a call-site sweep, and it has to
tell text apart from the other uses. The filter is the call itself: only lines
that draw text (put_string / put_aligned_string / from_color / item_name_color)
are rewritten, so `DrawParams::tinted_alpha(UIBlack.r, ...)` and friends are left
where they are.

Three deliberate exclusions:
  * Screen_*.cpp — the menu screens draw on painted parchment, where black is
    correct and white would be unreadable. They are not flat panels.
  * DialogBox_HudPanel.cpp — every UIBlack in it is a drop shadow drawn at +1,+1
    under a white number, on brass that was never flattened.
  * DialogBox_Skill.cpp maps to UIDisabled, not UILabel: its black meant "this
    skill is not usable", and UILabel would make it look available.

Usage:
    python Scripts/migrate_dark_panel_text.py --dry-run
    python Scripts/migrate_dark_panel_text.py --verify
    python Scripts/migrate_dark_panel_text.py
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLIENT = ROOT / 'Sources' / 'Client'
OUT = ROOT / 'Scripts' / 'output'

# Files whose UIBlack is not dark-panel body text.
SKIP = {'DialogBox_HudPanel.cpp'}

# Files whose black meant "unavailable", not "body text".
DISABLED_INSTEAD = {'DialogBox_Skill.cpp'}

# A line only counts if it is actually drawing text.
TEXT_CALL = re.compile(r'put_string\(|put_aligned_string\(|from_color\(|item_name_color\(')

DEFAULT_DECL = 'const hb::shared::render::Color& color = GameColors::UIBlack'
DEFAULT_NEW = 'const hb::shared::render::Color& color = GameColors::UILabel'


def targets():
    for p in sorted(CLIENT.glob('DialogBox_*.cpp')) + sorted(CLIENT.glob('Overlay_*.cpp')):
        if p.name not in SKIP:
            yield p


def plan():
    """Return (path, [(lineno, old, new)]) for every file that changes."""
    changes = []
    for p in targets():
        replacement = ('GameColors::UIDisabled' if p.name in DISABLED_INSTEAD
                       else 'GameColors::UILabel')
        lines = p.read_text(errors='replace').split('\n')
        hits = []
        for i, line in enumerate(lines):
            if 'GameColors::UIBlack' not in line or not TEXT_CALL.search(line):
                continue
            hits.append((i + 1, line, line.replace('GameColors::UIBlack', replacement)))
        if hits:
            changes.append((p, hits))

    # The two put_aligned_string defaults, which cover the colourless callers.
    for rel in ('IDialogBox.h', 'IGameScreen.h'):
        p = CLIENT / rel
        lines = p.read_text(errors='replace').split('\n')
        hits = [(i + 1, l, l.replace(DEFAULT_DECL, DEFAULT_NEW))
                for i, l in enumerate(lines) if DEFAULT_DECL in l]
        if hits:
            changes.append((p, hits))
    return changes


def verify():
    """Report what the filter deliberately leaves behind, so it can be eyeballed."""
    problems = 0
    print('--- UIBlack left in place (expected: tints, shadows, painted screens) ---')
    for p in sorted(CLIENT.glob('*.cpp')) + sorted(CLIENT.glob('*.h')):
        for i, line in enumerate(p.read_text(errors='replace').split('\n')):
            if 'GameColors::UIBlack' not in line:
                continue
            if p.name.startswith(('DialogBox_', 'Overlay_')) and p.name not in SKIP \
                    and TEXT_CALL.search(line):
                continue   # this one gets rewritten
            kind = ('painted menu screen' if p.name.startswith('Screen_')
                    else 'drop shadow / HUD' if p.name in SKIP
                    else 'not a text call')
            print(f'  {p.name}:{i + 1}  [{kind}]  {line.strip()[:96]}')
            if kind == 'not a text call' and TEXT_CALL.search(line):
                problems += 1
    print(f'--- {problems} unexplained ---')
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--verify', action='store_true')
    args = ap.parse_args()

    if args.verify:
        sys.exit(1 if verify() else 0)

    changes = plan()
    total = sum(len(h) for _, h in changes)

    if args.dry_run:
        OUT.mkdir(parents=True, exist_ok=True)
        log = OUT / 'dark_panel_text.log'
        with log.open('w', encoding='utf-8') as f:
            for p, hits in changes:
                f.write(f'=== {p.relative_to(ROOT)} ({len(hits)}) ===\n')
                for lineno, old, new in hits:
                    f.write(f'  {lineno}\n  - {old.strip()}\n  + {new.strip()}\n')
        for p, hits in changes:
            print(f'{p.name:38} {len(hits):3}')
        print(f'\n{total} line(s) across {len(changes)} file(s). Full detail: {log}')
        return

    for p, hits in changes:
        lines = p.read_text(errors='replace').split('\n')
        for lineno, _, new in hits:
            lines[lineno - 1] = new
        p.write_text('\n'.join(lines))
        print(f'{p.name:38} {len(hits):3} rewritten')
    print(f'\n{total} line(s) across {len(changes)} file(s).')


if __name__ == '__main__':
    main()
