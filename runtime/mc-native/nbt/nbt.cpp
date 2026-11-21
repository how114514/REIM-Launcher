#include "nbt/nbt.h"

#include "util/compression.h"
#include "util/file.h"
#include "util/utf.h"

#include <bit>
#include <cstring>
#include <limits>

namespace reim::nbt {
namespace {

constexpr int kMaxDepth = 512;

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  size_t position() const { return position_; }
  size_t remaining() const { return size_ - position_; }
  bool exhausted() const { return position_ >= size_; }

  bool read_u8(uint8_t& out) {
    if (remaining() < 1) return false;
    out = data_[position_++];
    return true;
  }

  bool read_bytes(size_t count, const uint8_t*& out) {
    if (count > remaining()) return false;
    out = data_ + position_;
    position_ += count;
    return true;
  }

  bool read_u16(uint16_t& out) {
    if (remaining() < 2) return false;
    out = static_cast<uint16_t>((static_cast<uint16_t>(data_[position_]) << 8) |
                                data_[position_ + 1]);
    position_ += 2;
    return true;
  }

  bool read_i16(int16_t& out) {
    uint16_t raw = 0;
    if (!read_u16(raw)) return false;
    out = std::bit_cast<int16_t>(raw);
    return true;
  }

  bool read_i32(int32_t& out) {
    if (remaining() < 4) return false;
    const uint32_t raw =
        (static_cast<uint32_t>(data_[position_]) << 24) |
        (static_cast<uint32_t>(data_[position_ + 1]) << 16) |
        (static_cast<uint32_t>(data_[position_ + 2]) << 8) |
        static_cast<uint32_t>(data_[position_ + 3]);
    position_ += 4;
    out = std::bit_cast<int32_t>(raw);
    return true;
  }

  bool read_i64(int64_t& out) {
    if (remaining() < 8) return false;
    uint64_t raw = 0;
    for (int i = 0; i < 8; ++i) {
      raw = (raw << 8) | data_[position_ + static_cast<size_t>(i)];
    }
    position_ += 8;
    out = std::bit_cast<int64_t>(raw);
    return true;
  }

  bool read_f32(float& out) {
    if (remaining() < 4) return false;
    uint32_t raw = 0;
    for (int i = 0; i < 4; ++i) {
      raw = (raw << 8) | data_[position_ + static_cast<size_t>(i)];
    }
    position_ += 4;
    out = std::bit_cast<float>(raw);
    return true;
  }

  bool read_f64(double& out) {
    if (remaining() < 8) return false;
    uint64_t raw = 0;
    for (int i = 0; i < 8; ++i) {
      raw = (raw << 8) | data_[position_ + static_cast<size_t>(i)];
    }
    position_ += 8;
    out = std::bit_cast<double>(raw);
    return true;
  }

