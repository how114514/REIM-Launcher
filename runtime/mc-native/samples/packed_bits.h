#pragma once

#include <cstdint>
#include <vector>

namespace reim::samples {

/*
 * Hand-written native counterpart of the Minecraft palette bit storage
 * (net.minecraft.util.BitStorage / PackedIntegerArray semantics).
 *
 * Entries are packed least-significant-bit first. A value never crosses a
 * 64-bit word boundary, so each word holds floor(64 / bits) entries and the
 * remaining high bits are unused. `bits` is at least 4 in the world format,
 * but the helper works for any value in [1, 32].
 *
 * This is the S1 representative translation experiment (see
 * converter/samples/DIFFERENCES.md); the world reader in S2 uses the same
 * function.
 */
unsigned bits_for_palette(size_t palette_size);

bool unpack_packed(const int64_t* data, size_t word_count, unsigned bits,
                   size_t entry_count, std::vector<uint32_t>& out);

/* Inverse used by unit tests only. */
std::vector<int64_t> pack_packed(const uint32_t* values, size_t entry_count,
                                 unsigned bits);

} // namespace reim::samples
