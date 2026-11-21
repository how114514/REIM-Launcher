#!/usr/bin/env python3
"""M3-J10: audit the UE Host for a second world/render/game implementation.

The UE runtime is a Contract consumer: it may upload meshes, interpolate
entity/particle transforms, draw UI/audio and convert coordinates. It must not
parse block/entity models, NBT/Anvil, recipes/loot, AI, worldgen or lighting.

Usage:
  python tools/m3-render-audit.py [--root runtime/host/Source]
                                  [--json build/m3/reports/render-audit.json]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

BANNED = {
    "nbt": r"\bnbt\b|\.dat\b|TagCompound|CompoundTag",
    "anvil": r"\banvil\b|\.mca\b|region file|regionfile",
    "block_table": r"block_table|BlockTable\b",
    "block_model_parse": r"elements\b.*rotation|blockstate",
    "recipe_loot": r"\brecipe\b|\bloot_table\b|crafting_table",
    "inventory_logic": r"\binventory\b.*\bslot\b|stack_size\b",
    "worldgen": r"worldgen|noise_settings|density_function|surface_rule",
    "ai_pathfind": r"pathfind|\ba_star\b|navigat(e|or)\b",
    "light_engine": r"light_engine|sky_light|block_light.*bfs",
    "chunk_mesh_gen": r"greedy.?mesh|face.?cull.*neighbou?r",
}

# Lines containing these may legitimately mention the word while only
# consuming Contract data (comments, logs, asset names).
ALLOW = [
    "reim_render",
    "reim_abi",
    "ReimEntityRender",
    "ReimParticle",
    "ReimUiDraw",
    "// ",
    "/*",
    "* ",
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default="runtime/host/Source")
    parser.add_argument("--json", default="build/m3/reports/render-audit.json")
    args = parser.parse_args()

    root = Path(args.root)
    if not root.is_dir():
        print(f"render audit: root missing: {root}", file=sys.stderr)
        return 2
    hits = []
    files = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in (".cpp", ".h", ".hpp", ".cs"):
            continue
        files += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if any(token in line for token in ALLOW):
                continue
            for area, pattern in BANNED.items():
                if re.search(pattern, line, re.IGNORECASE):
                    hits.append({"area": area, "file": str(path), "line": line_no,
                                 "text": line.strip()[:200]})
    report = {
        "schema": "reim.render_audit",
        "format": 1,
        "root": str(root),
        "files": files,
        "hit_count": len(hits),
        "hits": hits,
        "allowed_contract_consumers": [
            "ReimEntityRender/ReimParticle upload and interpolation",
            "UI display-list drawing and audio event playback",
            "coordinate conversion and GPU upload",
        ],
    }
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if hits:
        for hit in hits[:30]:
            print(f"{hit['area']}: {hit['file']}:{hit['line']}: {hit['text']}",
                  file=sys.stderr)
        print(f"render audit: {len(hits)} hit(s); see {out}")
        return 1
    print(f"render audit: OK ({files} files, 0 hits)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