  bool read_string(std::string& out) {
    uint16_t length = 0;
    if (!read_u16(length)) return false;
    const uint8_t* bytes = nullptr;
    if (!read_bytes(length, bytes)) return false;
    out = reim::utf::modified_utf8_to_utf8(
        reinterpret_cast<const char*>(bytes), length);
    return true;
  }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t position_ = 0;
};

std::string at_offset(std::string_view message, size_t offset) {
  return std::string(message) + " at offset " + std::to_string(offset);
}

bool parse_value(Reader& reader, Tag tag, int depth, Value& out, std::string& error);

bool parse_compound(Reader& reader, int depth, Value& out, std::string& error) {
  if (depth > kMaxDepth) {
    error = at_offset("NBT depth limit exceeded", reader.position());
    return false;
  }
  auto compound = std::make_shared<Compound>();
  for (;;) {
    uint8_t raw_tag = 0;
    if (!reader.read_u8(raw_tag)) {
      error = at_offset("truncated compound", reader.position());
      return false;
    }
    if (raw_tag == static_cast<uint8_t>(Tag::end)) break;
    if (raw_tag > static_cast<uint8_t>(Tag::long_array)) {
      error = at_offset("unknown tag id " + std::to_string(raw_tag), reader.position());
      return false;
    }
    std::string name;
    if (!reader.read_string(name)) {
      error = at_offset("truncated compound key", reader.position());
      return false;
    }
    Value child;
    if (!parse_value(reader, static_cast<Tag>(raw_tag), depth + 1, child, error)) {
      return false;
    }
    compound->insert_or_assign(std::move(name), std::move(child));
  }
  out.type = Tag::compound;
  out.compound = std::move(compound);
  return true;
}

bool parse_list(Reader& reader, int depth, Value& out, std::string& error) {
  if (depth > kMaxDepth) {
    error = at_offset("NBT depth limit exceeded", reader.position());
    return false;
  }
  uint8_t raw_element = 0;
  if (!reader.read_u8(raw_element)) {
    error = at_offset("truncated list header", reader.position());
    return false;
  }
  int32_t count = 0;
  if (!reader.read_i32(count)) {
    error = at_offset("truncated list length", reader.position());
    return false;
  }
  if (count < 0) {
    error = at_offset("negative list length", reader.position());
    return false;
  }
  if (raw_element > static_cast<uint8_t>(Tag::long_array)) {
    error = at_offset("unknown list element tag", reader.position());
    return false;
  }
  const auto element = static_cast<Tag>(raw_element);
  if (element == Tag::end && count != 0) {
    error = at_offset("non-empty list of TAG_End", reader.position());
    return false;
  }
  // Every element occupies at least one byte, so a count larger than the
  // remaining input can never succeed. This rejects absurd allocations from
  // corrupt lengths before reserving.
  if (static_cast<uint64_t>(count) > reader.remaining()) {
    error = at_offset("list length exceeds input", reader.position());
    return false;
  }
  auto list = std::make_shared<List>();
  list->element_type = element;
  list->items.reserve(static_cast<size_t>(count));
  for (int32_t i = 0; i < count; ++i) {
    Value child;
    if (!parse_value(reader, element, depth + 1, child, error)) return false;
    list->items.push_back(std::move(child));
  }
  out.type = Tag::list;
  out.list = std::move(list);
  return true;
}

bool parse_value(Reader& reader, Tag tag, int depth, Value& out, std::string& error) {
  if (depth > kMaxDepth) {
    error = at_offset("NBT depth limit exceeded", reader.position());
    return false;
  }
  out = Value{};
  out.type = tag;
  switch (tag) {
    case Tag::end:
      error = at_offset("unexpected TAG_End", reader.position());
      return false;
    case Tag::byte: {
      uint8_t raw = 0;
      if (!reader.read_u8(raw)) break;
      out.integral = static_cast<int8_t>(raw);
      return true;
    }
    case Tag::short_tag: {
      int16_t value = 0;
      if (!reader.read_i16(value)) break;
      out.integral = value;
      return true;
    }
    case Tag::int_tag: {
      int32_t value = 0;
      if (!reader.read_i32(value)) break;
      out.integral = value;
      return true;
    }
    case Tag::long_tag: {
      int64_t value = 0;
      if (!reader.read_i64(value)) break;
      out.integral = value;
      return true;
    }
    case Tag::float_tag: {
      float value = 0.0f;
      if (!reader.read_f32(value)) break;
      out.real = value;
      return true;
    }
    case Tag::double_tag: {
      double value = 0.0;
      if (!reader.read_f64(value)) break;
      out.real = value;
      return true;
    }
    case Tag::byte_array: {
      int32_t count = 0;
      if (!reader.read_i32(count)) break;
      if (count < 0) {
        error = at_offset("negative byte array length", reader.position());
        return false;
      }
      const uint8_t* bytes = nullptr;
      if (!reader.read_bytes(static_cast<size_t>(count), bytes)) break;
      out.bytes.assign(bytes, bytes + count);
      return true;
    }
    case Tag::string:
      if (!reader.read_string(out.text)) break;
      return true;
    case Tag::list:
      return parse_list(reader, depth, out, error);
    case Tag::compound:
      return parse_compound(reader, depth, out, error);
    case Tag::int_array: {
      int32_t count = 0;
      if (!reader.read_i32(count)) break;
      if (count < 0) {
        error = at_offset("negative int array length", reader.position());
        return false;
      }
      const uint64_t bytes = static_cast<uint64_t>(count) * 4;
      if (bytes > reader.remaining()) break;
      out.ints.resize(static_cast<size_t>(count));
      for (int32_t i = 0; i < count; ++i) {
        int32_t value = 0;
        reader.read_i32(value);
        out.ints[static_cast<size_t>(i)] = value;
      }
      return true;
    }
    case Tag::long_array: {
      int32_t count = 0;
      if (!reader.read_i32(count)) break;
      if (count < 0) {
        error = at_offset("negative long array length", reader.position());
        return false;
      }
      const uint64_t bytes = static_cast<uint64_t>(count) * 8;
      if (bytes > reader.remaining()) break;
      out.longs.resize(static_cast<size_t>(count));
      for (int32_t i = 0; i < count; ++i) {
        int64_t value = 0;
        reader.read_i64(value);
        out.longs[static_cast<size_t>(i)] = value;
      }
      return true;
    }
    default:
      error = at_offset("unknown tag id", reader.position());
      return false;
  }
  error = at_offset("truncated tag payload", reader.position());
  return false;
}

} // namespace

