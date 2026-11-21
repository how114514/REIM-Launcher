#!/usr/bin/env python3
"""M3 save audit / semantic NBT snapshot (offline, no Java).

Reads a Minecraft 26.3 save directory without modifying it and writes a JSON
report used by docs/M3_ACCEPTANCE.md (A1) and the save round-trip matrix (B7):

  * level.dat, data/minecraft/*.dat and players/data/*.dat: NBT tree summary,
    selected fields and a canonical SHA-256 over the decoded tag tree (types
    included, list order preserved, compound keys sorted).
  * region files: chunk count, compression types, external .mcc references,
    Status/DataVersion, presence of entities/block entities/light/heightmaps
    and the decoded key list. With --chunks every chunk gets a record and a
    semantic digest so a rewrite can be diffed field by field.

The digest is deliberately independent of NBT byte layout (zlib/gzip, packed
long order and key order do not change it), so it can prove that a Native
rewrite preserved unknown fields. It is NOT a vanilla-readability proof.

Usage:
  python tools/m3-save-audit.py --world <save> [--out report.json] [--chunks]
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import struct
import sys
import zlib

TAG_NAMES = {
    0: "end", 1: "byte", 2: "short", 3: "int", 4: "long", 5: "float",
    6: "double", 7: "byte_array", 8: "string", 9: "list", 10: "compound",
    11: "int_array", 12: "long_array",
}

MAX_DEPTH = 512


class NbtError(Exception):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, count: int) -> bytes:
        if count < 0 or self.pos + count > len(self.data):
            raise NbtError("truncated NBT at %d (+%d of %d)" %
                           (self.pos, count, len(self.data)))
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def u1(self) -> int:
        return self.take(1)[0]

    def i1(self) -> int:
        return struct.unpack(">b", self.take(1))[0]

    def i2(self) -> int:
        return struct.unpack(">h", self.take(2))[0]

    def i4(self) -> int:
        return struct.unpack(">i", self.take(4))[0]

    def i8(self) -> int:
        return struct.unpack(">q", self.take(8))[0]

    def f4(self) -> float:
        return struct.unpack(">f", self.take(4))[0]

    def f8(self) -> float:
        return struct.unpack(">d", self.take(8))[0]

    def text(self) -> str:
        length = struct.unpack(">H", self.take(2))[0]
        raw = self.take(length)
        # Java modified UTF-8: C0 80 is NUL; standard UTF-8 is accepted as is.
        return raw.replace(b"\xc0\x80", b"\x00").decode("utf-8", "replace")


class List:
    __slots__ = ("element_type", "items")

    def __init__(self, element_type: int, items: list):
        self.element_type = element_type
        self.items = items


class ByteArray:
    __slots__ = ("bytes",)

    def __init__(self, value: bytes):
        self.bytes = value


class IntArray:
    __slots__ = ("ints",)

    def __init__(self, value: list):
        self.ints = value


class LongArray:
    __slots__ = ("longs",)

    def __init__(self, value: list):
        self.longs = value


def read_payload(reader: Reader, tag: int, depth: int = 0):
    if depth > MAX_DEPTH:
        raise NbtError("NBT nesting exceeds %d" % MAX_DEPTH)
    if tag == 1:
        return reader.i1()
    if tag == 2:
        return reader.i2()
    if tag == 3:
        return reader.i4()
    if tag == 4:
        return reader.i8()
    if tag == 5:
        return reader.f4()
    if tag == 6:
        return reader.f8()
    if tag == 7:
        return ByteArray(reader.take(reader.i4()))
    if tag == 8:
        return reader.text()
    if tag == 9:
        element = reader.i1()
        count = reader.i4()
        if count < 0 or count > 100_000_000:
            raise NbtError("implausible list length %d" % count)
        return List(element, [read_payload(reader, element, depth + 1)
                              for _ in range(count)])
    if tag == 10:
        out = {}
        while True:
            child = reader.u1()
            if child == 0:
                return out
            key = reader.text()
            out[key] = read_payload(reader, child, depth + 1)
    if tag == 11:
        count = reader.i4()
        if count < 0 or count > 64_000_000:
            raise NbtError("implausible int array length %d" % count)
        return IntArray(list(struct.unpack(">%di" % count,
                                           reader.take(4 * count))))
    if tag == 12:
        count = reader.i4()
        if count < 0 or count > 32_000_000:
            raise NbtError("implausible long array length %d" % count)
        return LongArray(list(struct.unpack(">%dq" % count,
                                            reader.take(8 * count))))
    raise NbtError("unknown tag type %d at byte %d" % (tag, reader.pos))


def decompress(raw: bytes) -> tuple[bytes, str]:
    if len(raw) >= 2 and raw[0] == 0x1F and raw[1] == 0x8B:
        return gzip.decompress(raw), "gzip"
    if len(raw) >= 2 and (raw[0] & 0x0F) == 8 and \
            ((raw[0] << 8) | raw[1]) % 31 == 0:
        return zlib.decompress(raw), "zlib"
    return raw, "none"


def parse_nbt(raw: bytes) -> tuple[object, str]:
    data, kind = decompress(raw)
    reader = Reader(data)
    tag = reader.u1()
    name = reader.text()
    if tag != 10:
        raise NbtError("root is %s, expected compound" % TAG_NAMES.get(tag, tag))
    value = read_payload(reader, tag)
    if reader.pos != len(data):
        raise NbtError("%d trailing bytes after root" % (len(data) - reader.pos))
    return {"name": name, "value": value}, kind


def canonical(value) -> bytes:
    """Type-aware canonical form independent of byte layout/key order."""
    if isinstance(value, dict):
        parts = []
        for key in sorted(value):
            parts.append(canonical(key))
            parts.append(canonical(value[key]))
        return b"{" + b"".join(parts) + b"}"
    if isinstance(value, List):
        return b"[%d:%s]" % (value.element_type,
                              b"".join(canonical(item) for item in value.items))
    if isinstance(value, ByteArray):
        return b"ba" + value.bytes
    if isinstance(value, IntArray):
        return b"ia" + struct.pack(">%di" % len(value.ints), *value.ints)
    if isinstance(value, LongArray):
        return b"la" + struct.pack(">%dq" % len(value.longs), *value.longs)
    if isinstance(value, bool):
        return b"b%d" % int(value)
    if isinstance(value, float):
        return b"d" + struct.pack(">d", value)
    if isinstance(value, int):
        return b"i%d" % value
    if isinstance(value, str):
        return b"s" + value.encode("utf-8", "replace")
    raise TypeError(type(value))


def digest_nbt(root) -> str:
    return hashlib.sha256(canonical(root)).hexdigest()


def json_default(value):
    """Report-friendly form for typed NBT containers."""
    if isinstance(value, List):
        return {"kind": "list", "element": TAG_NAMES.get(value.element_type),
                "items": value.items}
    if isinstance(value, ByteArray):
        return {"kind": "byte_array", "length": len(value.bytes)}
    if isinstance(value, IntArray):
        return {"kind": "int_array", "items": value.ints}
    if isinstance(value, LongArray):
        return {"kind": "long_array", "items": value.longs}
    raise TypeError(type(value).__name__)


def summarize_value(value, max_items: int = 12):
    if isinstance(value, dict):
        return {"kind": "compound", "keys": sorted(value.keys())}
    if isinstance(value, List):
        return {"kind": "list", "element": TAG_NAMES.get(value.element_type),
                "length": len(value.items)}
    if isinstance(value, ByteArray):
        return {"kind": "byte_array", "length": len(value.bytes)}
    if isinstance(value, IntArray):
        return {"kind": "int_array", "length": len(value.ints),
                "items": value.ints[:max_items]}
    if isinstance(value, LongArray):
        return {"kind": "long_array", "length": len(value.longs)}
    return {"kind": type(value).__name__, "value": value}


def find(value, path: str):
    node = value
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


def audit_nbt_file(path: str) -> dict:
    entry = {"path": path}
    try:
        raw = open(path, "rb").read()
        root, compression = parse_nbt(raw)
        entry["size"] = len(raw)
        entry["compression"] = compression
        entry["root_name"] = root["name"]
        entry["root_keys"] = sorted(root["value"].keys())
        entry["semantic_sha256"] = digest_nbt(root["value"])
        entry["value"] = root["value"]
    except Exception as error:  # noqa: BLE001 - report, never crash the audit
        entry["error"] = str(error)
    return entry


REGION_SIZE = 4096
REGION_HEADER = REGION_SIZE * 2


def read_chunk_payload(region_path: str, region_bytes: bytes,
                       local_x: int, local_z: int):
    index = local_z * 32 + local_x
    word = struct.unpack(">I", region_bytes[index * 4:index * 4 + 4])[0]
    if word == 0:
        return None
    sector = word >> 8
    count = word & 0xFF
    start = sector * REGION_SIZE
    allocated = count * REGION_SIZE
    if count == 0 or start + allocated > len(region_bytes) or allocated < 5:
        raise NbtError("invalid sector allocation")
    length = struct.unpack(">i", region_bytes[start:start + 4])[0]
    if length < 1 or length + 4 > allocated:
        raise NbtError("invalid payload length")
    kind = region_bytes[start + 4]
    external = bool(kind & 0x80)
    compression = kind & 0x7F
    payload = region_bytes[start + 5:start + 4 + length]
    mcc = None
    if external:
        mcc = os.path.join(os.path.dirname(region_path),
                           "c.%d.%d.mcc" % (local_x, local_z))
        payload = open(mcc, "rb").read()
    if compression == 1:
        data = gzip.decompress(payload)
    elif compression == 2:
        data = zlib.decompress(payload)
    elif compression == 3:
        data = payload
    else:
        raise NbtError("compression type %d is not decoded by this audit" %
                       compression)
    return {"compression": compression, "external": external,
            "payload": data, "mcc": mcc}


def region_audit(path: str, per_chunk: bool) -> dict:
    result = {"file": os.path.basename(path), "chunks": 0,
              "compression": {}, "errors": [], "external": 0,
              "key_presence": {}, "status": {}, "data_versions": {},
              "chunk_records": []}
    raw = open(path, "rb").read()
    if len(raw) < REGION_HEADER:
        result["errors"].append("region shorter than header")
        return result
    watched = ["entities", "block_entities", "structures", "Heightmaps",
               "PostProcessing", "isLightOn", "SkyLight", "BlockLight",
               "block_states", "biomes", "block_ticks", "fluid_ticks",
               "scheduled_ticks", "block_entity", "poi"]
    for local_z in range(32):
        for local_x in range(32):
            try:
                chunk = read_chunk_payload(path, raw, local_x, local_z)
            except Exception as error:  # noqa: BLE001
                result["errors"].append("chunk %d,%d: %s" %
                                        (local_x, local_z, error))
                continue
            if chunk is None:
                continue
            try:
                root, _ = parse_nbt(chunk["payload"])
            except Exception as error:  # noqa: BLE001
                result["errors"].append("chunk %d,%d NBT: %s" %
                                        (local_x, local_z, error))
                continue
            value = root["value"]
            result["chunks"] += 1
            key = "lz4" if chunk["compression"] == 4 else \
                str(chunk["compression"])
            result["compression"][key] = result["compression"].get(key, 0) + 1
            if chunk["external"]:
                result["external"] += 1
            for name in watched:
                if name in value:
                    result["key_presence"][name] = \
                        result["key_presence"].get(name, 0) + 1
            status = value.get("Status")
            if isinstance(status, str):
                result["status"][status] = result["status"].get(status, 0) + 1
            data_version = value.get("DataVersion")
            if isinstance(data_version, int):
                key = str(data_version)
                result["data_versions"][key] = \
                    result["data_versions"].get(key, 0) + 1
            if per_chunk:
                sections = value.get("sections")
                entities = value.get("entities")
                block_entities = value.get("block_entities")
                record = {
                    "x": value.get("xPos"), "z": value.get("zPos"),
                    "type": value.get("type"), "status": status,
                    "data_version": data_version,
                    "compression": chunk["compression"],
                    "external": chunk["external"],
                    "sections": len(sections.items)
                    if isinstance(sections, List) else 0,
                    "entities": len(entities.items)
                    if isinstance(entities, List) else 0,
                    "block_entities": len(block_entities.items)
                    if isinstance(block_entities, List) else 0,
                    "keys": sorted(value.keys()),
                    "semantic_sha256": digest_nbt(value),
                }
                result["chunk_records"].append(record)
    return result


def audit_world(world: str, per_chunk: bool) -> dict:
    report = {
        "format": "reim.m3.save-audit",
        "format_version": 1,
        "world": os.path.abspath(world),
        "layout": "unknown",
        "level": None,
        "data": [],
        "dimension_data": [],
        "players": [],
        "dimensions": [],
        "errors": [],
    }
    level_path = os.path.join(world, "level.dat")
    if not os.path.isfile(level_path):
        report["errors"].append("level.dat missing")
        return report
    level = audit_nbt_file(level_path)
    if "value" in level:
        data = level["value"].get("Data", level["value"])
        report["level"] = {
            "path": "level.dat",
            "compression": level["compression"],
            "data_version": find(data, "DataVersion"),
            "version_name": find(data, "Version.Name"),
            "version_id": find(data, "Version.Id"),
            "level_version": find(data, "version"),
            "level_name": find(data, "LevelName"),
            "time": find(data, "Time"),
            "game_type": find(data, "GameType"),
            "initialized": find(data, "initialized"),
            "difficulty": find(data, "difficulty_settings.difficulty"),
            "hardcore": find(data, "difficulty_settings.hardcore"),
            "spawn": find(data, "spawn"),
            "data_keys": level["root_keys"],
            "semantic_sha256": level["semantic_sha256"],
        }
    else:
        report["errors"].append("level.dat: " + level.get("error", "?"))

    # Older 26.3 data files live at data/ and data/minecraft/; accept both.
    for base in (os.path.join(world, "data"), os.path.join(world, "data", "minecraft")):
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            path = os.path.join(base, name)
            if not os.path.isfile(path):
                continue
            entry = audit_nbt_file(path)
            entry["relative"] = os.path.relpath(path, world).replace("\\", "/")
            if "value" in entry:
                data = entry["value"].get("data")
                if isinstance(data, dict):
                    entry["data_keys"] = sorted(data.keys())
                entry["root_keys"] = sorted(entry["value"].keys())
                if isinstance(entry["value"].get("DataVersion"), int):
                    entry["data_version"] = entry["value"]["DataVersion"]
                entry.pop("value", None)
            report["data"].append(entry)

    dimensions_root = os.path.join(world, "dimensions")
    if os.path.isdir(dimensions_root):
        report["layout"] = "dimensions"
        for namespace in sorted(os.listdir(dimensions_root)):
            ns_path = os.path.join(dimensions_root, namespace)
            if not os.path.isdir(ns_path):
                continue
            for dimension in sorted(os.listdir(ns_path)):
                dim_root = os.path.join(ns_path, dimension)
                if not os.path.isdir(dim_root):
                    continue
                dim_id = namespace + ":" + dimension
                info = {"id": dim_id, "root": os.path.relpath(
                    dim_root, world).replace("\\", "/"),
                    "region_files": [], "data_files": [], "poi_files": []}
                region_dir = os.path.join(dim_root, "region")
                if os.path.isdir(region_dir):
                    for name in sorted(os.listdir(region_dir)):
                        if name.endswith(".mca"):
                            info["region_files"].append(
                                region_audit(os.path.join(region_dir, name),
                                             per_chunk))
                        elif name.endswith(".mcc"):
                            info.setdefault("mcc", []).append(name)
                poi_dir = os.path.join(dim_root, "poi")
                if os.path.isdir(poi_dir):
                    for name in sorted(os.listdir(poi_dir)):
                        info["poi_files"].append(name)
                data_dir = os.path.join(dim_root, "data")
                if os.path.isdir(data_dir):
                    for root, _dirs, files in os.walk(data_dir):
                        for name in sorted(files):
                            path = os.path.join(root, name)
                            entry = audit_nbt_file(path)
                            entry["relative"] = os.path.relpath(
                                path, world).replace("\\", "/")
                            entry.pop("value", None)
                            info["data_files"].append(entry)
                report["dimensions"].append(info)
    else:
        region_dir = os.path.join(world, "region")
        report["layout"] = "legacy"
        if os.path.isdir(region_dir):
            info = {"id": "minecraft:overworld", "region_files": [],
                    "data_files": [], "poi_files": []}
            for name in sorted(os.listdir(region_dir)):
                if name.endswith(".mca"):
                    info["region_files"].append(
                        region_audit(os.path.join(region_dir, name), per_chunk))
            report["dimensions"].append(info)

    players_dir = os.path.join(world, "players", "data")
    if os.path.isdir(players_dir):
        for name in sorted(os.listdir(players_dir)):
            path = os.path.join(players_dir, name)
            if not os.path.isfile(path):
                continue
            entry = audit_nbt_file(path)
            entry["relative"] = os.path.relpath(path, world).replace("\\", "/")
            if "value" in entry:
                entry["player_keys"] = sorted(entry["value"].keys())
                entry.pop("value", None)
            report["players"].append(entry)
    return report


def summary(report: dict) -> str:
    lines = ["world: " + report["world"],
             "layout: " + report["layout"]]
    level = report.get("level")
    if level:
        lines.append("level.dat: DataVersion=%s version=%s spawn=%s" %
                     (level.get("data_version"), level.get("version_name"),
                      json.dumps(level.get("spawn"), ensure_ascii=False,
                                 default=json_default)))
    lines.append("data files: %d, players: %d" %
                 (len(report["data"]), len(report["players"])))
    for dim in report["dimensions"]:
        chunks = sum(entry["chunks"] for entry in dim["region_files"])
        compression = {}
        for entry in dim["region_files"]:
            for key, value in entry["compression"].items():
                compression[key] = compression.get(key, 0) + value
        errors = sum(len(entry["errors"]) for entry in dim["region_files"])
        lines.append("dimension %s: %d chunks, compression %s, errors %d, "
                     "poi files %d" %
                     (dim["id"], chunks, compression, errors,
                      len(dim["poi_files"])))
        status = {}
        keys = {}
        for entry in dim["region_files"]:
            for key, value in entry["status"].items():
                status[key] = status.get(key, 0) + value
            for key, value in entry["key_presence"].items():
                keys[key] = keys.get(key, 0) + value
        if status:
            lines.append("  status: " + json.dumps(status, sort_keys=True))
        if keys:
            lines.append("  keys: " + json.dumps(keys, sort_keys=True))
    for error in report["errors"]:
        lines.append("ERROR: " + error)
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--world", required=True, help="save directory")
    parser.add_argument("--out", help="write the JSON report here")
    parser.add_argument("--chunks", action="store_true",
                        help="include one record per chunk (digests)")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    if not os.path.isdir(args.world):
        print("world directory not found: " + args.world, file=sys.stderr)
        return 2
    report = audit_world(args.world, args.chunks)
    text = json.dumps(report, ensure_ascii=False, indent=2,
                      default=json_default)
    if args.out:
        out_dir = os.path.dirname(os.path.abspath(args.out))
        os.makedirs(out_dir, exist_ok=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text + "\n")
    if not args.quiet:
        print(summary(report))
        if not args.out:
            print(text)
    return 0 if not report["errors"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
