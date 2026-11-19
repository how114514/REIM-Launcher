#include "samples/packed_bits.h"

namespace reim::samples {

unsigned bits_for_palette(size_t palette_size) {
  unsigned bits = 1;
  while ((size_t{1} << bits) < palette_size) ++bits;
  return bits < 4 ? 4 : bits;
}

bool unpack_packed(const int64_t* data, size_t word_count, unsigned bits,
                   size_t entry_count, std::vector<uint32_t>& out) {
  if (!data || bits == 0 || bits > 32) return false;
  const size_t per_word = 64 / bits;
  if (per_word == 0) return false;
  const size_t required_words = (entry_count + per_word - 1) / per_word;
  if (word_count < required_words) return false;

  const uint64_t mask = bits == 32 ? 0xFFFFFFFFull : ((uint64_t{1} << bits) - 1);
  out.resize(entry_count);
  for (size_t i = 0; i < entry_count; ++i) {
    const size_t word = i / per_word;
    const unsigned offset = static_cast<unsigned>((i % per_word) * bits);
    const uint64_t value = static_cast<uint64_t>(data[word]) >> offset;
    out[i] = static_cast<uint32_t>(value & mask);
  }
  return true;
}

std::vector<int64_t> pack_packed(const uint32_t* values, size_t entry_count,
                                 unsigned bits) {
  std::vector<int64_t> out;
  if (!values || bits == 0 || bits > 32) return out;
  const size_t per_word = 64 / bits;
  out.assign((entry_count + per_word - 1) / per_word, 0);
  const uint64_t mask = bits == 32 ? 0xFFFFFFFFull : ((uint64_t{1} << bits) - 1);
  for (size_t i = 0; i < entry_count; ++i) {
    const size_t word = i / per_word;
    const unsigned offset = static_cast<unsigned>((i % per_word) * bits);
    out[word] |= static_cast<int64_t>((static_cast<uint64_t>(values[i]) & mask) << offset);
  }
  return out;
}

} // namespace reim::samples