bool parse(const uint8_t* data, size_t size, Document& out, std::string& error) {
  out = Document{};
  if (data == nullptr || size == 0) {
    error = "empty NBT payload";
    return false;
  }
  Reader reader(data, size);
  uint8_t raw_tag = 0;
  if (!reader.read_u8(raw_tag)) {
    error = "empty NBT payload";
    return false;
  }
  if (raw_tag == static_cast<uint8_t>(Tag::end)) {
    error = "NBT root tag is TAG_End";
    return false;
  }
  if (raw_tag > static_cast<uint8_t>(Tag::long_array)) {
    error = at_offset("unknown root tag id " + std::to_string(raw_tag), 0);
    return false;
  }
  std::string name;
  if (!reader.read_string(name)) {
    error = at_offset("truncated NBT root name", reader.position());
    return false;
  }
  Value root;
  if (!parse_value(reader, static_cast<Tag>(raw_tag), 0, root, error)) return false;
  if (!reader.exhausted()) {
    error = at_offset("trailing bytes after NBT root", reader.position());
    return false;
  }
  out.root_name = std::move(name);
  out.root = std::move(root);
  error.clear();
  return true;
}

bool parse(const std::vector<uint8_t>& data, Document& out, std::string& error) {
  return parse(data.data(), data.size(), out, error);
}

bool parse_file(const std::filesystem::path& path, Document& out, std::string& error) {
  std::vector<uint8_t> raw;
  if (!file::read_all(path, raw, error)) return false;
  std::vector<uint8_t> decoded;
  if (!compression::decompress_auto(raw.data(), raw.size(), decoded, error)) {
    error = "cannot decompress " + file::to_utf8(path) + ": " + error;
    return false;
  }
  if (!parse(decoded, out, error)) {
    error = "cannot parse " + file::to_utf8(path) + ": " + error;
    return false;
  }
  return true;
}

bool parse_payload(const std::vector<uint8_t>& data, Document& out, std::string& error) {
  return parse(data, out, error);
}

const Value* find(const Value& value, std::string_view key) {
  if (value.type != Tag::compound || !value.compound) return nullptr;
  const auto it = value.compound->find(key);
  return it == value.compound->end() ? nullptr : &it->second;
}

const Value* item(const Value& value, size_t index) {
  if (value.type != Tag::list || !value.list || index >= value.list->items.size()) {
    return nullptr;
  }
  return &value.list->items[index];
}

const std::string* as_string(const Value& value) {
  return value.type == Tag::string ? &value.text : nullptr;
}

std::optional<int64_t> as_int(const Value& value) {
  switch (value.type) {
    case Tag::byte:
    case Tag::short_tag:
    case Tag::int_tag:
    case Tag::long_tag:
      return value.integral;
    default:
      return std::nullopt;
  }
}

std::optional<double> as_number(const Value& value) {
  switch (value.type) {
    case Tag::float_tag:
    case Tag::double_tag:
      return value.real;
    case Tag::byte:
    case Tag::short_tag:
    case Tag::int_tag:
    case Tag::long_tag:
      return static_cast<double>(value.integral);
    default:
      return std::nullopt;
  }
}

bool is_air_name(std::string_view block_name) {
  if (block_name.find(':') == std::string_view::npos) {
    return block_name == "air" || block_name == "cave_air" || block_name == "void_air";
  }
  return block_name == "minecraft:air" || block_name == "minecraft:cave_air" ||
         block_name == "minecraft:void_air";
}

