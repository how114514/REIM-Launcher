#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace reim::asset {

using Json = nlohmann::json;

/* M3-I4/I5: startup-only resource/data pack loading. Packs are resolved by the
 * Host and passed as absolute paths in ReimInstanceConfig. No hot reload.
 *
 * Security: ZIP/directory entry names are untrusted. Absolute paths, drive
 * letters and ".." are rejected; per-entry and total size limits apply.
 */
struct PackView {
  Json lang_values = Json::object();
  Json sounds;
  /* asset path -> bytes ("minecraft/lang/en_us.json", "minecraft/sounds/..."). */
  std::map<std::string, std::vector<uint8_t>> overrides;
  Json report = Json::object();
  std::vector<std::string> warnings;
};

bool parse_pack_list(const char* json_array, std::vector<std::string>& out,
                     std::string& error);

bool apply_resource_packs(const std::vector<std::string>& paths, const Json& base_lang,
                          const Json& base_sounds, PackView& view,
                          std::string& error);

bool validate_data_packs(const std::vector<std::string>& paths, Json& report,
                         std::string& error);

} // namespace reim::asset
