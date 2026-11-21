#!/usr/bin/env python3
"""Generate ue_adapter.json for the pinned UE Native Adapter (M2-E3).

The UE runtime project (runtime/host) is a pinned Native Adapter build, not a
Java render translation. This script records its identity and the hashes of
the adapter assets (uproject, Config, Content, plugin config, Shaders) so the
Converter can bind a NativeVersion to exactly this adapter and the UE Bridge
can reject anything else (M2-E6/E9).

Usage:
  python converter/adapters/ue/generate_ue_adapter.py \
    --repo D:\\EngineeringDocuments\\REIM-Launcher \
    --out runtime/host/Build/Adapter

  python ... --check     # exit 3 when the existing ue_adapter.json differs

The output contains no absolute paths and no timestamps, so two runs over the
same source tree are byte identical (M2-E7).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

SKIP_SUFFIXES = {
    ".pdb", ".dll", ".exe", ".lib", ".exp", ".obj", ".ilk", ".map", ".tlog",
}
SKIP_NAMES = {".DS_Store", "Thumbs.db"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def collect(root: Path, entries: list[Path]) -> list[Path]:
    files: list[Path] = []
    for entry in entries:
        target = (root / entry).resolve()
        if not target.exists():
            continue
        if target.is_file():
            files.append(target)
            continue
        for child in sorted(target.rglob("*")):
            if child.is_file():
                files.append(child)
    kept: list[Path] = []
    for path in files:
        if path.name in SKIP_NAMES or path.suffix.lower() in SKIP_SUFFIXES:
            continue
        kept.append(path)
    return kept


def relative_posix(root: Path, path: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def read_bridge_pin(header: Path) -> dict[str, str]:
    if not header.is_file():
        raise SystemExit(f"bridge pin header not found: {header}")
    text = header.read_text(encoding="utf-8")
    defs: dict[str, str] = {}
    for match in re.finditer(r'^#define\s+(REIM_ADAPTER_\w+)\s+("?)([^"\s]+)\2',
                             text, re.MULTILINE):
        defs[match.group(1)] = match.group(3)
    return defs


def canonical_json(value: object) -> str:
    return json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False) + "\n"


def build(repo: Path, pin_path: Path) -> dict:
    pin_text = pin_path.read_bytes()
    pin = json.loads(pin_text.decode("utf-8"))

    header = repo / pin["bridge_pin_header"]
    defines = read_bridge_pin(header)
    expected = {
        "REIM_ADAPTER_ID": pin["adapter_id"],
        "REIM_ADAPTER_VERSION": pin["adapter_version"],
        "REIM_ADAPTER_UE_VERSION": pin["ue_version"],
        "REIM_ADAPTER_ABI_VERSION": str(pin["abi_version"]),
        "REIM_ADAPTER_RENDER_LEVEL": str(pin["render_level"]),
    }
    def normalize(value):
        # C integer suffixes (2u / 2U / 2L) are not part of the value.
        return value.rstrip("uUlL") if value is not None else None

    for key, value in expected.items():
        if normalize(defines.get(key)) != value:
            raise SystemExit(
                f"adapter pin mismatch: {key} in {header} is "
                f"{defines.get(key)!r}, adapter_pin.json needs {value!r}")

    files = collect(repo, [Path(p) for p in pin["asset_roots"]])
    assets = []
    for path in sorted(files, key=lambda p: relative_posix(repo, p)):
        assets.append({
            "path": relative_posix(repo, path),
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    if not assets:
        raise SystemExit("no adapter assets found; check asset_roots")

    digest = hashlib.sha256()
    for asset in assets:
        digest.update(asset["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(asset["sha256"].encode("ascii"))
        digest.update(b"\n")

    return {
        "schema": "reim.ue_adapter",
        "format": 2,
        "adapter_id": pin["adapter_id"],
        "adapter_version": pin["adapter_version"],
        "ue_version": pin["ue_version"],
        "ue_version_short": pin["ue_version_short"],
        "abi_version": pin["abi_version"],
        "render_level": pin["render_level"],
        "platform": pin["platform"],
        "feature_flags": pin["feature_flags"],
        "asset_root_sha256": digest.hexdigest(),
        "pin_sha256": hashlib.sha256(pin_text).hexdigest(),
        "assets": assets,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--pin", default=str(Path(__file__).resolve().parent /
                                              "adapter_pin.json"))
    parser.add_argument("--out", required=True,
                        help="output directory (ue_adapter.json is written there)")
    parser.add_argument("--check", action="store_true",
                        help="verify the existing ue_adapter.json is current")
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    pin_path = Path(args.pin).resolve()
    out_dir = Path(args.out).resolve()
    document = build(repo, pin_path)
    text = canonical_json(document)

    target = out_dir / "ue_adapter.json"
    if args.check:
        if not target.is_file():
            print(f"missing {target}; run without --check", file=sys.stderr)
            return 3
        if target.read_text(encoding="utf-8") != text:
            print(f"{target} is stale; regenerate it", file=sys.stderr)
            return 3
        print(f"ue_adapter.json up to date ({len(document['assets'])} assets, "
              f"adapter {document['adapter_id']} {document['adapter_version']})")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {target} ({len(document['assets'])} assets, "
          f"asset_root_sha256={document['asset_root_sha256']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
