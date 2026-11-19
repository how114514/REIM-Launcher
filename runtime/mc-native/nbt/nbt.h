#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reim::nbt {

/*
 * Read-only NBT (Named Binary Tag) reader for Minecraft 26.3.
 *
 * - Big-endian scalars.
 * - Strings use Java "modified UTF-8" (NUL = C0 80, surrogate pairs); they are
 *   converted to standard UTF-8 on read.
 * - Compound keys use a sorted map so iteration (and therefore any diagnostics
 *   built from it) is deterministic.
 * - Lists carry their element tag; compound list elements have no names.
 * - Parsing is bounded: depth and per-container sizes are checked against the
 *   input before allocation, and malformed data returns false with a message.
 *   No exception crosses this interface.
 */

enum class Tag : uint8_t {
  end = 0,
  byte = 1,
  short_tag = 2,
  int_tag = 3,
  long_tag = 4,
  float_tag = 5,
  double_tag = 6,
  byte_array = 7,
  string = 8,
  list = 9,
  compound = 10,
  int_array = 11,
  long_array = 12
};

struct Value;

using Compound = std::map<std::string, Value, std::less<>>;

struct List {
  Tag element_type = Tag::end;
  std::vector<Value> items;
};

struct Value {
  Tag type = Tag::end;
  int64_t integral = 0;  // byte / short / int / long
  double real = 0.0;     // float / double
  std::string text;      // string
  std::vector<uint8_t> bytes;   // byte array
  std::vector<int32_t> ints;    // int array
  std::vector<int64_t> longs;   // long array
  std::shared_ptr<List> list;
  std::shared_ptr<Compound> compound;
};

struct Document {
  std::string root_name;
  Value root;
};

/* Parses a complete, already-decompressed NBT payload. Trailing bytes are an
 * error: an Anvil payload is exactly one root tag. */
bool parse(const uint8_t* data, size_t size, Document& out, std::string& error);
bool parse(const std::vector<uint8_t>& data, Document& out, std::string& error);

/* Reads `path`, detects gzip/zlib by header, decompresses if needed, then
 * parses. Used for level.dat and for debugging NBT files. */
bool parse_file(const std::filesystem::path& path, Document& out, std::string& error);

/* Parses bytes that are already decompressed (Anvil chunk payloads). */
bool parse_payload(const std::vector<uint8_t>& data, Document& out, std::string& error);

const Value* find(const Value& value, std::string_view key);
const Value* item(const Value& value, size_t index);
const std::string* as_string(const Value& value);
std::optional<int64_t> as_int(const Value& value);
std::optional<double> as_number(const Value& value);
bool is_air_name(std::string_view block_name);
const char* tag_name(Tag tag);

} // namespace reim::nbt
