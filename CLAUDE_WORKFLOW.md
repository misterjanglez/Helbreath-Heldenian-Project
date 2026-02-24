# Workflow Details

## Mode 2: Python Script (BULK — 10+ files)

Use only for large-scale mechanical transforms (e.g., "replace X with Y in every file containing X").

Must explicitly justify: "This touches N files with pattern X, a script is appropriate because Y."

1. Write a script in `Scripts/` that calls `bak.py guard` internally (bak.py is at repo root).
2. Run `--dry-run` — preview changes, full log to `Scripts/output/<script_name>_dry_run.log`.
3. Run `--verify` — scan for collisions (Shared enums, SFMLEngine interfaces, C++ keywords, duplicate targets). Log to `Scripts/output/<script_name>_verify.log`.
4. Review both logs. Fix any flagged collisions.
5. Run without flags to apply.
6. If errors: fix the script itself, re-run. **Never write a second "fix" script.**
7. Once 0 errors: `python bak.py commit` to accept.

## Bulk Script Verification Standards

All Mode 2 scripts must support two verification phases before applying changes:

### `--dry-run` (Quick Preview)
- Full detail output to `Scripts/output/<script_name>_dry_run.log` (no truncation).
- Summary by rename entry — which entries matched and how many times.
- Unused entries report — entries with 0 matches are flagged.
- C++ keyword check — warn if any rename target is a reserved keyword.

### `--verify` (Collision Detection)
- **Shared enum scan** — grep `Sources/Dependencies/Shared/` for old names in enum/struct/class contexts.
- **SFMLEngine interface scan** — grep `Sources/SFMLEngine/` for old names in virtual/override/method declarations.
- **Cross-scope check** — flag old names appearing outside the script's target directory.
- **Duplicate target check** — warn if two old names map to the same new name.
- **Context preview** — show surrounding lines for each flagged collision.
- Output to `Scripts/output/<script_name>_verify.log`.

### Script template

```python
parser.add_argument("--dry-run", action="store_true", help="Preview changes without modifying files")
parser.add_argument("--verify", action="store_true", help="Scan for collisions in Shared/SFMLEngine")
parser.add_argument("--skip-verify", action="store_true", help="Apply without requiring prior --verify")
```

Full specification: `CLAUDE_BULKSCRIPTS_DRYRUN_STANDARDS.md`.

## Regex Safety Rules (for Mode 2 scripts)

- **NEVER** use `::TypeName` as regex — matches inside prefixed names (`sf::Color`). Use `(?<!\w)::TypeName\b`.
- **NEVER** replace inside `#define` macro names — `#define DEF_X` cannot become `#define hb::shared::X`.
- **ALWAYS** use `\b` word boundaries — `PlayerStatus` without `\b` matches inside `PlayerStatusData.h`.
- **ALWAYS** use `(?<!\w)` lookbehind for `::Name` — `::GetPoint2` matches inside `CMisc::GetPoint2`.
- **PREFER** `content.replace("exact_old", "exact_new")` over regex for known patterns.
- **ALWAYS** order replacements longest-first — `DEF_OBJECTMOVE_CONFIRM` before `DEF_OBJECTMOVE`.
- **USE** placeholder approach for substring collisions (`SFMLInput::` contains `Input::`).
- **TEST** on 2-3 files first when dealing with regex patterns.

## Include Ordering Pitfall

`GlobalDef.h` and `RenderConstants.h` both define `LOGICAL_WIDTH()`, `LOGICAL_HEIGHT()`, etc.
`RenderConstants.h` guards behind `#ifndef GLOBALDEF_H_RESOLUTION_FUNCTIONS`.

**Rule**: `GlobalDef.h` must be included BEFORE `IRenderer.h` (which pulls in `RenderConstants.h`).
- In .cpp files: include `MapData.h` or `Game.h` (transitively includes `GlobalDef.h`) before render headers.
- In .h files: use forward declarations for `IRenderer`, `SpriteCollection`, etc.
