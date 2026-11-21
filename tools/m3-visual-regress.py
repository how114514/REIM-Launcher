#!/usr/bin/env python3
"""M3-J9 visual regression using REIMPreview screenshots.

Runs a fixed camera/scene set (day/night/rain), compares against baselines with
a pooled difference threshold, and writes diff images + a JSON report. Preview
is the shared-subset renderer; UE-only R1/R2 items (entities/particles in UE)
are not part of this comparison.

Usage:
  python tools/m3-visual-regress.py --world <save-copy> [--update]
                                    [--baseline build/m3/visual-baseline]
                                    [--current build/m3/reports/visual-current]
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

SCENES = {
    "day": {"time": 6000, "weather": "clear"},
    "night": {"time": 18000, "weather": "clear"},
    "rain": {"time": 6000, "weather": "rain"},
}


def run_scene(repo: Path, preview: Path, world: Path, generated: Path,
              out_dir: Path, name: str, scenario: dict) -> dict:
    scenario_path = out_dir / f"scenario-{name}.json"
    scenario_path.write_text(json.dumps(scenario), encoding="utf-8")
    screenshot = out_dir / f"{name}.png"
    report = out_dir / f"{name}.run.json"
    cmd = [
        str(preview),
        "--world", str(world),
        "--generated", str(generated),
        "--scenario", str(scenario_path),
        "--spawn", "16.5", "100.0", "80.5",
        "--spawn-yaw", "30",
        "--spawn-pitch", "20",
        "--render-distance", "3",
        "--frames", "3000",
        "--exit-tick", "30",
        "--hidden",
        "--no-vsync",
        "--screenshot", str(screenshot),
        "--report", str(report),
    ]
    completed = subprocess.run(cmd, cwd=repo, capture_output=True, text=True)
    return {
        "name": name,
        "exit_code": completed.returncode,
        "screenshot": str(screenshot),
        "exists": screenshot.is_file(),
        "stderr_tail": "\n".join(completed.stderr.splitlines()[-4:]),
    }


def compare(baseline: Path, current: Path, diff_path: Path) -> dict:
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return {"ok": False, "error": "Pillow is required"}
    a = Image.open(baseline).convert("RGB")
    b = Image.open(current).convert("RGB")
    if a.size != b.size:
        return {"ok": False, "error": f"size mismatch {a.size} vs {b.size}"}
    diff = ImageChops.difference(a, b)
    histogram = diff.histogram()
    total = a.size[0] * a.size[1]
    changed = 0
    sad = 0
    for channel in range(3):
        for value in range(256):
            count = histogram[channel * 256 + value]
            sad += count * value
            if value > 8:
                changed += count
    mean = sad / (total * 3)
    ratio = changed / (total * 3)
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    amplified = diff.point(lambda v: min(255, v * 8))
    amplified.save(diff_path)
    return {
        "ok": mean <= 3.0 and ratio <= 0.02,
        "mean_abs_diff": round(mean, 4),
        "changed_ratio": round(ratio, 6),
        "threshold_mean": 3.0,
        "threshold_ratio": 0.02,
        "diff_image": str(diff_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--world", required=True)
    parser.add_argument("--generated", default="generated/26.3")
    parser.add_argument("--preview", default="build/m3/bin/REIMPreview.exe")
    parser.add_argument("--baseline", default="build/m3/visual-baseline")
    parser.add_argument("--current", default="build/m3/reports/visual-current")
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    world = Path(args.world)
    generated = repo / args.generated
    preview = repo / args.preview
    baseline = repo / args.baseline
    current = repo / args.current
    current.mkdir(parents=True, exist_ok=True)
    if args.update:
        baseline.mkdir(parents=True, exist_ok=True)

    report = {"schema": "reim.visual_regress", "format": 1, "scenes": []}
    failed = False
    for name, scenario in SCENES.items():
        run = run_scene(repo, preview, world, generated, current, name, scenario)
        if not run["exists"]:
            run["status"] = "fail"
            run["error"] = "screenshot not produced"
            failed = True
            report["scenes"].append(run)
            continue
        base = baseline / f"{name}.png"
        if args.update:
            shutil.copyfile(current / f"{name}.png", base)
            run["status"] = "baseline_updated"
        elif not base.is_file():
            run["status"] = "not_run"
            run["error"] = "baseline missing; run with --update first"
            failed = True
        else:
            result = compare(base, current / f"{name}.png",
                             current / f"diff-{name}.png")
            run["compare"] = result
            run["status"] = "pass" if result.get("ok") else "fail"
            if not result.get("ok"):
                failed = True
        report["scenes"].append(run)

    report["status"] = "fail" if failed else "pass"
    out = current / "visual-regress.json"
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    for scene in report["scenes"]:
        print(f"{scene['name']}: {scene['status']}")
    print(f"visual regression: {report['status']} ({out})")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
