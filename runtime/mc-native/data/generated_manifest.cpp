#include "data/generated_manifest.h"

#include "util/file.h"
#include "util/hash.h"

#include <string_view>

#include "json.hpp"

namespace reim::data {
namespace {

using Json = nlohmann::json;

Json parse_noexcept(std::string_view text) {
  return Json::parse(text.begin(), text.end(), nullptr, false);
}

const Json* find_member(const Json& object, const char* name) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(name);
  return it == object.end() ? nullptr : &(*it);
}

std::string get_string(const Json& object, const char* name,
                       std::string_view fallback = {}) {
  const Json* value = find_member(object, name);
  if (!value || !value->is_string()) return std::string(fallback);
  return value->get<std::string>();
}

uint64_t get_u64(const Json& object, const char* name, uint64_t fallback = 0) {
  const Json* value = find_member(object, name);
  if (!value || !value->is_number_unsigned()) {
    if (value && value->is_number_integer()) {
      const int64_t v = value->get<int64_t>();
      return v < 0 ? fallback : static_cast<uint64_t>(v);
    }
    return fallback;
  }
  return value->get<uint64_t>();
}

uint32_t get_u32(const Json& object, const char* name, uint32_t fallback = 0) {
  return static_cast<uint32_t>(get_u64(object, name, fallback));
}

} // namespace

bool load_manifest(const std::filesystem::path& generated_dir, GeneratedManifest& out,
                   std::string& error) {
  const auto manifest_path = generated_dir / "manifest.json";
  std::string text;
  if (!file::read_text(manifest_path, text, error)) return false;
  const Json root = parse_noexcept(text);
  if (root.is_discarded() || !root.is_object()) {
    error = "manifest.json is not valid JSON";
    return false;
  }
  out = GeneratedManifest{};
  out.format = static_cast<int>(get_u32(root, "format"));
  out.schema = get_string(root, "schema");
  out.profile = get_string(root, "profile");
  out.abi_version = get_u32(root, "abi_version");
  out.tool = get_string(root, "tool");
  out.tool_version = get_string(root, "tool_version");

  const Json* capabilities = find_member(root, "capabilities");
  if (capabilities && capabilities->is_array()) {
    for (const Json& item : *capabilities) {
      if (item.is_string()) out.capabilities.push_back(item.get<std::string>());
    }
  }

  const Json* atlas = find_member(root, "atlas");
  if (atlas && atlas->is_object()) {
    out.atlas_sha256 = get_string(*atlas, "sha256");
    out.atlas_width = get_u32(*atlas, "width");
    out.atlas_height = get_u32(*atlas, "height");
    out.atlas_tile_size = get_u32(*atlas, "tile_size");
    out.atlas_meta_version = get_u32(*atlas, "meta_version");
  }

  const Json* counts = find_member(root, "counts");
  if (counts && counts->is_object()) {
    out.blocks_total = get_u64(*counts, "blocks_total");
    out.blocks_renderable = get_u64(*counts, "blocks_renderable");
    out.sprites = get_u64(*counts, "sprites");
  }

  const Json* generated = find_member(root, "generated");
  if (!generated || !generated->is_array()) {
    error = "manifest.json has no generated[] list";
    return false;
  }
  out.files.clear();
  out.files.reserve(generated->size());
  for (const Json& item : *generated) {
    if (!item.is_object()) {
      error = "manifest.json generated[] contains a non-object";
      return false;
    }
    GeneratedFile entry;
    entry.path = get_string(item, "path");
    entry.sha256 = get_string(item, "sha256");
    entry.size = get_u64(item, "size");
    if (entry.path.empty() || entry.sha256.empty()) {
      error = "manifest.json generated[] entry lacks path/sha256";
      return false;
    }
    out.files.push_back(std::move(entry));
  }
  return true;
}

bool verify_manifest(const std::filesystem::path& generated_dir,
                     const GeneratedManifest& manifest, std::string& error) {
  for (const GeneratedFile& entry : manifest.files) {
    const auto path = generated_dir / file::from_utf8(entry.path);
    file::Bytes bytes;
    if (!file::read_all(path, bytes, error)) return false;
    if (bytes.size() != entry.size) {
      error = "size mismatch for " + entry.path + ": manifest " +
              std::to_string(entry.size) + ", actual " +
              std::to_string(bytes.size());
      return false;
    }
    const std::string actual = hash::hex(hash::sha256(bytes.data(), bytes.size()));
    if (actual != entry.sha256) {
      error = "sha256 mismatch for " + entry.path;
      return false;
    }
  }
  return true;
}

bool validate_generated(const std::filesystem::path& generated_dir,
                        const std::string& expected_profile,
                        uint32_t expected_abi_version, GeneratedManifest& out,
                        std::string& error) {
  if (!file::is_directory(generated_dir)) {
    error = "generated directory not found: " + file::to_utf8(generated_dir);
    return false;
  }
  if (!load_manifest(generated_dir, out, error)) return false;
  if (out.format != 3) {
    error = "generated manifest schema format " + std::to_string(out.format) +
            " is not supported (ABI 3 requires format 3; run `reim-convert data` "
            "then `reim-convert extract`)";
    return false;
  }
  if (out.atlas_sha256.empty() || out.atlas_meta_version != 2) {
    error = "generated manifest is missing the atlas descriptor; re-run "
            "`reim-convert extract`";
    return false;
  }
  if (out.profile != expected_profile) {
    error = "profile mismatch: generated '" + out.profile + "', runtime expects '" +
            expected_profile + "'";
    return false;
  }
  if (out.abi_version != expected_abi_version) {
    error = "ABI mismatch: generated " + std::to_string(out.abi_version) +
            ", runtime " + std::to_string(expected_abi_version);
    return false;
  }
  for (const char* required : {"blocks.json", "textures.json", "atlas.png",
                               "items.json", "entities.json", "models.json",
                               "recipes.json", "loot_tables.json", "tags.json",
                               "worldgen.json", "sounds.json", "lang.json",
                               "coverage_report.json"}) {
    bool found = false;
    for (const GeneratedFile& entry : out.files) {
      if (entry.path == required) {
        found = true;
        break;
      }
    }
    if (!found) {
      error = std::string("manifest is missing required generated file: ") + required;
      return false;
    }
  }
  return verify_manifest(generated_dir, out, error);
}

} // namespace reim::data
