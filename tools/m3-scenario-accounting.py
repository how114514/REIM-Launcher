#!/usr/bin/env python3
"""M3 scenario pass-rate accounting (M3_PLAN §1.6 / 5B N8).

Counts the first-release 62-scenario category contract against the authored
scenarios and a scenario report directory. The denominator is the fixed 62
release categories, not the number of extra scenarios authored.

    python tools/m3-scenario-accounting.py \
        --scenarios tests/scenarios \
        --reports build/m3/reports/scenarios-s5 \
        --json build/m3/reports/scenario-accounting.json

A missing report file is `not_run`; a report with status `fail` is a failure.
Extra scenarios beyond a category's first-release count are reported separately
and do not change the 62 denominator. Exit status is 0 only when the gate
(pass/62 >= 95%, not_run <= 3, no core category entirely not_run) is met.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

# M3_PLAN §1.6 first-release suite. Order is the table order.
REQUIRED = {
    "INT": 10,
    "INV": 12,
    "SUR": 8,
    "MOB": 8,
    "WLD": 8,
    "SAV": 6,
    "UI": 6,
    "RES": 4,
}
CORE = ("INT", "INV", "SUR", "SAV", "UI")
ID_RE = re.compile(r"^([A-Z]+)-\d+")


def read_reports(report_dir: Path) -> dict[str, str]:
    status: dict[str, str] = {}
    if not report_dir.exists():
        return status
    for path in sorted(report_dir.rglob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        sid = data.get("id") or path.stem
        status[str(sid)] = str(data.get("status", "not_run"))
    return status


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenarios", default="tests/scenarios")
    ap.add_argument("--reports", default="build/m3/reports/scenarios-s5")
    ap.add_argument("--json", default="build/m3/reports/scenario-accounting.json")
    args = ap.parse_args()

    status = read_reports(Path(args.reports))
    by_cat: dict[str, list[str]] = {cat: [] for cat in REQUIRED}
    extras: list[str] = []
    for path in sorted(Path(args.scenarios).rglob("*.json")):
        sid = path.stem
        match = ID_RE.match(sid)
        if not match:
            continue
        cat = match.group(1)
        if cat not in REQUIRED:
            extras.append(sid)
            continue
        by_cat[cat].append(sid)

    rows = []
    total_pass = 0
    total_not_run = 0
    for cat, required in REQUIRED.items():
        ids = sorted(by_cat[cat], key=lambda s: int(s.rsplit("-", 1)[1]))
        counted = ids[:required]
        missing = required - len(counted)
        passed = sum(1 for sid in counted if status.get(sid) == "pass")
        failed = sum(1 for sid in counted if status.get(sid) == "fail")
        not_run = missing + sum(
            1 for sid in counted if status.get(sid) not in ("pass", "fail"))
        extra = ids[required:]
        total_pass += passed
        total_not_run += not_run
        rows.append({
            "category": cat,
            "required": required,
            "authored": len(ids),
            "counted": len(counted),
            "pass": passed,
            "fail": failed,
            "not_run": not_run,
            "extra_scenarios": extra,
        })

    denominator = sum(REQUIRED.values())
    rate = total_pass / denominator if denominator else 0.0
    core_all_missing = [
        row["category"] for row in rows
        if row["category"] in CORE and row["pass"] == 0
    ]
    gate = (rate >= 0.95 and total_not_run <= 3 and not core_all_missing)

    # Extras that actually ran (reported separately for transparency).
    extra_pass = sum(1 for sid in extras if status.get(sid) == "pass")
    extra_fail = sum(1 for sid in extras if status.get(sid) == "fail")

    report = {
        "schema": "reim.m3.scenario_accounting",
        "format": 1,
        "denominator": denominator,
        "required": REQUIRED,
        "rows": rows,
        "pass": total_pass,
        "not_run": total_not_run,
        "pass_rate": round(rate, 4),
        "gate_95_percent": rate >= 0.95,
        "gate_not_run_le_3": total_not_run <= 3,
        "core_categories_all_not_run": core_all_missing,
        "gate_pass": gate,
        "extras": {
            "count": len(extras),
            "pass": extra_pass,
            "fail": extra_fail,
            "ids": sorted(extras),
        },
        "executed_total": {
            "pass": sum(1 for v in status.values() if v == "pass"),
            "fail": sum(1 for v in status.values() if v == "fail"),
            "not_run": sum(1 for v in status.values() if v not in ("pass", "fail")),
        },
        "report_dir": str(Path(args.reports)),
    }
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")

    print(f"{'cat':4} {'req':>3} {'auth':>4} {'pass':>4} {'fail':>4} {'n/r':>4}")
    for row in rows:
        print(f"{row['category']:4} {row['required']:>3} {row['authored']:>4} "
              f"{row['pass']:>4} {row['fail']:>4} {row['not_run']:>4}")
    print(f"required suite: {total_pass}/{denominator} = {rate:.1%}; "
          f"not_run={total_not_run}; gate={'PASS' if gate else 'FAIL'}")
    print(f"extras: {len(extras)} authored, {extra_pass} pass, {extra_fail} fail")
    return 0 if gate else 1


if __name__ == "__main__":
    raise SystemExit(main())
