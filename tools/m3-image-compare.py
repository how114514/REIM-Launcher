#!/usr/bin/env python3
"""Fixed-image regression comparison for M3 visual checks.

Used for the UE R0 baseline (Development vs Shipping package, same world/time)
in addition to the Preview scene regression in tools/m3-visual-regress.py.

    python tools/m3-image-compare.py --baseline a.png --current b.png \
        --json build/m3/reports/ue-r0-visual.json [--mean 3.0] [--ratio 0.02]

Exit status 0 when mean absolute RGB difference <= --mean and the fraction of
pixels with a channel difference > 8 is <= --ratio.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--current", required=True)
    ap.add_argument("--json", default="build/m3/reports/image-compare.json")
    ap.add_argument("--mean", type=float, default=3.0)
    ap.add_argument("--ratio", type=float, default=0.02)
    args = ap.parse_args()

    a = np.asarray(Image.open(args.baseline).convert("RGB"), dtype=np.float32)
    b = np.asarray(Image.open(args.current).convert("RGB"), dtype=np.float32)
    if a.shape != b.shape:
        report = {"ok": False, "reason": f"size mismatch {a.shape} vs {b.shape}"}
    else:
        diff = np.abs(a - b)
        mean = float(diff.mean())
        ratio = float((diff.max(axis=2) > 8).mean())
        report = {
            "ok": mean <= args.mean and ratio <= args.ratio,
            "mean_abs_diff": round(mean, 4),
            "changed_ratio": round(ratio, 6),
            "max_channel_diff": int(diff.max()),
            "threshold_mean": args.mean,
            "threshold_ratio": args.ratio,
            "pixels": int(a.shape[0] * a.shape[1]),
        }
    report.update({"baseline": args.baseline, "current": args.current,
                   "schema": "reim.m3.image_compare", "format": 1})
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
