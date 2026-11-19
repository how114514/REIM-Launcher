#include "util/compression.h"

#include <cstring>
#include <limits>

#include "miniz.h"

namespace reim::compression {
namespace {

bool raw_inflate(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                 std::string& error, size_t max_output, int flags) {
  out.clear();
  if (size == 0) {
    error = "empty compressed stream";
    return false;
  }
  size_t out_size = 0;
  void* buffer = tinfl_decompress_mem_to_heap(source, size, &out_size, flags);
  if (buffer == nullptr) {
    error = "deflate decompression failed";
    return false;
  }
  if (out_size > max_output) {
    mz_free(buffer);
    error = "decompressed data exceeds limit (" + std::to_string(out_size) + " bytes)";
    return false;
  }
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  out.assign(bytes, bytes + out_size);
  mz_free(buffer);
  return true;
}

uint32_t read_le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool zlib_decompress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error, size_t max_output) {
  if (size < 6) {
    error = "zlib stream too short";
    return false;
  }
  return raw_inflate(source, size, out, error, max_output, TINFL_FLAG_PARSE_ZLIB_HEADER);
}

bool gzip_decompress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error, size_t max_output) {
  out.clear();
  if (size < 18 || source[0] != 0x1f || source[1] != 0x8b || source[2] != 8) {
    error = "not a gzip stream";
    return false;
  }
  const uint8_t flags = source[3];
  size_t offset = 10;
  const auto need = [&](size_t n) { return offset + n <= size; };
  if ((flags & 0x04) != 0) { // FEXTRA
    if (!need(2)) {
      error = "truncated gzip extra field";
      return false;
    }
    const size_t extra = static_cast<size_t>(source[offset]) |
                         (static_cast<size_t>(source[offset + 1]) << 8);
    offset += 2;
    if (!need(extra)) {
      error = "truncated gzip extra field";
      return false;
    }
    offset += extra;
  }
  if ((flags & 0x08) != 0) { // FNAME
    while (offset < size && source[offset] != 0) ++offset;
    if (offset >= size) {
      error = "truncated gzip file name";
      return false;
    }
    ++offset;
  }
  if ((flags & 0x10) != 0) { // FCOMMENT
    while (offset < size && source[offset] != 0) ++offset;
    if (offset >= size) {
      error = "truncated gzip comment";
      return false;
    }
    ++offset;
  }
  if ((flags & 0x02) != 0) { // FHCRC
    if (!need(2)) {
      error = "truncated gzip header CRC";
      return false;
    }
    offset += 2;
  }
  if (offset + 8 > size) {
    error = "truncated gzip trailer";
    return false;
  }
  const size_t deflate_size = size - offset - 8;
  if (!raw_inflate(source + offset, deflate_size, out, error, max_output, 0)) return false;

  const uint8_t* trailer = source + size - 8;
  const uint32_t expected_crc = read_le32(trailer);
  const uint32_t expected_size = read_le32(trailer + 4);
  const uint32_t actual_size = static_cast<uint32_t>(out.size() & 0xFFFFFFFFu);
  if (actual_size != expected_size) {
    out.clear();
    error = "gzip size mismatch";
    return false;
  }
  const uint32_t actual_crc =
      static_cast<uint32_t>(mz_crc32(0, out.data(), out.size()));
  if (actual_crc != expected_crc) {
    out.clear();
    error = "gzip CRC mismatch";
    return false;
  }
  return true;
}

bool decompress_auto(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error) {
  if (size >= 2 && source[0] == 0x1f && source[1] == 0x8b) {
    return gzip_decompress(source, size, out, error);
  }
  if (size >= 2 && (source[0] & 0x0F) == 8 &&
      ((static_cast<unsigned>(source[0]) << 8) | source[1]) % 31 == 0) {
    return zlib_decompress(source, size, out, error);
  }
  out.assign(source, source + size);
  return true;
}

} // namespace reim::compression
