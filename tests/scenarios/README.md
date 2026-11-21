# M3 scenario suite

Status (2026-11-21, stage 5B): **68 authored, 68 pass headless**
(5 A + 12 INT + 12 INV + 6 UI + 8 SUR + 10 MOB + 8 WLD + 4 RES + 2 R2 +
UI-07 counted in the 7 UI). The M3_PLAN §1.6 first-release contract is
**62 categories**, and SAV-01..06 are still not authored because B1
WorldWriter is not implemented:

```text
required 62-category suite: 56 / 62 = 90.3%; not_run = 6; gate = FAIL
```

Do not read "68/68 implemented scenes pass" as G3 passing. The accounting is
produced by `python tools/m3-scenario-accounting.py --reports
build/m3/reports/scenarios-s5`.

Layout:

```text
tests/scenarios/
  m3a/  M3A-UI-01..03, M3A-ACT-01, M3A-A6-01   phase 1 scaffolding
  m3d/  INT-01..12                              interaction/items
  m3e/  INV-01..12                              inventory/containers/crafting
  m3h/  UI-01..06                               HUD/screens/input
  m3f/  SUR-01..08, MOB-01..10, UI-07           survival/mobs/combat/death UI
  m3g/  WLD-01..08                              new world/worldgen/time/weather/nether
  m3i/  RES-01..04                              resource/data packs, audio, language
  m3j/  R2-01..02                               particles/weather/render_level
  SAV-01...SAV-06   (phase 1 B / 5B; not authored until B1 exists)
```

WLD scenarios use `create_world` + `seed` with an in-memory world; run them
with a disposable `--world` directory (e.g. `build/m3/worlds/m3g`), no
level.dat is written. SUR/MOB scenarios use the real save copy and only
mutate Runtime memory, except `MOB-10` (natural spawning) which declares
`create_world` and generates in memory as well.

## World fixture for phase 2

INT/INV/UI scenarios do not depend on natural terrain. They spawn at a fixed
point and build a small two-layer stone platform through `setup`, then use
`probes` to assert block state:

```json
{
  "spawn": [16.5, 100.0, 80.5],
  "spawn_yaw": 0.0,
  "spawn_pitch": 90.0,
  "setup": [
    { "action": "clear_inventory" },
    { "action": "give", "item": "minecraft:stone_pickaxe", "count": 1 },
    { "action": "set_block", "x": 17, "y": 99, "z": 80, "block": "minecraft:stone" }
  ],
  "probes": [[17, 99, 80]]
}
```

`setup`/`probes` are diagnostic scenario config passed to the Runtime; they do
not exist in Shipping. State changes stay in memory until M3-B1 adds the save
writer.

## DSL

```json
{
  "id": "INV-02",
  "ui_canvas": [1280, 720, 2],
  "render_distance": 2,
  "timeout_ms": 20000,
  "steps": [
    { "wait_ticks": 3 },
    { "input": { "key": "e", "action": "press" } },
    { "action": { "type": "slot_click", "screen": 3, "a": 36, "b": 0 } },
    { "action": { "type": "break_block", "x": 17, "y": 99, "z": 80 } },
    { "fetch_frames": 2 }
  ],
  "assert": [
    { "type": "screen_equals", "value": 3 },
    { "type": "slot_item", "slot": 0, "item": "minecraft:oak_planks", "count": 4 },
    { "type": "inventory_count", "item": "minecraft:oak_planks", "min": 4 },
    { "type": "block_at", "x": 17, "y": 99, "z": 80, "state": "minecraft:air" }
  ]
}
```

- Input: `wait_ticks`, `input` (key press/release), `action` (Contract intent
  with `screen/a/b/c/x/y/z/screen_x/screen_y/wheel/buttons`), `fetch_frames`,
  and an inline `assert` step evaluated mid-scenario (e.g. verify the nether
  before returning).
- Config: `create_world`, `seed`, `time`, `weather`, `dimension`,
  `spawn`, `probes`, `render_distance`, `gamemode_id`, `difficulty_id`.
- Setup: `give`, `set_block`, `platform`, `clear_inventory`, `set_health`,
  `set_food`, `set_air`, `set_xp`, `damage_player`, `spawn_entity`,
  `clear_entities`, `natural_spawn`, `keep_inventory`.
- Actions: `pointer`, `slot_click`, `select_slot`, `drop_item`,
  `break_start/stop`, `break_block`, `place_block`, `use_block`, `use_item`,
  `attack`, `pick_block`, `use_start/stop`, `drag_start/to/end`, `wheel`,
  `close_screen`, `respawn`, `debug_set_time`, `debug_set_weather`.
- Assertions: `status_equals`, `status_at_least`, `inventory_count`,
  `slot_item`, `held_item`, `block_at`, `biome_at`, `screen_equals`,
  `drops_at_least`, `target_block`, `ui_min_draws`, `ui_kind`, `ui_has_clip`,
  `ui_has_item_icon`, `ui_slots`, `ui_text_contains`, `ui_cursor_mode`,
  `entities_at_least`, `entity_parts_at_least`, `audio_sound_seen`,
  `entity_type_count`, `entity_count_at_least`, `entity_type_health_at_most`,
  `entity_distance_at_least`, `player_health_at_most/least`,
  `player_food_at_most`, `player_air_at_most`, `player_xp_total_at_least`,
  `player_xp_level_at_least`, `player_dead_equals`, `damage_taken_at_least`,
  `entity_target_type`.
- Slot numbers are screen-relative: HUD hotbar 0–8; inventory result 0,
  2×2 grid 1–4, main 9–35, hotbar 36–44; crafting table result 0, grid 1–9,
  main 10–36, hotbar 37–45; furnace input/fuel/output 0–2, main 3–29,
  hotbar 30–38; chest 0–26, main 27–53, hotbar 54–62.
- The world is `--world` or `REIM_TEST_WORLD`; scenarios never point at
  `.minecraft/saves` directly. Without a world the report is `not_run`.

Run:

```bat
set REIM_TEST_WORLD=<save-copy>
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3d ^
  --out build\m3\reports\scenarios-m3d --generated generated\26.3 ^
  --dll build\m3\bin\mc_26.3.dll
```

Rules from `docs/M3_PLAN.md` §1.6:

- A scenario is deterministic: fixed world/fixture, seed, time, weather and
  input ticks.
- Core gameplay scenarios must run headless/Preview; UE scenarios only cover
  UI/visual/audio/packaging.
- `not_run` is not `pass`. The pass rate denominator is the full suite (62).
- A new feature must add a scenario; deleting/downgrading one needs a written
  reason in `docs/M3_ACCEPTANCE.md`.
- SUR is owned by M3-F (F10), WLD by M3-G (G10).
