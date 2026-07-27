# Formula Faithfulness Audit — merged balance vs original Helbreath

**Purpose:** row-by-row comparison of the merged (post-`merge-development`) balance data against the original source (`C:\Users\miste\source\repos\HelbreathServer\HGServer`), so ShadowEvil's final-week rebalance can be ratified or reverted à la carte during Phase 6. Everything in §1–§2 is **data**: apply a row, run console `reload formulas` (or edit `server_config.json` + restart), and play — no rebuild, no client patch.

**Status: full restore set (§4) APPLIED 2026-07-22** — server 0.3.1. The "Current (Dev live)" columns below show the pre-restore merged values for reference; every row remains individually revertible to Dev's value via the tables here + console `reload formulas`.

**Sources:** live `Binaries/Server/gamedata.db` `formulas` table (14 rows) and `server_config.json`, vs original functions read directly from `HGServer/Game.cpp` / `Game.h` on 2026-07-22. Original line refs included.

---

## 1. The 14 formula rows

### Pools

| id | Current (Dev live) | Original (HGServer) | Verdict |
|---|---|---|---|
| `max_hp` | `vit*3 + level*2 + str*0.5 + angelic_str*0.5` | `Vit*3 + Level*2 + (Str+AngStr)/2` — `iGetMaxHP` | **FAITHFUL** (±1 rounding when Str+AngStr is odd) |
| `max_mp` | `mag*2 + angelic_mag*2 + level*2 + int*0.5 + angelic_int*0.5` | `2(Mag+AngMag) + 2·Level + (Int+AngInt)/2` — `iGetMaxMP` | **FAITHFUL** (same rounding nit) |
| `max_sp` | `(str + angelic_str) + level*2` | `2(Str+AngStr) + 2·Level` — `iGetMaxSP` | **DEVIATES — STR term halved** (Dev stamina nerf) |
| `max_load` | `str*5000 + angelic_str*5000 + level*5000` | `(Str+AngStr)*500 + Level*500` @ 100 units/stone — `_iCalcMaxLoad` (Game.cpp:25488) | **FAITHFUL** (unit-adjusted for 1000 units/stone; dagger 200→2000 verified) |

Restore for `max_sp`: `(str + angelic_str) * 2 + level * 2`

### Progression

| id | Current (Dev live) | Original | Verdict |
|---|---|---|---|
| `level_exp` (per-level cost) | `((100 + pow(level, 2))) - 8 + 0.5 * pow(level, 3)` | increment of recursive `iGetLevelExp`: `L * (50 + L*(L/17)*(L/17))`, integer `L/17` | **DEVIATES — Dev's smooth cubic rebalance** |

