#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reim::data {

struct GeneratedFile {
  std::string path;
  std::string sha256;
  uint64_t size = 0;
};

struct GeneratedManifest {
  int format = 0;
  std::string profile;
  uint32_t abi_version = 0;
  std::string tool;
  std::string tool_version;
  std::vector<GeneratedFile> files;
  uint64_t blocks_total = 0;
  uint64_t blocks_renderable = 0;
  uint64_t sprites = 0;
};

/* Loads <generated_dir>/manifest.json. */
bool load_manifest(const std::filesystem::path& generated_dir, GeneratedManifest& out,
                   std::string& error);

/* Recomputes SHA-256 for every listed file and checks size. */
bool verify_manifest(const std::filesystem::path& generated_dir,
                     const GeneratedManifest& manifest, std::string& error);

/* Converter <-> Runtime version contract (M1-B / B7):
 *   - manifest.json exists and parses
 *   - profile matches the requested Minecraft profile
 *   - abi_version matches the Runtime contract
 *   - blocks.json / textures.json / atlas.png are present
 *   - SHA-256 of every listed file matches the manifest
 * The Runtime calls this before accepting a generated directory. */
bool validate_generated(const std::filesystem::path& generated_dir,
                        const std::string& expected_profile,
                        uint32_t expected_abi_version, GeneratedManifest& out,
                        std::string& error);

} // namespace reim::data
