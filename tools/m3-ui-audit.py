#!/usr/bin/env python3
"""M3-H8 Host audit: UE consumes the Contract, it never re-implements gameplay.

Scans the UE Host sources for identifiers that would indicate a second
inventory/crafting/loot/world implementation. Contract consumption types
(ReimInventorySnapshot, ReimItemStack, ReimUiFrame, ...) are explicitly
allowed. The audit is intentionally narrow: it catches code that names a
gameplay implementation, not use of the published snapshot.

Usage:
  python tools/m3-ui-audit.py [--root runtime/host/Source] [--json OUT]
Exit code 0 = no hits, 1 = hits (report still written).
"""
import argparse
import json
import os
import re
import sys

FORBIDDEN = [
    (r'\bFAnvil\b|\bAnvilWriter\b|\bRegionWriter\b', 'anvil writer'),
    (r'\bFNbt\b|\bNbtTag\b|\bNbtDocument\b', 'nbt implementation'),
    (r'\bFCrafting\b|\bCraftingRecipe\b|\bRecipeBook\b|\bMatchRecipe\b', 'crafting logic'),
    (r'\bFLootTable\b|\bLootRoll\b|\bRollLoot\b', 'loot logic'),
    (r'\bFFurnace\b|\bFurnaceState\b', 'furnace logic'),
    (r'\bFChest\b|\bChestState\b', 'chest logic'),
    (r'\bFInventory\b|\bFItemStack\b|\bItemStackImpl\b', 'inventory logic'),
    (r'\bBlockTable\b|\bFBlockRegistry\b', 'block table'),
    (r'\bSetBlock\s*\(|\bPlaceBlock\s*\(|\bBreakBlock\s*\(', 'world mutation'),
    (r'\bGenerateChunk\b|\bChunkMesher\b|\bMeshChunk\b', 'chunk generation/meshing'),
    (r'\bBiomeTint\b|\bLightEngine\b', 'biome/lighting implementation'),
]

ALLOWED_CONTEXT = [
    'ReimInventorySnapshot',
    'ReimInventorySlot',
    'ReimItemStack',
    'ReimUiFrame',
    'ReimUiDraw',
    'ReimPlayerSnapshot',
    'ReimInteractionSnapshot',
]

SOURCE_EXT = {'.h', '.hpp', '.cpp', '.cc', '.cxx', '.ini', '.cs'}


def scan(root):
    hits = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if os.path.splitext(name)[1].lower() not in SOURCE_EXT:
                continue
            path = os.path.join(dirpath, name)
            try:
                with open(path, 'r', encoding='utf-8', errors='replace') as handle:
                    lines = handle.readlines()
            except OSError as error:
                hits.append({'file': path, 'line': 0, 'rule': 'unreadable',
                             'text': str(error)})
                continue
            for number, line in enumerate(lines, 1):
                if any(allowed in line for allowed in ALLOWED_CONTEXT):
                    continue
                for pattern, rule in FORBIDDEN:
                    if re.search(pattern, line):
                        hits.append({'file': path, 'line': number, 'rule': rule,
                                     'text': line.strip()[:200]})
    return hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default='runtime/host/Source')
    parser.add_argument('--json', default='build/m3/reports/ui-audit.json')
    args = parser.parse_args()
    if not os.path.isdir(args.root):
        print('not_run: Host source directory missing: ' + args.root)
        return 2
    hits = scan(args.root)
    report = {'root': args.root, 'hits': len(hits), 'findings': hits,
              'result': 'pass' if not hits else 'fail'}
    if args.json:
        os.makedirs(os.path.dirname(args.json), exist_ok=True)
        with open(args.json, 'w', encoding='utf-8') as handle:
            json.dump(report, handle, indent=2)
            handle.write('\n')
    print('ui-audit: %d hit(s) in %s -> %s' %
          (len(hits), args.root, report['result']))
    for hit in hits[:40]:
        print('  %s:%d [%s] %s' % (hit['file'], hit['line'], hit['rule'], hit['text']))
    return 0 if not hits else 1


if __name__ == '__main__':
    sys.exit(main())