Restore (this is literally Dev's own **seed** expression; his live tuning replaced it): `level * (50 + level * trunc(level / 17) * trunc(level / 17))`

Per-level cost comparison (original → Dev, ratio):

| Level | Original | Dev cubic | Dev/orig |
|---|---|---|---|
| 10 | 500 | 692 | 1.4× |
| 30 | 2,400 | 14,492 | 6.0× |
| 50 | 12,500 | 65,092 | 5.2× |
| 100 | 255,000 | 510,092 | 2.0× |
| 140 | 1,261,400 | 1,391,692 | 1.1× |

Dev's curve is far grindier in the early–mid game, converging late. This is the single biggest feel-difference of the rebalance.

### Regen rolls

RegenManager (Dev's system) computes each tick as: three rolls of `1d(max_roll)` each clamped to `min_roll`, picks min/max/average of the three with equal probability, then adds a flat `0..variance` bonus (then the original-style `AddHP/MP/SP %` and hunger machinery, which are structurally mirrored). The original was a **single** roll per tick. With matching knobs the *means* align to the original; the spread shape differs slightly (code, not data).

| Stat | Current knobs (max / min / variance) | Original (single roll) | Verdict |
|---|---|---|---|
| HP | `vit` / `vit/2` / `30*vit/(vit+60)` | `1d(Vit)` **clamped to ≥ Vit/2** + HPstock (`TimeHitPointsUp`, Game.cpp:52917) | **NEAR-FAITHFUL** — ceiling *and* floor are exactly original; the variance bonus (~+9 mean at Vit 100 → ≈ +15% regen) is Dev's addition |
| MP | `mag+angelic_mag` / `(mag+angelic_mag)/2` / `30M/(M+60)` | `1d(Mag+AngMag)`, **no floor** (`TimeManaPointsUp`) | **DEVIATES** — floor + variance push mean from ~M/2 to ~3M/4 + bonus (≈ 1.7× per tick at Mag 100) |
| SP | `floor(vit*0.15)+5` / `floor(vit*0.05)+1` / `30v/(v+500)` | `1d(Vit/3)` **+ level band: +15 (L≤20), +10 (L≤40), +5 (L≤60)** (`TimeStaminarPointsUp`, v2.03 mechanic) | **DEVIATES** — different curve, and the original low-level band is gone entirely |

Restores (mean-faithful; spread shape stays RegenManager's):
- HP: `hp_regen_roll_variance` → `0` (keep max/min as-is)
- MP: `mp_regen_min_roll` → `1`, `mp_regen_roll_variance` → `0`
- SP: `sp_regen_max_roll` → `vit / 3`, `sp_regen_min_roll` → `1`, `sp_regen_roll_variance` → `0`

**Not restorable by data:** the SP level band. The `sp_regen_*` formula calls only receive `vit` (RegenManager.cpp:218–223); restoring the band needs `level` added as an input (one-line signature change), after which the band can live in the variance expression — or a small in-code band behind a config flag. Optional follow-up; flag if low-level stamina feels starved in Phase 6.

## 2. Regen cadence (`server_config.json` → `timing`)

| Key | Current | Original define (Game.h:67–70) | Verdict |
|---|---|---|---|
| `stamina_regen_ms` | 10000 | `DEF_SPUPTIME` 10000 | **FAITHFUL** |
| `health_regen_ms` | 15000 | `DEF_HPUPTIME` 15000 | **FAITHFUL** |
| `mana_regen_ms` | 15000 | `DEF_MPUPTIME` **20000** | **DEVIATES — MP ticks 25% faster** (compounds with the MP roll deviation: net MP regen ≈ 2.2× original) |

Restore: `mana_regen_ms` → `20000`. (The hunger-based `iPlusTime` delay of the original is mirrored by RegenManager's hunger delay.)

## 3. Constants (code — no restore needed unless noted)

| Constant | Value | Status |
|---|---|---|
| `swing_str_divisor` (BalanceConstants.h) | 13 | original-verified (weaponSpeed − Str/13; Feb-era + July frozen-fix audits) |
| `swing_frames` / `base_frame_time` / `delay_per_frame` / `run_frame_time` | 8 / 78 / 12 / 39 | derived from original client animation timing (Feb-era); spot-check optional |
| `weight_units_per_stone` | 1000 | Dev's precision rework; capacity-neutral (verified via max_load) |
| `angelic_bonus` | 16 | not audited against original — spot-check optional |
| `equip_str_threshold` | 11 stones | not audited against original — spot-check optional |
| ServerConfig `character`: base 10, creation +0..4, pool 10 (→70 total), levelup +3 | — | matches original creation/level-up conventions |

## 4. À la carte restore set

Apply any subset to the live DB, then console `reload formulas` (config keys need a restart). Full set for maximum original fidelity:

```sql
UPDATE formulas SET expression='(str + angelic_str) * 2 + level * 2'                                   WHERE formula_id='max_sp';
UPDATE formulas SET expression='level * (50 + level * trunc(level / 17) * trunc(level / 17))'          WHERE formula_id='level_exp';
UPDATE formulas SET expression='0'        WHERE formula_id='hp_regen_roll_variance';
UPDATE formulas SET expression='1'        WHERE formula_id='mp_regen_min_roll';
UPDATE formulas SET expression='0'        WHERE formula_id='mp_regen_roll_variance';
UPDATE formulas SET expression='vit / 3'  WHERE formula_id='sp_regen_max_roll';
UPDATE formulas SET expression='1'        WHERE formula_id='sp_regen_min_roll';
UPDATE formulas SET expression='0'        WHERE formula_id='sp_regen_roll_variance';
```
```json
"mana_regen_ms": 20000
```

**On ratification** (whatever subset is chosen): mirror into `Scripts/setup_gamedata.py`, dump a fresh `gameconfigs-*.sql` snapshot, changelog entry. Existing characters need no migration for any of these (XP totals are compared against the recomputed curve at runtime; over-cap SP just clamps down—same as the login stat-clamp behavior).

## 5. Recommendation

For the project mission (original fundamental mechanics): **restore all DEVIATES rows** — `max_sp`, `level_exp`, the MP/SP regen knobs, HP variance, and `mana_regen_ms`. They are independently togglable and hot-reloadable, so they can be A/B-felt live during the Phase-6 play pass; keep any of Dev's values that play better as *deliberate, documented* deviations rather than inherited ones. The SP level band is the one item needing a small code change — defer unless low-level play feels stamina-starved.
