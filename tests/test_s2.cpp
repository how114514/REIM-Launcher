#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "anvil/region.h"
#include "nbt/nbt.h"
#include "samples/packed_bits.h"
#include "util/compression.h"
#include "util/file.h"
#include "world/chunk.h"
#include "world/world.h"

#include "miniz.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace nbt = reim::nbt;

namespace {

using Bytes = std::vector<uint8_t>;

void append_be16(Bytes& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void append_be32(Bytes& out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
}

void append_be64(Bytes& out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
}

void append_string(Bytes& out, const std::string& value) {
  append_be16(out, static_cast<uint16_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

Bytes zigzag_string(const std::string& value) {
  Bytes out;
  append_string(out, value);
  return out;
}

/* Minimal NBT writer used only by these tests. */
class NbtWriter {
 public:
  static Bytes compound(std::initializer_list<std::pair<const char*, Bytes>> fields,
                        const std::string& name = {}) {
    Bytes out;
    out.push_back(10);
    append_string(out, name);
    for (const auto& [key, payload] : fields) {
      out.insert(out.end(), payload.begin(), payload.end());
      (void)key;
    }
    out.push_back(0);
    return out;
  }

  static Bytes tag(uint8_t type, const std::string& name, const Bytes& payload) {
    Bytes out;
    out.push_back(type);
    append_string(out, name);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
  }

  static Bytes byte(int8_t value) {
    return Bytes{static_cast<uint8_t>(value)};
  }
  static Bytes short_value(int16_t value) {
    Bytes out;
    append_be16(out, static_cast<uint16_t>(value));
    return out;
  }
  static Bytes int_value(int32_t value) {
    Bytes out;
    append_be32(out, static_cast<uint32_t>(value));
    return out;
  }
  static Bytes long_value(int64_t value) {
    Bytes out;
    append_be64(out, static_cast<uint64_t>(value));
    return out;
  }
  static Bytes float_value(float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    Bytes out;
    append_be32(out, raw);
    return out;
  }
  static Bytes double_value(double value) {
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    Bytes out;
    append_be64(out, raw);
    return out;
  }
  static Bytes string_value(const std::string& value) { return zigzag_string(value); }
  static Bytes byte_array(const Bytes& values) {
    Bytes out;
    append_be32(out, static_cast<uint32_t>(values.size()));
    out.insert(out.end(), values.begin(), values.end());
    return out;
  }
  static Bytes int_array(const std::vector<int32_t>& values) {
    Bytes out;
    append_be32(out, static_cast<uint32_t>(values.size()));
    for (int32_t value : values) append_be32(out, static_cast<uint32_t>(value));
    return out;
  }
  static Bytes long_array(const std::vector<int64_t>& values) {
    Bytes out;
    append_be32(out, static_cast<uint32_t>(values.size()));
    for (int64_t value : values) append_be64(out, static_cast<uint64_t>(value));
    return out;
  }
  static Bytes list(uint8_t element_type,
                    std::initializer_list<std::pair<const char*, Bytes>> items) {
    Bytes out;
    out.push_back(element_type);
    append_be32(out, static_cast<uint32_t>(items.size()));
    for (const auto& [key, payload] : items) {
      out.insert(out.end(), payload.begin(), payload.end());
      (void)key;
    }
    return out;
  }
};

Bytes raw_deflate(const Bytes& input) {
  size_t size = 0;
  void* heap = tdefl_compress_mem_to_heap(input.data(), input.size(), &size, 0);
  REQUIRE(heap != nullptr);
  Bytes out(static_cast<uint8_t*>(heap), static_cast<uint8_t*>(heap) + size);
  mz_free(heap);
  return out;
}

Bytes wrap_gzip(const Bytes& input) {
  const Bytes deflated = raw_deflate(input);
  Bytes out{0x1f, 0x8b, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xff};
  out.insert(out.end(), deflated.begin(), deflated.end());
  const auto crc = static_cast<uint32_t>(mz_crc32(0, input.data(), input.size()));
  const auto size = static_cast<uint32_t>(input.size());
  for (int shift = 0; shift <= 24; shift += 8) {
    out.push_back(static_cast<uint8_t>((crc >> shift) & 0xFF));
  }
  for (int shift = 0; shift <= 24; shift += 8) {
    out.push_back(static_cast<uint8_t>((size >> shift) & 0xFF));
  }
  return out;
}

Bytes zlib_compress(const Bytes& input) {
  mz_ulong size = mz_compressBound(static_cast<mz_ulong>(input.size()));
  Bytes out(static_cast<size_t>(size));
  REQUIRE(mz_compress(out.data(), &size, input.data(),
                      static_cast<mz_ulong>(input.size())) == MZ_OK);
  out.resize(static_cast<size_t>(size));
  return out;
}

/* ---- Document builders for chunk-level tests ---- */

nbt::Value make_int(int64_t value) {
  nbt::Value out;
  out.type = nbt::Tag::int_tag;
  out.integral = value;
  return out;
}

nbt::Value make_string(std::string value) {
  nbt::Value out;
  out.type = nbt::Tag::string;
  out.text = std::move(value);
  return out;
}

nbt::Value make_compound(
    std::initializer_list<std::pair<const char*, nbt::Value>> fields) {
  nbt::Value out;
  out.type = nbt::Tag::compound;
  out.compound = std::make_shared<nbt::Compound>();
  for (const auto& [key, value] : fields) {
    out.compound->insert_or_assign(key, value);
  }
  return out;
}

nbt::Value make_list(nbt::Tag element, std::vector<nbt::Value> items) {
  nbt::Value out;
  out.type = nbt::Tag::list;
  out.list = std::make_shared<nbt::List>();
  out.list->element_type = element;
  out.list->items = std::move(items);
  return out;
}

nbt::Value make_longs(std::vector<int64_t> values) {
  nbt::Value out;
  out.type = nbt::Tag::long_array;
  out.longs = std::move(values);
  return out;
}

nbt::Value make_bytes(std::vector<uint8_t> values) {
  nbt::Value out;
  out.type = nbt::Tag::byte_array;
  out.bytes = std::move(values);
  return out;
}

nbt::Value make_properties(
    std::initializer_list<std::pair<const char*, nbt::Value>> fields) {
  nbt::Value out;
  out.type = nbt::Tag::compound;
  out.compound = std::make_shared<nbt::Compound>();
  for (const auto& [key, value] : fields) {
    out.compound->insert_or_assign(key, value);
  }
  return out;
}

} // namespace

TEST_CASE("NBT parses every tag type") {
  const std::string nul_string =
      std::string("A") + static_cast<char>(0xC0) + static_cast<char>(0x80) + "B";
  const std::string surrogate_text("\xED\xA0\xBD\xED\xB8\x80", 6);
  const Bytes surrogate(surrogate_text.begin(), surrogate_text.end());

  Bytes root;
  root.push_back(10);
  append_string(root, "");
  auto add = [&](const Bytes& field) { root.insert(root.end(), field.begin(), field.end()); };
  add(NbtWriter::tag(1, "b", NbtWriter::byte(-5)));
  add(NbtWriter::tag(2, "s", NbtWriter::short_value(-300)));
  add(NbtWriter::tag(3, "i", NbtWriter::int_value(-70000)));
  add(NbtWriter::tag(4, "l", NbtWriter::long_value(1234567890123LL)));
  add(NbtWriter::tag(5, "f", NbtWriter::float_value(1.5f)));
  add(NbtWriter::tag(6, "d", NbtWriter::double_value(-2.25)));
  add(NbtWriter::tag(7, "ba", NbtWriter::byte_array({1, 2, 3})));
  add(NbtWriter::tag(8, "str", NbtWriter::string_value(nul_string)));
  add(NbtWriter::tag(8, "emoji", NbtWriter::string_value(std::string(surrogate.begin(), surrogate.end()))));
  add(NbtWriter::tag(9, "li",
                     NbtWriter::list(3, {{"", NbtWriter::int_value(1)},
                                         {"", NbtWriter::int_value(2)},
                                         {"", NbtWriter::int_value(3)}})));
  add(NbtWriter::compound(
      {{"x", NbtWriter::tag(3, "x", NbtWriter::int_value(7))}}, "c"));
  add(NbtWriter::tag(11, "ia", NbtWriter::int_array({10, -20})));
  add(NbtWriter::tag(12, "la", NbtWriter::long_array({1, -2, 3})));
  root.push_back(0);

  nbt::Document document;
  std::string error;
  const bool parsed = nbt::parse(root, document, error);
  INFO("parse error: " << error);
  REQUIRE(parsed);
  REQUIRE(error.empty());
  REQUIRE(document.root_name.empty());
  REQUIRE(document.root.type == nbt::Tag::compound);

  REQUIRE(nbt::as_int(*nbt::find(document.root, "b")) == -5);
  REQUIRE(nbt::as_int(*nbt::find(document.root, "s")) == -300);
  REQUIRE(nbt::as_int(*nbt::find(document.root, "i")) == -70000);
  REQUIRE(nbt::as_int(*nbt::find(document.root, "l")) == 1234567890123LL);
  REQUIRE(nbt::as_number(*nbt::find(document.root, "f")) == Approx(1.5));
  REQUIRE(nbt::as_number(*nbt::find(document.root, "d")) == Approx(-2.25));
  const nbt::Value* byte_array = nbt::find(document.root, "ba");
  REQUIRE(byte_array != nullptr);
  REQUIRE(byte_array->bytes == std::vector<uint8_t>{1, 2, 3});
  REQUIRE(*nbt::as_string(*nbt::find(document.root, "str")) == std::string("A\0B", 3));
  REQUIRE(*nbt::as_string(*nbt::find(document.root, "emoji")) == "\xF0\x9F\x98\x80");
  const nbt::Value* list = nbt::find(document.root, "li");
  REQUIRE(list != nullptr);
  REQUIRE(list->list->items.size() == 3);
  REQUIRE(nbt::as_int(*nbt::item(*list, 2)) == 3);
  REQUIRE(nbt::as_int(*nbt::find(*nbt::find(document.root, "c"), "x")) == 7);
  const nbt::Value* ints = nbt::find(document.root, "ia");
  REQUIRE(ints != nullptr);
  REQUIRE(ints->ints == std::vector<int32_t>{10, -20});
  const nbt::Value* longs = nbt::find(document.root, "la");
  REQUIRE(longs != nullptr);
  REQUIRE(longs->longs == std::vector<int64_t>{1, -2, 3});
}

TEST_CASE("NBT rejects truncated and trailing input") {
  Bytes root;
  root.push_back(10);
  append_string(root, "");
  const Bytes field = NbtWriter::tag(3, "x", NbtWriter::int_value(42));
  root.insert(root.end(), field.begin(), field.end());
  root.push_back(0);

  nbt::Document document;
  std::string error;
  REQUIRE(nbt::parse(root, document, error));
  for (size_t cut = 0; cut < root.size(); ++cut) {
    Bytes truncated(root.begin(), root.begin() + static_cast<ptrdiff_t>(cut));
    nbt::Document ignored;
    std::string cut_error;
    REQUIRE_FALSE(nbt::parse(truncated, ignored, cut_error));
    REQUIRE_FALSE(cut_error.empty());
  }
  Bytes trailing = root;
  trailing.push_back(0x7f);
  nbt::Document ignored;
  std::string trailing_error;
  REQUIRE_FALSE(nbt::parse(trailing, ignored, trailing_error));
  REQUIRE(trailing_error.find("trailing") != std::string::npos);
}

TEST_CASE("gzip and zlib decompression round-trip") {
  const std::string text("hello\0nbt\n", 10);
  const Bytes payload(text.begin(), text.end());
  std::vector<uint8_t> out;
  std::string error;

  const Bytes gzipped = wrap_gzip(payload);
  REQUIRE(reim::compression::gzip_decompress(gzipped.data(), gzipped.size(), out, error));
  REQUIRE(out == payload);

  const Bytes zlibbed = zlib_compress(payload);
  REQUIRE(reim::compression::zlib_decompress(zlibbed.data(), zlibbed.size(), out, error));
  REQUIRE(out == payload);

  REQUIRE(reim::compression::decompress_auto(gzipped.data(), gzipped.size(), out, error));
  REQUIRE(out == payload);

  Bytes corrupt = gzipped;
  corrupt[corrupt.size() - 1] ^= 0xFF;
  REQUIRE_FALSE(reim::compression::decompress_auto(corrupt.data(), corrupt.size(), out,
                                                   error));
}

TEST_CASE("Anvil region reads a synthetic zlib chunk") {
  NbtWriter writer;
  Bytes nbt = writer.compound({});
  const Bytes compressed = zlib_compress(nbt);

  const auto dir = std::filesystem::temp_directory_path() / "reim-s2-region-test";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  const auto path = dir / "r.2.-3.mca";

  Bytes region(5 * 4096, 0);
  auto set_location = [&region](int local_x, int local_z, uint32_t sector) {
    const size_t index = static_cast<size_t>(local_z * 32 + local_x);
    const uint32_t word = (sector << 8) | 1u;
    region[index * 4 + 0] = static_cast<uint8_t>(word >> 24);
    region[index * 4 + 1] = static_cast<uint8_t>(word >> 16);
    region[index * 4 + 2] = static_cast<uint8_t>(word >> 8);
    region[index * 4 + 3] = static_cast<uint8_t>(word);
  };

  const int local_x = 3;
  const int local_z = 2;
  set_location(local_x, local_z, 2u);
  const uint32_t length = static_cast<uint32_t>(compressed.size()) + 1;
  const uint32_t raw_length = length;
  region[8192 + 0] = static_cast<uint8_t>(raw_length >> 24);
  region[8192 + 1] = static_cast<uint8_t>(raw_length >> 16);
  region[8192 + 2] = static_cast<uint8_t>(raw_length >> 8);
  region[8192 + 3] = static_cast<uint8_t>(raw_length);
  region[8192 + 4] = 2;
  std::copy(compressed.begin(), compressed.end(), region.begin() + 8192 + 5);

  // Corrupt chunk: its payload length claims more than the one allocated sector.
  set_location(4, 2, 4u);
  region[16384 + 0] = 0x00;
  region[16384 + 1] = 0x00;
  region[16384 + 2] = 0x27;
  region[16384 + 3] = 0x10;
  region[16384 + 4] = 2;

  // External chunk: 0x80 | 2, payload lives in c.5.2.mcc.
  set_location(5, 2, 3u);
  region[12288 + 0] = 0x00;
  region[12288 + 1] = 0x00;
  region[12288 + 2] = 0x00;
  region[12288 + 3] = 0x01;
  region[12288 + 4] = static_cast<uint8_t>(0x80 | 2);

  std::string error;
  REQUIRE(reim::file::write_all(dir / "c.5.2.mcc", compressed.data(),
                                compressed.size(), error));
  REQUIRE(reim::file::write_all(path, region.data(), region.size(), error));
  REQUIRE(error.empty());

  reim::anvil::Region file;
  REQUIRE(file.open(path, error));
  REQUIRE(file.region_x() == 2);
  REQUIRE(file.region_z() == -3);
  REQUIRE(file.has_chunk(local_x, local_z));
  REQUIRE_FALSE(file.has_chunk(0, 0));
  REQUIRE(file.has_chunk(4, 2));
  REQUIRE(file.has_chunk(5, 2));
  const auto present = file.present();
  REQUIRE(present.size() == 3);

  std::vector<uint8_t> payload;
  REQUIRE(file.read_chunk(local_x, local_z, payload, error));
  REQUIRE(payload == nbt);
  REQUIRE_FALSE(file.read_chunk(0, 0, payload, error));
  REQUIRE_FALSE(file.read_chunk(4, 2, payload, error));
  REQUIRE(payload.empty());
  REQUIRE(file.read_chunk(5, 2, payload, error));
  REQUIRE(payload == nbt);

  std::filesystem::remove_all(dir, ec);
}

TEST_CASE("26.3 chunk palette forms decode to blocks and biomes") {
  std::vector<uint32_t> indices(4096, 0);
  const size_t stone_index =
      (static_cast<size_t>(2) << 8) | (static_cast<size_t>(3) << 4) | 1;
  indices[stone_index] = 1;
  auto packed = reim::samples::pack_packed(indices.data(), indices.size(), 4);

  std::vector<uint32_t> biome_cells(64, 0);
  biome_cells[(0 << 4) | (1 << 2) | 1] = 1;
  auto biome_packed = reim::samples::pack_packed(biome_cells.data(), biome_cells.size(), 1);

  nbt::Value section = make_compound({
      {"Y", make_int(0)},
      {"block_states",
       make_compound({{"palette", make_list(nbt::Tag::string,
                                            {make_string("minecraft:air"),
                                             make_compound({{"", make_string("minecraft:stone")}})})},
                      {"data", make_longs(packed)}})},
      {"biomes", make_compound({{"palette", make_list(nbt::Tag::string,
                                                      {make_string("minecraft:plains"),
                                                       make_string("minecraft:desert")})},
                                {"data", make_longs(biome_packed)}})},
      {"SkyLight", make_bytes(std::vector<uint8_t>(2048, 0xAB))},
  });

  std::vector<uint32_t> upper_indices(4096, 0);
  upper_indices[0] = 1;
  auto upper_packed = reim::samples::pack_packed(upper_indices.data(), 4096, 4);
  nbt::Value upper_section = make_compound({
      {"Y", make_int(1)},
      {"block_states",
       make_compound(
           {{"palette",
             make_list(nbt::Tag::compound,
                       {make_compound({{"", make_string("minecraft:air")}}),
                        make_compound({{"id", make_string("minecraft:grass_block")},
                                       {"properties",
                                        make_properties(
                                            {{"snowy", make_string("true")}})}})})},
            {"data", make_longs(upper_packed)}})},
  });

  nbt::Document document;
  document.root = make_compound({
      {"xPos", make_int(2)},
      {"zPos", make_int(3)},
      {"yPos", make_int(-4)},
      {"DataVersion", make_int(5023)},
      {"Status", make_string("minecraft:full")},
      {"sections", make_list(nbt::Tag::compound, {section, upper_section})},
  });

  reim::world::Chunk chunk;
  std::string error;
  REQUIRE(reim::world::parse_chunk(document, 2, 3, chunk, error));
  REQUIRE(chunk.x == 2);
  REQUIRE(chunk.z == 3);
  REQUIRE(chunk.data_version == 5023);
  REQUIRE(chunk.status == "minecraft:full");
  REQUIRE(chunk.sections.size() == 2);
  REQUIRE(chunk.sections[0].y == 0);
  REQUIRE(chunk.sections[1].y == 1);
  REQUIRE(chunk.non_empty_sections() == 2);

  REQUIRE(chunk.block(2 * 16 + 1, 2, 3 * 16 + 3).name == "minecraft:stone");
  REQUIRE(chunk.block(2 * 16 + 0, 2, 3 * 16 + 3).name == "minecraft:air");
  REQUIRE(chunk.block(2 * 16 + 0, 16, 3 * 16 + 0).name == "minecraft:grass_block");
  const auto& grass = chunk.block(2 * 16 + 0, 16, 3 * 16 + 0);
  REQUIRE(grass.properties.at("snowy") == "true");
  REQUIRE(chunk.biome(2 * 16 + 4, 0, 3 * 16 + 4) == "minecraft:desert");
  REQUIRE(chunk.biome(2 * 16 + 0, 0, 3 * 16 + 0) == "minecraft:plains");

  // Nibble order: even index = low nibble, odd index = high nibble.
  REQUIRE(chunk.sky_light(2 * 16 + 1, 2, 3 * 16 + 3) == 0xA);
  REQUIRE(chunk.sky_light(2 * 16 + 0, 0, 3 * 16 + 0) == 0xB);
  REQUIRE(chunk.block_light(2 * 16 + 1, 2, 3 * 16 + 3) == 0);
  REQUIRE(chunk.light_sections() == 1);

  // A chunk whose xPos disagrees with its region slot is rejected.
  document.root.compound->insert_or_assign("xPos", make_int(9));
  reim::world::Chunk misplaced;
  REQUIRE_FALSE(reim::world::parse_chunk(document, 2, 3, misplaced, error));
  REQUIRE(error.find("xPos") != std::string::npos);
}

TEST_CASE("real 26.3 world loads level data and blocks") {
  const char* world_env = std::getenv("REIM_TEST_WORLD");
  if (world_env == nullptr || *world_env == '\0') {
    WARN("REIM_TEST_WORLD not set; skipping real-world integration test");
    return;
  }
  const auto path = reim::file::from_utf8(world_env);
  reim::world::World world;
  std::string error;
  REQUIRE(world.open(path, error));

  const reim::world::LevelInfo& level = world.level();
  REQUIRE(level.data_version == 5023);
  REQUIRE(level.spawn.present);
  REQUIRE(level.spawn.x == Approx(16.0));
  REQUIRE(level.spawn.y == Approx(65.0));
  REQUIRE(level.spawn.z == Approx(80.0));
  REQUIRE(level.spawn.dimension == "minecraft:overworld");
  REQUIRE(level.level_version == 19133);
  REQUIRE_FALSE(level.player.present);

  const reim::world::DimensionInfo* overworld = world.dimension("minecraft:overworld");
  REQUIRE(overworld != nullptr);
  REQUIRE(overworld->region_files.size() == 4);

  reim::world::Chunk chunk;
  const auto status = world.load_chunk(*overworld, 1, 5, chunk, error);
  INFO("load error: " << error);
  REQUIRE(status == reim::world::ChunkLoad::ok);
  REQUIRE(chunk.status == "minecraft:full");
  REQUIRE(chunk.data_version == 5023);
  REQUIRE(chunk.sections.size() == 24);
  REQUIRE(chunk.non_empty_sections() == 9);
  REQUIRE(chunk.min_y == -64);
  REQUIRE(chunk.max_y == 319);

  struct Check {
    int y;
    const char* block;
  };
  const Check checks[] = {{-64, "minecraft:bedrock"},
                          {0, "minecraft:deepslate"},
                          {64, "minecraft:grass_block"},
                          {65, "minecraft:snow"},
                          {66, "minecraft:air"}};
  for (const Check& check : checks) {
    const auto& state = chunk.block(16, check.y, 80);
    INFO("block at y=" << check.y << " is " << state.name);
    REQUIRE(state.name == check.block);
  }
  const auto& grass = chunk.block(16, 64, 80);
  REQUIRE(grass.properties.at("snowy") == "true");
  REQUIRE(chunk.biome(16, 0, 80) == "minecraft:snowy_plains");
  REQUIRE(chunk.sky_light(16, 65, 80) == 15);
  REQUIRE(chunk.sky_light(16, 64, 80) == 0);
  REQUIRE(chunk.block_light(16, 65, 80) == 0);
  REQUIRE(chunk.light_sections() > 0);
}
