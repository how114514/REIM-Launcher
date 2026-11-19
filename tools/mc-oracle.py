#!/usr/bin/env python3
"""REIM M1-C Python oracle (S2).

An intentionally independent implementation of the read path used by the C++
world reader: NBT, Anvil region decompression, 26.3 chunk palettes and block
lookup. It exists to cross-check `reim-convert inspect-world` on a real save
(M1_PLAN C7) and never writes to the world.

Commands
    nbt <file> [-o out.json] [--max-array N]
    level --world <save>
    chunk --world <save> --x <chunkX> --z <chunkZ> [--dimension minecraft:overworld]
    block --world <save> --x <x> --y <y> --z <z> [--dimension ...]
    compare --world <save> --report <inspect-world.json> [--check-chunks]

Exit status: 0 success, 1 comparison failure, 2 usage/data error.
"""

from __future__ import annotations

import argparse
import gzip
import json
import struct
import sys
import zlib
from pathlib import Path

TAG_NAMES = {
    0: "end",
    1: "byte",
    2: "short",
    3: "int",
    4: "long",
    5: "float",
    6: "double",
    7: "byte_array",
    8: "string",
    9: "list",
    10: "compound",
    11: "int_array",
    12: "long_array",
}
AIR_NAMES = {
    "minecraft:air",
    "minecraft:cave_air",
    "minecraft:void_air",
    "air",
    "cave_air",
    "void_air",
}


class NbtError(Exception):
    pass


class Array:
    __slots__ = ("kind", "values")

    def __init__(self, kind: str, values):
        self.kind = kind
        self.values = values


def decode_modified_utf8(raw: bytes) -> str:
    units = []
    index = 0
    size = len(raw)
    while index < size:
        first = raw[index]
        if first < 0x80:
            units.append(first)
            index += 1
        elif first & 0xE0 == 0xC0:
            if index + 1 >= size or raw[index + 1] & 0xC0 != 0x80:
                raise NbtError(f"invalid modified UTF-8 at {index}")
            units.append(((first & 0x1F) << 6) | (raw[index + 1] & 0x3F))
            index += 2
        elif first & 0xF0 == 0xE0:
            if (
                index + 2 >= size
                or raw[index + 1] & 0xC0 != 0x80
                or raw[index + 2] & 0xC0 != 0x80
            ):
                raise NbtError(f"invalid modified UTF-8 at {index}")
            units.append(
                ((first & 0x0F) << 12)
                | ((raw[index + 1] & 0x3F) << 6)
                | (raw[index + 2] & 0x3F)
            )
            index += 3
        elif first & 0xF8 == 0xF0:
            # Not produced by Java DataOutput, but tolerate standard UTF-8.
            if index + 3 >= size:
                raise NbtError(f"invalid UTF-8 at {index}")
            units.append(
                ((first & 0x07) << 18)
                | ((raw[index + 1] & 0x3F) << 12)
                | ((raw[index + 2] & 0x3F) << 6)
                | (raw[index + 3] & 0x3F)
            )
            index += 4
        else:
            raise NbtError(f"invalid modified UTF-8 lead byte at {index}")

    chars = []
    index = 0
    while index < len(units):
        unit = units[index]
        if (
            0xD800 <= unit <= 0xDBFF
            and index + 1 < len(units)
            and 0xDC00 <= units[index + 1] <= 0xDFFF
        ):
            chars.append(
                chr(0x10000 + ((unit - 0xD800) << 10) + (units[index + 1] - 0xDC00))
            )
            index += 2
        else:
            chars.append(chr(unit))
            index += 1
    return "".join(chars)


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, count: int) -> bytes:
        if count < 0 or self.pos + count > len(self.data):
            raise NbtError(f"truncated payload at offset {self.pos}")
        value = self.data[self.pos : self.pos + count]
        self.pos += count
        return value

    def u8(self) -> int:
        return self.take(1)[0]

    def i8(self) -> int:
        return struct.unpack(">b", self.take(1))[0]

    def u16(self) -> int:
        return struct.unpack(">H", self.take(2))[0]

    def i16(self) -> int:
        return struct.unpack(">h", self.take(2))[0]

    def i32(self) -> int:
        return struct.unpack(">i", self.take(4))[0]

    def i64(self) -> int:
        return struct.unpack(">q", self.take(8))[0]

    def f32(self) -> float:
        return struct.unpack(">f", self.take(4))[0]

    def f64(self) -> float:
        return struct.unpack(">d", self.take(8))[0]

    def string(self) -> str:
        length = self.u16()
        return decode_modified_utf8(self.take(length))

    def payload(self, tag: int, depth: int = 0):
        if depth > 512:
            raise NbtError("NBT depth limit exceeded")
        if tag == 1:
            return self.i8()
        if tag == 2:
            return self.i16()
        if tag == 3:
            return self.i32()
        if tag == 4:
            return self.i64()
        if tag == 5:
            return self.f32()
        if tag == 6:
            return self.f64()
        if tag == 7:
            return Array("byte_array", self.take(self.i32()))
        if tag == 8:
            return self.string()
        if tag == 9:
            element = self.u8()
            count = self.i32()
            if count < 0:
                raise NbtError("negative list length")
            if element == 0 and count != 0:
                raise NbtError("non-empty list of TAG_End")
            return [self.payload(element, depth + 1) for _ in range(count)]
        if tag == 10:
            result = {}
            while True:
                child_tag = self.u8()
                if child_tag == 0:
                    break
                key = self.string()
                result[key] = self.payload(child_tag, depth + 1)
            return result
        if tag == 11:
            count = self.i32()
            if count < 0:
                raise NbtError("negative int array length")
            return Array("int_array", list(struct.unpack(f">{count}i", self.take(count * 4))))
        if tag == 12:
            count = self.i32()
            if count < 0:
                raise NbtError("negative long array length")
            return Array("long_array", list(struct.unpack(f">{count}q", self.take(count * 8))))
        raise NbtError(f"unknown tag id {tag}")


