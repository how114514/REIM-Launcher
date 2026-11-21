# M3 scenario suite

Status: **0 / 62** (not started).

Layout:

```text
tests/scenarios/
  INT-01.json ... INT-10.json
  INV-01.json ... INV-12.json
  SUR-01.json ... SUR-08.json
  MOB-01.json ... MOB-08.json
  WLD-01.json ... WLD-08.json
  SAV-01.json ... SAV-06.json
  UI-01.json  ... UI-06.json
  RES-01.json ... RES-04.json
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
