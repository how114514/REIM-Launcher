#!/usr/bin/env python3
"""M3 three-Host Contract v3 consistency check (G3 / 5B N1).

Compares the Runtime status embedded in a Preview report, a headless dump and
a UE state report. Only fields owned by the Runtime/Contract are compared;
Host-specific rendering, tick timing and UI canvas fields are excluded.

    python tools/m3-host-consistency.py \
        --preview build/m3/reports/visual/m3-s5-preview.json \
        --headless build/m3/reports/visual/m3-s5-headless.json \
        --ue build/m3/reports/visual/m3-s5-ue-state.json \
        --json build/m3/reports/host-consistency.json

Exit status is 0 only when every compared field matches on all three hosts.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

# (label, accessor) — accessor receives the nested `status` object.
FIELDS = [
    ("abi", lambda s: s.get("abi")),
    ("loaded_chunks", lambda s: s.get("loaded_chunks")),
    ("mesh_sections", lambda s: s.get("mesh_sections")),
    ("block_count", lambda s: s.get("block_count")),
    ("player.x", lambda s: s.get("player", {}).get("x")),
    ("player.y", lambda s: s.get("player", {}).get("y")),
    ("player.z", lambda s: s.get("player", {}).get("z")),
    ("player.yaw", lambda s: s.get("player", {}).get("yaw")),
    ("player.pitch", lambda s: s.get("player", {}).get("pitch")),
    ("player.health", lambda s: s.get("player", {}).get("health")),
    ("player.food", lambda s: s.get("player", {}).get("food")),
    ("player.selected_slot", lambda s: s.get("player", {}).get("selected_slot")),
    ("world.dimension", lambda s: s.get("world", {}).get("dimension")),
    ("world.time_of_day", lambda s: s.get("world", {}).get("time_of_day")),
    ("world.weather", lambda s: s.get("world", {}).get("weather")),
    ("world.worldgen_source", lambda s: s.get("world", {}).get("worldgen_source")),
    ("ui.screen", lambda s: s.get("ui", {}).get("screen")),
    ("gameplay.recipes.crafting", lambda s: s.get("gameplay", {}).get("recipes", {}).get("crafting")),
    ("gameplay.recipes.smelting", lambda s: s.get("gameplay", {}).get("recipes", {}).get("smelting")),
    ("save.state", lambda s: s.get("save", {}).get("state")),
]


def status_of(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    return data.get("status", data)


def equal(a, b) -> bool:
    if isinstance(a, float) or isinstance(b, float):
        try:
            return abs(float(a) - float(b)) <= 1e-6
        except (TypeError, ValueError):
            return False
    return a == b


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", required=True)
    ap.add_argument("--headless", required=True)
    ap.add_argument("--ue", required=True)
    ap.add_argument("--json", default="build/m3/reports/host-consistency.json")
    args = ap.parse_args()

    hosts = {
        "preview": status_of(Path(args.preview)),
        "headless": status_of(Path(args.headless)),
        "ue": status_of(Path(args.ue)),
    }
    rows = []
    for label, getter in FIELDS:
        values = {name: getter(status) for name, status in hosts.items()}
        ok = equal(values["preview"], values["headless"]) and equal(
            values["preview"], values["ue"])
        rows.append({"field": label, "values": values, "match": ok})

    ticks = {name: status.get("tick") for name, status in hosts.items()}
    # The UE and headless reports must be taken at the same fixed tick. Preview
    # is run with --exit-tick for this check; otherwise time-of-day differs.
    tick_match = ticks["ue"] == ticks["headless"] == ticks["preview"]
    # render_level is negotiated per Host (Preview R2, headless/UE pin R0), not
    # shared Runtime state; recorded for transparency but not compared.
    render_levels = {name: status.get("render_level")
                     for name, status in hosts.items()}

    mismatches = [row for row in rows if not row["match"]]
    report = {
        "schema": "reim.m3.host_consistency",
        "format": 1,
        "hosts": {"preview": args.preview, "headless": args.headless, "ue": args.ue},
        "fields": rows,
        "ticks": ticks,
        "tick_match": tick_match,
        "render_levels": render_levels,
        "render_level_note": ("negotiated per Host; excluded from shared-state "
                              "comparison"),
        "mismatches": mismatches,
        "pass": not mismatches and tick_match,
    }
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")

    for row in rows:
        mark = "ok" if row["match"] else "MISMATCH"
        print(f"{mark:8} {row['field']:28} {row['values']}")
    print(f"ticks: {ticks}; ue/headless tick match={tick_match}; "
          f"pass={report['pass']}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