def parse_nbt(data: bytes):
    reader = Reader(data)
    tag = reader.u8()
    if tag == 0:
        raise NbtError("root tag is TAG_End")
    name = reader.string()
    value = reader.payload(tag)
    if reader.pos != len(data):
        raise NbtError(f"trailing bytes after NBT root at offset {reader.pos}")
    return name, value


def decompress_auto(raw: bytes) -> bytes:
    if raw[:2] == b"\x1f\x8b":
        return gzip.decompress(raw)
    if len(raw) >= 2 and raw[0] & 0x0F == 8 and ((raw[0] << 8) | raw[1]) % 31 == 0:
        return zlib.decompress(raw)
    return raw


def parse_nbt_file(path: Path):
    return parse_nbt(decompress_auto(path.read_bytes()))


class Region:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 8192:
            raise NbtError(f"region shorter than header: {path}")
        stem = path.name[:-4] if path.name.endswith(".mca") else path.name
        parts = stem.split(".")
        if len(parts) != 3 or parts[0] != "r":
            raise NbtError(f"not a region file name: {path.name}")
        self.region_x = int(parts[1])
        self.region_z = int(parts[2])

    def location(self, local_x: int, local_z: int) -> int:
        index = local_z * 32 + local_x
        return struct.unpack_from(">I", self.data, index * 4)[0]

    def has_chunk(self, local_x: int, local_z: int) -> bool:
        return self.location(local_x, local_z) != 0

    def read_chunk(self, local_x: int, local_z: int) -> bytes | None:
        word = self.location(local_x, local_z)
        if word == 0:
            return None
        sector = word >> 8
        count = word & 0xFF
        start = sector * 4096
        if count == 0 or start + count * 4096 > len(self.data):
            raise NbtError(
                f"{self.path.name} chunk {local_x},{local_z}: sectors outside file"
            )
        length = struct.unpack_from(">i", self.data, start)[0]
        if length < 1 or start + 4 + length > len(self.data):
            raise NbtError(
                f"{self.path.name} chunk {local_x},{local_z}: invalid length"
            )
        compression = self.data[start + 4]
        payload = self.data[start + 5 : start + 4 + length]
        if compression & 0x80:
            compression &= 0x7F
            external = self.path.parent / f"c.{local_x}.{local_z}.mcc"
            payload = external.read_bytes()
        if compression == 1:
            return gzip.decompress(payload)
        if compression == 2:
            return zlib.decompress(payload)
        if compression == 3:
            return payload
        raise NbtError(
            f"{self.path.name} chunk {local_x},{local_z}: compression {compression} "
            "is not supported"
        )

    def count_chunks(self) -> int:
        return sum(
            1
            for index in range(1024)
            if struct.unpack_from(">I", self.data, index * 4)[0] != 0
        )