const char* tag_name(Tag tag) {
  switch (tag) {
    case Tag::end: return "end";
    case Tag::byte: return "byte";
    case Tag::short_tag: return "short";
    case Tag::int_tag: return "int";
    case Tag::long_tag: return "long";
    case Tag::float_tag: return "float";
    case Tag::double_tag: return "double";
    case Tag::byte_array: return "byte_array";
    case Tag::string: return "string";
    case Tag::list: return "list";
    case Tag::compound: return "compound";
    case Tag::int_array: return "int_array";
    case Tag::long_array: return "long_array";
  }
  return "unknown";
}

namespace {

struct Writer {
  std::vector<uint8_t>& out;
  std::string& error;

  void u8(uint8_t value) { out.push_back(value); }
  void be16(uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
  }
  void be32(uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
  }
  void be64(uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<uint8_t>(value >> shift));
    }
  }
  void text(const std::string& value) {
    be16(static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
  }
};

bool write_payload(Writer& writer, Tag tag, const Value& value,
                   std::string& error) {
  if (tag != value.type) {
    error = std::string("value type ") + tag_name(value.type) +
            " does not match declared tag " + tag_name(tag);
    return false;
  }
  switch (tag) {
    case Tag::end:
      error = "TAG_End cannot be written as a payload";
      return false;
    case Tag::byte:
      writer.u8(static_cast<uint8_t>(value.integral));
      return true;
    case Tag::short_tag:
      writer.be16(static_cast<uint16_t>(value.integral));
      return true;
    case Tag::int_tag:
      writer.be32(static_cast<uint32_t>(value.integral));
      return true;
    case Tag::long_tag:
      writer.be64(static_cast<uint64_t>(value.integral));
      return true;
    case Tag::float_tag: {
      const float number = static_cast<float>(value.real);
      uint32_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      writer.be32(bits);
      return true;
    }
    case Tag::double_tag: {
      const double number = value.real;
      uint64_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      writer.be64(bits);
      return true;
    }
    case Tag::byte_array:
      writer.be32(static_cast<uint32_t>(value.bytes.size()));
      writer.out.insert(writer.out.end(), value.bytes.begin(), value.bytes.end());
      return true;
    case Tag::string:
      writer.text(value.text);
      return true;
    case Tag::list: {
      if (!value.list) {
        error = "list value has no payload";
        return false;
      }
      writer.u8(static_cast<uint8_t>(value.list->element_type));
      writer.be32(static_cast<uint32_t>(value.list->items.size()));
      for (const Value& item : value.list->items) {
        if (!write_payload(writer, value.list->element_type, item, error)) {
          return false;
        }
      }
      return true;
    }
    case Tag::compound: {
      if (!value.compound) {
        error = "compound value has no payload";
        return false;
      }
      for (const auto& [name, child] : *value.compound) {
        if (child.type == Tag::end) {
          error = "compound member '" + name + "' has no type";
          return false;
        }
        writer.u8(static_cast<uint8_t>(child.type));
        writer.text(name);
        if (!write_payload(writer, child.type, child, error)) return false;
      }
      writer.u8(static_cast<uint8_t>(Tag::end));
      return true;
    }
    case Tag::int_array:
      writer.be32(static_cast<uint32_t>(value.ints.size()));
      for (int32_t number : value.ints) {
        writer.be32(static_cast<uint32_t>(number));
      }
      return true;
    case Tag::long_array:
      writer.be32(static_cast<uint32_t>(value.longs.size()));
      for (int64_t number : value.longs) {
        writer.be64(static_cast<uint64_t>(number));
      }
      return true;
  }
  error = "unknown tag type";
  return false;
}

} // namespace

bool write(const Document& document, std::vector<uint8_t>& out,
           std::string& error) {
  out.clear();
  if (document.root.type == Tag::end) {
    error = "root tag has no type";
    return false;
  }
  Writer writer{out, error};
  writer.u8(static_cast<uint8_t>(document.root.type));
  writer.text(document.root_name);
  if (!write_payload(writer, document.root.type, document.root, error)) {
    out.clear();
    return false;
  }
  return true;
}

} // namespace reim::nbt
