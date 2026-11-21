# M3 scenario fixtures

This directory holds **generation scripts and hashes**, not user saves or
large world binaries.

Rules:

- A fixture is defined by a fixed seed, generator version, time/weather and
  inventory setup; the same inputs must produce the same world.
- Generated worlds go under `build/m3/fixtures/` (gitignored), never here.
- Do not point scenarios at `%APPDATA%`/`.minecraft/saves`; user saves are for
  the SAV round-trip tests only, and must be copied and hash-verified first.
- When a fixture changes, update its hash and the affected scenarios in
  `tests/scenarios/` in the same commit.

Planned fixtures (S1 A8 / S2 B7):

| id | purpose |
|---|---|
| `m3-basic` | flat-ish survival start; mining/crafting/container scenarios |
| `m3-lighting` | cave/light/torch edits |
| `m3-worldgen` | fixed-seed chunk samples for G8 |
| `m3-player` | save with `players/data` for B3 round-trip |
| `m3-nethersave` | save with an existing Nether region for G6 |
| `m3-compression` | gzip/zlib/none/LZ4/.mcc variants for SAV-04 |

None of these exist yet; create them with the S1/S2 scripts, not by copying a
user world.