class WorldView:
    def __init__(self, root: Path):
        self.root = root
        if not (root / "level.dat").is_file():
            raise NbtError(f"level.dat not found under {root}")
        self.layout = "26.3" if (root / "dimensions").is_dir() else "legacy"
        self._regions = {}
        self._chunks = {}

    def dimension_root(self, dimension: str) -> Path:
        if self.layout == "26.3":
            namespace, _, name = dimension.partition(":")
            if not name:
                raise NbtError(f"invalid dimension id {dimension!r}")
            return self.root / "dimensions" / namespace / name
        if dimension != "minecraft:overworld":
            raise NbtError(f"legacy world has no dimension {dimension}")
        return self.root

    def region(self, dimension: str, chunk_x: int, chunk_z: int) -> Region | None:
        region_x = chunk_x // 32
        region_z = chunk_z // 32
        path = (
            self.dimension_root(dimension)
            / "region"
            / f"r.{region_x}.{region_z}.mca"
        )
        key = str(path)
        if key not in self._regions:
            self._regions[key] = Region(path) if path.is_file() else None
        return self._regions[key]

    def region_files(self, dimension: str):
        region_dir = self.dimension_root(dimension) / "region"
        if not region_dir.is_dir():
            return []
        files = []
        for path in region_dir.iterdir():
            if path.is_file() and path.suffix == ".mca":
                try:
                    files.append(Region(path))
                except (NbtError, ValueError):
                    continue
        files.sort(key=lambda region: (region.region_x, region.region_z))
        return files

    def validate_region_names(self, dimension: str) -> int:
        """Raises if any region file cannot be parsed (the C++ side records it)."""
        return len(self.region_files(dimension))

    def chunk_nbt(self, dimension: str, chunk_x: int, chunk_z: int):
        key = (dimension, chunk_x, chunk_z)
        if key in self._chunks:
            return self._chunks[key]
        region = self.region(dimension, chunk_x, chunk_z)
        if region is None:
            self._chunks[key] = None
            return None
        local_x = chunk_x - (chunk_x // 32) * 32
        local_z = chunk_z - (chunk_z // 32) * 32
        payload = region.read_chunk(local_x, local_z)
        if payload is None:
            self._chunks[key] = None
            return None
        _, root = parse_nbt(payload)
        if not isinstance(root, dict):
            raise NbtError(f"chunk {chunk_x},{chunk_z} root is not a compound")
        self._chunks[key] = root
        return root

    def chunk_present(self, dimension: str, chunk_x: int, chunk_z: int) -> bool:
        region = self.region(dimension, chunk_x, chunk_z)
        if region is None:
            return False
        local_x = chunk_x - (chunk_x // 32) * 32
        local_z = chunk_z - (chunk_z // 32) * 32
        return region.has_chunk(local_x, local_z)

    def sections(self, dimension: str, chunk_x: int, chunk_z: int):
        root = self.chunk_nbt(dimension, chunk_x, chunk_z)
        if root is None:
            return None
        sections = {}
        for section in root.get("sections", []):
            if not isinstance(section, dict):
                raise NbtError("section entry is not a compound")
            y = section.get("Y")
            if not isinstance(y, int):
                raise NbtError("section has no integer Y")
            sections[y] = prepare_section(section)
        return sections

    def block_at(self, dimension: str, x: int, y: int, z: int):
        chunk_x = x // 16
        chunk_z = z // 16
        sections = self.sections(dimension, chunk_x, chunk_z)
        if sections is None:
            return "minecraft:air", {}, False
        section_y = y // 16
        section = sections.get(section_y)
        if section is None:
            return "minecraft:air", {}, True
        local_x = x - chunk_x * 16
        local_y = y - section_y * 16
        local_z = z - chunk_z * 16
        value = section["values"]()
        index = (local_y << 8) | (local_z << 4) | local_x
        if index >= len(value):
            raise NbtError("block index out of range")
        palette_index = value[index]
        if palette_index >= len(section["palette"]):
            raise NbtError("block palette index out of range")
        name, properties = section["palette"][palette_index]
        return name, properties, True

    def biome_at(self, dimension: str, x: int, y: int, z: int):
        chunk_x = x // 16
        chunk_z = z // 16
        sections = self.sections(dimension, chunk_x, chunk_z)
        if sections is None:
            return None
        section_y = y // 16
        section = sections.get(section_y)
        if section is None or not section["biome_palette"]:
            return None
        local_x = x - chunk_x * 16
        local_y = y - section_y * 16
        local_z = z - chunk_z * 16
        cells = section["biome_values"]()
        index = ((local_y >> 2) << 4) | ((local_z >> 2) << 2) | (local_x >> 2)
        return section["biome_palette"][cells[index]]

    def chunk_summary(self, dimension: str, chunk_x: int, chunk_z: int):
        root = self.chunk_nbt(dimension, chunk_x, chunk_z)
        if root is None:
            return None
        sections = self.sections(dimension, chunk_x, chunk_z)
        if sections is None:
            return None
        non_empty = 0
        palette_entries = 0
        for section in sections.values():
            palette_entries += len(section["palette"])
            if any(name not in AIR_NAMES for name, _ in section["palette"]):
                non_empty += 1
        ys = sorted(sections)
        return {
            "status": root.get("Status", ""),
            "data_version": root.get("DataVersion", 0),
            "sections": len(sections),
            "non_empty_sections": non_empty,
            "palette_entries": palette_entries,
            "light_sections": sum(
                1
                for section in sections.values()
                if section["sky_light"] is not None
                or section["block_light"] is not None
            ),
            "min_y": ys[0] * 16 if ys else None,
            "max_y": ys[-1] * 16 + 15 if ys else None,
        }


def bits_for_size(size: int, minimum: int) -> int:
    bits = minimum
    while (1 << bits) < size:
        bits += 1
    return bits


def unpack_longs(longs, bits: int, count: int):
    per_word = 64 // bits
    required = (count + per_word - 1) // per_word
    if len(longs) < required:
        raise NbtError(
            f"packed data has {len(longs)} words, needs {required} at {bits} bits"
        )
    mask = (1 << bits) - 1
    values = [0] * count
    for index in range(count):
        word = longs[index // per_word]
        values[index] = (word >> ((index % per_word) * bits)) & mask
    return values


def array_values(value):
    if isinstance(value, Array):
        return list(value.values)
    if isinstance(value, list):
        return value
    raise NbtError("expected an array")


def parse_block_palette(block_states) -> list[tuple[str, dict]]:
    palette = block_states.get("palette")
    if not isinstance(palette, list) or not palette:
        raise NbtError("block palette missing")
    result = []
    for entry in palette:
        if isinstance(entry, str):
            result.append((entry, {}))
            continue
        if not isinstance(entry, dict):
            raise NbtError("unsupported block palette entry")
        name = entry.get("")
        if not isinstance(name, str):
            name = entry.get("id")
        if not isinstance(name, str):
            name = entry.get("Name")
        if not isinstance(name, str):
            raise NbtError("block palette entry has no id")
        properties = entry.get("properties", entry.get("Properties", {}))
        if not isinstance(properties, dict):
            raise NbtError("block properties is not a compound")
        result.append((name, {str(key): str(value) for key, value in properties.items()}))
    return result


def parse_biome_palette(biomes) -> list[str]:
    palette = biomes.get("palette")
    if not isinstance(palette, list) or not palette:
        raise NbtError("biome palette missing")
    result = []
    for entry in palette:
        if isinstance(entry, str):
            result.append(entry)
        elif isinstance(entry, dict):
            name = entry.get("", entry.get("id", entry.get("Name")))
            if not isinstance(name, str):
                raise NbtError("biome palette entry has no id")
            result.append(name)
        else:
            raise NbtError("unsupported biome palette entry")
    return result


def prepare_section(section: dict):
    block_states = section.get("block_states")
    if not isinstance(block_states, dict):
        palette = [("minecraft:air", {})]
        values = lambda: [0] * 4096  # noqa: E731
    else:
        palette = parse_block_palette(block_states)
        if len(palette) == 1:
            values = lambda: [0] * 4096  # noqa: E731
        else:
            data = array_values(block_states.get("data"))
            bits = bits_for_size(len(palette), 4)
            packed = unpack_longs(data, bits, 4096)
            values = (lambda packed=packed: packed)  # noqa: E731

    def light_layer(key):
        layer = section.get(key)
        if layer is None:
            return None
        values = layer.values if isinstance(layer, Array) else layer
        if len(values) != 2048:
            raise NbtError(f"{key} has {len(values)} bytes, expected 2048")
        return bytes(values)

    biomes = section.get("biomes")
    if not isinstance(biomes, dict):
        biome_palette = []
        biome_values = lambda: [0] * 64  # noqa: E731
    else:
        biome_palette = parse_biome_palette(biomes)
        if len(biome_palette) == 1:
            biome_values = lambda: [0] * 64  # noqa: E731
        else:
            data = array_values(biomes.get("data"))
            bits = bits_for_size(len(biome_palette), 1)
            packed = unpack_longs(data, bits, 64)
            biome_values = (lambda packed=packed: packed)  # noqa: E731

    return {
        "palette": palette,
        "values": values,
        "biome_palette": biome_palette,
        "biome_values": biome_values,
        "sky_light": light_layer("SkyLight"),
        "block_light": light_layer("BlockLight"),
    }


def level_info(world: WorldView) -> dict:
    _, root = parse_nbt_file(world.root / "level.dat")
    if not isinstance(root, dict):
        raise NbtError("level.dat root is not a compound")
    data = root.get("Data")
    state = data if isinstance(data, dict) else root
    version = state.get("Version", {})
    spawn = state.get("spawn")
    spawn_info = None
    if isinstance(spawn, dict):
        pos = array_values(spawn.get("pos"))
        if len(pos) >= 3:
            spawn_info = {
                "x": pos[0],
                "y": pos[1],
                "z": pos[2],
                "yaw": float(spawn.get("yaw", 0.0)),
                "pitch": float(spawn.get("pitch", 0.0)),
                "dimension": spawn.get("dimension", "minecraft:overworld"),
            }
    player = state.get("Player")
    player_info = None
    if isinstance(player, dict):
        pos = array_values(player.get("Pos"))
        rotation = array_values(player.get("Rotation"))
        player_info = {
            "x": pos[0],
            "y": pos[1],
            "z": pos[2],
            "yaw": float(rotation[0]),
            "pitch": float(rotation[1]),
            "dimension": player.get("Dimension", "minecraft:overworld"),
        }
    return {
        "data_version": state.get("DataVersion", root.get("DataVersion", 0)),
        "version": {
            "name": version.get("Name"),
            "id": version.get("Id", 0),
            "level_version": state.get("version", 0),
            "history": list(array_values(state.get("version_history", []))),
        },
        "level_name": state.get("LevelName"),
        "spawn": spawn_info,
        "player": player_info,
    }


def to_json(value, max_array: int = 64):
    if isinstance(value, Array):
        values = list(value.values)
        if len(values) > max_array:
            return {
                "type": value.kind,
                "length": len(values),
                "head": values[:max_array],
            }
        return {"type": value.kind, "length": len(values), "values": values}
    if isinstance(value, dict):
        return {key: to_json(item, max_array) for key, item in value.items()}
    if isinstance(value, list):
        return [to_json(item, max_array) for item in value]
    if isinstance(value, bytes):
        return {"type": "byte_array", "length": len(value)}
    if isinstance(value, float):
        return value
    return value


def emit(value):
    print(json.dumps(value, indent=2, ensure_ascii=False))


def cmd_nbt(args) -> int:
    _, root = parse_nbt_file(Path(args.file))
    emit(to_json(root, args.max_array))
    return 0


def cmd_level(args) -> int:
    emit(level_info(WorldView(Path(args.world))))
    return 0


def cmd_chunk(args) -> int:
    world = WorldView(Path(args.world))
    summary = world.chunk_summary(args.dimension, args.x, args.z)
    if summary is None:
        print(f"chunk {args.x},{args.z} is not present", file=sys.stderr)
        return 1
    summary["x"] = args.x
    summary["z"] = args.z
    summary["dimension"] = args.dimension
    emit(summary)
    return 0


def cmd_block(args) -> int:
    world = WorldView(Path(args.world))
    name, properties, present = world.block_at(args.dimension, args.x, args.y, args.z)
    biome = world.biome_at(args.dimension, args.x, args.y, args.z)
    result = {
        "dimension": args.dimension,
        "x": args.x,
        "y": args.y,
        "z": args.z,
        "chunk_present": present,
        "block": name,
        "properties": properties,
    }
    if biome is not None:
        result["biome"] = biome
    emit(result)
    return 0


def compare_value(failures, label, expected, actual):
    if expected != actual:
        failures.append(f"{label}: C++ report {expected!r}, oracle {actual!r}")


def cmd_compare(args) -> int:
    report = json.loads(Path(args.report).read_text(encoding="utf-8"))
    world_path = Path(args.world) if args.world else Path(report["world"]["path"])
    world = WorldView(world_path)
    failures = []
    checks = 0

    level = level_info(world)
    reported = report.get("level", {})
    compare_value(failures, "level.data_version", reported.get("data_version"),
                  level["data_version"])
    checks += 1
    reported_version = reported.get("version", {})
    compare_value(failures, "level.version.level_version",
                  reported_version.get("level_version"),
                  level["version"]["level_version"])
    compare_value(failures, "level.version.history", reported_version.get("history"),
                  level["version"]["history"])
    checks += 2
    reported_spawn = reported.get("spawn")
    if reported_spawn is not None:
        expected = {
            "x": reported_spawn["x"],
            "y": reported_spawn["y"],
            "z": reported_spawn["z"],
            "yaw": reported_spawn["yaw"],
            "pitch": reported_spawn["pitch"],
            "dimension": reported_spawn["dimension"],
        }
        compare_value(failures, "level.spawn", expected, level["spawn"])
        checks += 1

    for dimension in report.get("dimensions", []):
        dimension_id = dimension["id"]
        try:
            regions = world.region_files(dimension_id)
            present = sum(region.count_chunks() for region in regions)
        except (NbtError, ValueError) as error:
            failures.append(f"dimension {dimension_id}: oracle failed: {error}")
            continue
        compare_value(failures, f"{dimension_id}.regions", len(dimension["regions"]),
                      len(regions))
        compare_value(failures, f"{dimension_id}.chunks_present",
                      dimension["chunks_present"], present)
        checks += 2

    if args.check_chunks:
        for chunk in report.get("chunks", []):
            summary = world.chunk_summary(chunk["dimension"], chunk["x"], chunk["z"])
            label = f"chunk {chunk['dimension']} {chunk['x']},{chunk['z']}"
            if summary is None:
                failures.append(f"{label}: oracle says not present")
                continue
            for key in (
                "status",
                "data_version",
                "sections",
                "non_empty_sections",
                "palette_entries",
                "light_sections",
                "min_y",
                "max_y",
            ):
                compare_value(failures, f"{label}.{key}", chunk.get(key), summary.get(key))
                checks += 1

    for sample in report.get("samples", []):
        label = (
            f"sample {sample['dimension']} {sample['x']},{sample['y']},{sample['z']}"
        )
        name, properties, present = world.block_at(
            sample["dimension"], sample["x"], sample["y"], sample["z"]
        )
        compare_value(failures, f"{label}.chunk_present", sample["chunk_present"], present)
        compare_value(failures, f"{label}.block", sample["block"], name)
        compare_value(failures, f"{label}.properties", sample.get("properties", {}),
                      properties)
        checks += 3

    print(
        f"oracle compare: {checks} checks, {len(failures)} failure(s), "
        f"{len(report.get('samples', []))} sample(s)"
    )
    for failure in failures[:30]:
        print("  MISMATCH " + failure, file=sys.stderr)
    if len(failures) > 30:
        print(f"  ... {len(failures) - 30} more", file=sys.stderr)
    return 1 if failures else 0


def cmd_runtime(args) -> int:
    """Compare the Runtime's status.samples (M1-H4) against this oracle."""
    report = json.loads(Path(args.report).read_text(encoding="utf-8"))
    world = WorldView(Path(args.world))
    samples = report.get("status", {}).get("samples", [])
    failures = []
    checks = 0
    for sample in samples:
        dimension = sample.get("dimension", "minecraft:overworld")
        name, _properties, present = world.block_at(
            dimension, sample["x"], sample["y"], sample["z"]
        )
        label = f"sample {dimension} {sample['x']},{sample['y']},{sample['z']}"
        compare_value(failures, f"{label}.block", sample["block"], name)
        compare_value(failures, f"{label}.chunk_present", True, present)
        checks += 2
    print(
        f"oracle runtime compare: {checks} checks, {len(failures)} failure(s), "
        f"{len(samples)} sample(s)"
    )
    for failure in failures[:30]:
        print("  MISMATCH " + failure, file=sys.stderr)
    return 1 if failures else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    nbt = subcommands.add_parser("nbt", help="dump an NBT file (gzip/zlib/raw)")
    nbt.add_argument("file")
    nbt.add_argument("--max-array", type=int, default=64)
    nbt.set_defaults(func=cmd_nbt)

    level = subcommands.add_parser("level", help="dump level.dat metadata")
    level.add_argument("--world", required=True)
    level.set_defaults(func=cmd_level)

    chunk = subcommands.add_parser("chunk", help="summarize one chunk")
    chunk.add_argument("--world", required=True)
    chunk.add_argument("--x", type=int, required=True)
    chunk.add_argument("--z", type=int, required=True)
    chunk.add_argument("--dimension", default="minecraft:overworld")
    chunk.set_defaults(func=cmd_chunk)

    block = subcommands.add_parser("block", help="read one block")
    block.add_argument("--world", required=True)
    block.add_argument("--x", type=int, required=True)
    block.add_argument("--y", type=int, required=True)
    block.add_argument("--z", type=int, required=True)
    block.add_argument("--dimension", default="minecraft:overworld")
    block.set_defaults(func=cmd_block)

    compare = subcommands.add_parser(
        "compare", help="compare an inspect-world report with this oracle"
    )
    compare.add_argument("--world", default=None)
    compare.add_argument("--report", required=True)
    compare.add_argument("--check-chunks", action="store_true")
    compare.set_defaults(func=cmd_compare)

    runtime = subcommands.add_parser(
        "runtime", help="compare a reim-headless runtime report's block samples"
    )
    runtime.add_argument("--world", required=True)
    runtime.add_argument("--report", required=True)
    runtime.set_defaults(func=cmd_runtime)

    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (NbtError, OSError, ValueError, struct.error, zlib.error) as error:
        print(f"oracle error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
