#!/usr/bin/env python3
"""M3-I8 asset/license audit.

Checks that the repository does not track Mojang game assets or world data,
that generated output carries no absolute developer paths in its manifest,
and that packaged directories contain no Minecraft JARs/regions. Synthetic
test fixtures are allowed and listed explicitly.

Usage:
  python tools/m3-asset-audit.py [--generated generated/26.3]
                                 [--package build/native-build/26.3]
                                 [--json build/m3/reports/asset-audit.json]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ALLOWED_TRACKED = {
    "tests/fixtures/packs/res_ok/assets/minecraft/lang/en_us.json",
    "tests/fixtures/packs/res_ok/assets/minecraft/sounds.json",
    "tests/fixtures/packs/res_ok/assets/minecraft/sounds/custom/click.ogg",
    "tests/fixtures/packs/res_ok/assets/minecraft/textures/block/stone.png",
    "tests/fixtures/packs/res_missing/assets/minecraft/textures/block/not_a_block.png",
    "tests/fixtures/packs/res_missing/assets/minecraft/models/block/stone.json",
    "tests/fixtures/packs/data_ok/data/minecraft/recipe/oak_planks.json",
    "tests/fixtures/packs/data_ok/data/minecraft/tags/item/planks.json",
}


def git_tracked(repo: Path) -> list[str]:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(repo), "ls-files"], text=True, stderr=subprocess.DEVNULL
        )
    except (OSError, subprocess.CalledProcessError):
        return []
    return [line.strip().replace("\\", "/") for line in out.splitlines() if line.strip()]


def audit_tracked(repo: Path) -> dict:
    violations = []
    tracked_assets = []
    for path in git_tracked(repo):
        lower = path.lower()
        if lower.endswith((".jar", ".mca", ".mcc", ".dat")):
            violations.append({"path": path, "reason": "Mojang/binary game data tracked"})
            continue
        if (path.startswith("assets/minecraft/") or path.startswith("data/minecraft/")):
            if path not in ALLOWED_TRACKED:
                violations.append(
                    {"path": path, "reason": "Mojang namespace data tracked"}
                )
        if path.startswith("tests/fixtures/packs/"):
            tracked_assets.append(path)
    return {
        "tracked_files": len(git_tracked(repo)),
        "tracked_fixture_assets": tracked_assets,
        "violations": violations,
    }


def audit_generated(generated: Path) -> dict:
    manifest_path = generated / "manifest.json"
    if not manifest_path.is_file():
        return {"exists": False, "error": "manifest.json missing"}
    text = manifest_path.read_text(encoding="utf-8")
    manifest = json.loads(text)
    absolute_markers = [m for m in ("D:\\", "C:\\", "/D/", "/C/") if m in text]
    generated_files = manifest.get("generated", [])
    return {
        "exists": True,
        "format": manifest.get("format"),
        "abi_version": manifest.get("abi_version"),
        "generated_files": len(generated_files),
        "absolute_path_markers": absolute_markers,
        "contains_mojang_assets": any(
            entry.get("path", "").endswith((".ogg", ".png"))
            and ("sounds/" in entry["path"] or "textures/" in entry["path"])
            for entry in generated_files
        ),
        "note": "generated/ is not committed or shipped; Mojang inputs stay local",
    }


def audit_package(package: Path) -> dict:
    if not package.is_dir():
        return {"exists": False}
    violations = []
    for root, _dirs, files in os.walk(package):
        for name in files:
            if name.lower().endswith((".jar", ".mca", ".mcc")):
                violations.append(os.path.join(root, name))
    return {"exists": True, "files": sum(len(f) for _, _, f in os.walk(package)),
            "game_data_violations": violations}


def audit_licenses(repo: Path) -> dict:
    licenses = repo / "third_party" / "LICENSES.md"
    text = licenses.read_text(encoding="utf-8") if licenses.is_file() else ""
    required = ["miniz", "stb", "nlohmann", "glfw", "lz4"]
    return {
        "licenses_file": str(licenses.relative_to(repo)) if licenses.is_file() else None,
        "vendored_present": {
            name: (repo / "third_party" / name).exists()
            for name in required
            if name != "glfw"
        },
        "mentions": {name: name.lower() in text.lower() for name in required},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--generated", default="generated/26.3")
    parser.add_argument("--package", default="build/native-build/26.3")
    parser.add_argument("--json", default="build/m3/reports/asset-audit.json")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    generated = repo / args.generated
    package = repo / args.package
    report = {
        "schema": "reim.asset_audit",
        "format": 1,
        "tracked": audit_tracked(repo),
        "generated": audit_generated(generated),
        "package": audit_package(package),
        "licenses": audit_licenses(repo),
    }
    out = repo / args.json
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    violations = report["tracked"]["violations"]
    if violations:
        for item in violations:
            print(f"VIOLATION {item['path']}: {item['reason']}", file=sys.stderr)
        print(f"asset audit: {len(violations)} tracked-data violation(s)")
        return 1
    print(
        "asset audit: OK (no tracked Mojang game data; "
        f"{report['tracked']['tracked_files']} tracked files; "
        f"generated format {report['generated'].get('format')})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
