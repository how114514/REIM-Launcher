#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reim::compression {

/*
 * Read-only decompression helpers shared by level.dat and Anvil region files.
 *
 * All functions append/replace `out` and return false with a readable message
 * on malformed input. No exception escapes (the Runtime is built with
 * /EHs-c-).
 */

bool zlib_decompress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error, size_t max_output = 512u * 1024u * 1024u);

bool gzip_decompress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error, size_t max_output = 512u * 1024u * 1024u);

/* Detects gzip (1f 8b) or zlib (0x78 ..) by header and decompresses.
 * Anything else is treated as already-decompressed data and copied verbatim;
 * the caller decides whether that is plausible for its container. */
bool decompress_auto(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                     std::string& error);

} // namespace reim::compression
