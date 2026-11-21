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

/* M3-B write path. zlib is the preferred Anvil compression; gzip is used for
 * level.dat and the split data files. Output is canonical for a fixed input
 * and level (no timestamps). */
bool zlib_compress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                   std::string& error, int level = 6);
bool gzip_compress(const uint8_t* source, size_t size, std::vector<uint8_t>& out,
                   std::string& error, int level = 6);

/* 26.3 region LZ4 chunks: a sequence of 4-byte little-endian compressed block
 * sizes followed by LZ4 blocks (max 4 MiB uncompressed each). LZ4 is read-only;
 * the writer keeps zlib. */
bool lz4_decompress_region(const uint8_t* source, size_t size,
                           std::vector<uint8_t>& out, std::string& error);

} // namespace reim::compression
