#include "asset/packs.h"

#include "util/file.h"
#include "util/zip.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::asset {
namespace {

constexpr uint64_t kMaxEntryBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMaxTotalBytes = 512ull * 1024ull * 1024ull;
constexpr size_t kMaxEntries = 32768;

bool safe_path(const std::string& name, std::string& error) {
  if (name.empty() || name.front() == '/' || name.front() == '\\' ||
      (name.size() >= 2 && name[1] == ':')) {
    error = "untrusted pack path: " + name;
    return false;
  }
  size_t start = 0;
  while (start <= name.size()) {
    const size_t slash = name.find_first_of("/\\", start);
    const std::string part = name.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (part == "..") {
      error = "path traversal in pack entry: " + name;
      return false;
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

struct PackSource {
  bool is_zip = false;
  zip::Archive archive;
  std::filesystem::path directory;
  std::vector<std::string> entries;
  uint64_t total_bytes = 0;
};

bool open_pack(const std::string& path, PackSource& source, std::string& error) {
  const auto fs_path = file::from_utf8(path);
  if (!file::exists(fs_path)) {
    error = "pack not found: " + path;
    return false;
  }
  if (file::is_directory(fs_path)) {
    source.is_zip = false;
    source.directory = fs_path;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(fs_path, ec), end;
         it != end; it.increment(ec)) {
      if (ec) {
        error = "pack directory iteration failed: " + ec.message();
        return false;
      }
      if (!it->is_regular_file()) continue;
      std::string name = file::to_utf8(
          std::filesystem::relative(it->path(), fs_path, ec));
      std::replace(name.begin(), name.end(), '\\', '/');
      if (!safe_path(name, error)) return false;
      source.entries.push_back(std::move(name));
      if (source.entries.size() > kMaxEntries) {
        error = "pack has too many entries";
        return false;
      }
    }
    std::sort(source.entries.begin(), source.entries.end());
    return true;
  }
  source.is_zip = true;
  if (!source.archive.open(path, error)) return false;
  source.entries = source.archive.list();
  if (source.entries.size() > kMaxEntries) {
    error = "pack has too many entries";
    return false;
  }
  std::sort(source.entries.begin(), source.entries.end());
  for (const std::string& entry : source.entries) {
    if (!safe_path(entry, error)) return false;
  }
  return true;
}

bool read_entry(PackSource& source, const std::string& name,
                std::vector<uint8_t>& out, std::string& error) {
  if (source.is_zip) {
    if (!source.archive.read(name, out, error)) return false;
  } else {
    if (!file::read_all(source.directory / file::from_utf8(name), out, error)) {
      return false;
    }
  }
  if (out.size() > kMaxEntryBytes) {
    error = "pack entry exceeds the per-file limit: " + name;
    return false;
  }
  source.total_bytes += out.size();
  if (source.total_bytes > kMaxTotalBytes) {
    error = "pack exceeds the total uncompressed limit";
    return false;
  }
  return true;
}

Json parse_object(const std::vector<uint8_t>& bytes, const std::string& name,
                  std::string& error) {
  Json value = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
  if (value.is_discarded() || !value.is_object()) {
    error = "pack JSON is not an object: " + name;
    return Json::object();
  }
  return value;
}

bool read_pack_format(PackSource& source, Json& report, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!read_entry(source, "pack.mcmeta", bytes, error)) {
    error = "pack.mcmeta missing or unreadable";
    return false;
  }
  Json meta = parse_object(bytes, "pack.mcmeta", error);
  if (meta.is_discarded() || meta.empty()) return false;
  const auto pack = meta.find("pack");
  if (pack == meta.end() || !pack->is_object()) return false;
  std::vector<int> formats;
  if (const auto format = pack->find("pack_format");
      format != pack->end() && format->is_number_integer()) {
    formats.push_back(format->get<int>());
  }
  if (const auto supported = pack->find("supported_formats");
      supported != pack->end()) {
    if (supported->is_number_integer()) {
      formats.push_back(supported->get<int>());
    } else if (supported->is_array()) {
      for (const Json& value : *supported) {
        if (value.is_number_integer()) formats.push_back(value.get<int>());
      }
    } else if (supported->is_object()) {
      const int min = supported->value("min_inclusive", 0);
      const int max = supported->value("max_inclusive", 0);
      for (int i = min; i <= max && i > 0; ++i) formats.push_back(i);
    }
  }
  report["format_candidates"] = formats;
  bool accepted = false;
  for (int format : formats) {
    if (format >= 90) accepted = true;
  }
  if (!accepted) {
    error = "pack format is not 26.3-compatible (expected >= 90 / 97.1)";
    return false;
  }
  return true;
}

void merge_object(Json& base, const Json& overlay) {
  if (!base.is_object()) base = Json::object();
  for (auto it = overlay.begin(); it != overlay.end(); ++it) {
    base[it.key()] = it.value();
  }
}

} // namespace

bool parse_pack_list(const char* json_array, std::vector<std::string>& out,
                     std::string& error) {
  out.clear();
  if (json_array == nullptr || *json_array == '\0') return true;
  Json value = Json::parse(json_array, json_array + std::strlen(json_array), nullptr,
                           false);
  if (value.is_discarded() || !value.is_array()) {
    error = "resource/data pack list is not a JSON array of paths";
    return false;
  }
  for (const Json& item : value) {
    if (!item.is_string()) {
      error = "pack list contains a non-string entry";
      return false;
    }
    out.push_back(item.get<std::string>());
  }
  return true;
}

bool apply_resource_packs(const std::vector<std::string>& paths, const Json& base_lang,
                          const Json& base_sounds, PackView& view,
                          std::string& error) {
  view = PackView{};
  view.lang_values = base_lang.is_object() ? base_lang : Json::object();
  view.sounds = base_sounds.is_object() ? base_sounds : Json::object();
  view.report["packs"] = Json::array();
  view.report["overrides"] = Json::object();
  view.report["warnings"] = Json::array();
  uint64_t total_lang = 0;
  uint64_t total_sound_definitions = 0;
  uint64_t total_m3 = 0;
  uint64_t total_textures = 0;
  uint64_t total_models_unsupported = 0;

  for (size_t index = 0; index < paths.size(); ++index) {
    PackSource source;
    std::string open_error;
    if (!open_pack(paths[index], source, open_error)) {
      error = "resource pack " + std::to_string(index) + ": " + open_error;
      return false;
    }
    Json pack_report;
    pack_report["index"] = index;
    pack_report["name"] = std::filesystem::path(paths[index]).filename().string();
    if (!read_pack_format(source, pack_report, open_error)) {
      error = "resource pack " + pack_report["name"].get<std::string>() + ": " +
              open_error;
      return false;
    }
    uint64_t lang_overrides = 0;
    uint64_t sound_definitions = 0;
    uint64_t m3_overrides = 0;
    uint64_t texture_overrides = 0;
    uint64_t model_overrides = 0;
    for (const std::string& entry : source.entries) {
      if (entry.rfind("assets/", 0) != 0) continue;
      std::string key = entry.substr(std::string("assets/").size());
      std::vector<uint8_t> bytes;
      if (key == "minecraft/lang/en_us.json") {
        if (!read_entry(source, entry, bytes, open_error)) {
          view.warnings.push_back(open_error);
          continue;
        }
        Json values = parse_object(bytes, entry, open_error);
        if (!open_error.empty()) {
          error = open_error;
          return false;
        }
        merge_object(view.lang_values, values);
        ++lang_overrides;
        continue;
      }
      if (key == "minecraft/sounds.json") {
        if (!read_entry(source, entry, bytes, open_error)) {
          view.warnings.push_back(open_error);
          continue;
        }
        Json pack_sounds = parse_object(bytes, entry, open_error);
        if (!open_error.empty()) {
          error = open_error;
          return false;
        }
        Json definitions = Json::object();
        if (const auto found = pack_sounds.find("definitions");
            found != pack_sounds.end() && found->is_object()) {
          definitions = *found;
        } else {
          for (auto it = pack_sounds.begin(); it != pack_sounds.end(); ++it) {
            if (it.key() == "m3" || it.key() == "registry" ||
                it.key() == "definitions" || !it.value().is_object()) {
              continue;
            }
            definitions[it.key()] = it.value();
          }
        }
        if (const auto base_defs = view.sounds.find("definitions");
            base_defs == view.sounds.end() || !base_defs->is_object()) {
          view.sounds["definitions"] = Json::object();
        }
        merge_object(view.sounds["definitions"], definitions);
        sound_definitions += definitions.size();
        if (const auto m3 = pack_sounds.find("m3");
            m3 != pack_sounds.end() && m3->is_array()) {
          Json merged = Json::array();
          std::map<uint32_t, Json> by_id;
          if (const auto base_m3 = view.sounds.find("m3");
              base_m3 != view.sounds.end() && base_m3->is_array()) {
            for (const Json& item : *base_m3) {
              if (item.is_object()) by_id[item.value("id", 0u)] = item;
            }
          }
          for (const Json& item : *m3) {
            if (item.is_object()) by_id[item.value("id", 0u)] = item;
            ++m3_overrides;
          }
          for (const auto& [id, item] : by_id) {
            (void)id;
            merged.push_back(item);
          }
          view.sounds["m3"] = std::move(merged);
        }
        continue;
      }
      if (key.rfind("minecraft/textures/", 0) == 0 && entry.ends_with(".png")) {
        if (!read_entry(source, entry, bytes, open_error)) {
          view.warnings.push_back(open_error);
          continue;
        }
        view.overrides[key] = std::move(bytes);
        ++texture_overrides;
        continue;
      }
      if (key.rfind("minecraft/models/", 0) == 0 ||
          key.rfind("minecraft/blockstates/", 0) == 0) {
        ++model_overrides;
        continue;
      }
      if (key.rfind("minecraft/font/", 0) == 0 ||
          key.rfind("minecraft/lang/", 0) == 0) {
        if (!read_entry(source, entry, bytes, open_error)) {
          view.warnings.push_back(open_error);
          continue;
        }
        view.overrides[key] = std::move(bytes);
      }
    }
    if (model_overrides != 0) {
      view.warnings.push_back(
          "resource pack models/blockstates are not re-meshed at runtime; "
          "Converter extract must rebuild the atlas/geometry for them");
    }
    pack_report["lang_overrides"] = lang_overrides;
    pack_report["sound_definitions"] = sound_definitions;
    pack_report["m3_overrides"] = m3_overrides;
    pack_report["texture_overrides"] = texture_overrides;
    pack_report["model_overrides_unsupported"] = model_overrides;
    total_lang += lang_overrides;
    total_sound_definitions += sound_definitions;
    total_m3 += m3_overrides;
    total_textures += texture_overrides;
    total_models_unsupported += model_overrides;
    view.report["packs"].push_back(std::move(pack_report));
  }
  view.report["pack_count"] = paths.size();
  view.report["lang_entries"] = view.lang_values.size();
  view.report["lang_overrides"] = total_lang;
  view.report["sound_definitions"] = total_sound_definitions;
  view.report["m3_overrides"] = total_m3;
  view.report["texture_overrides"] = total_textures;
  view.report["unsupported_model_overrides"] = total_models_unsupported;
  view.report["warnings"] = Json::array();
  for (const std::string& warning : view.warnings) {
    view.report["warnings"].push_back(warning);
  }
  view.report["warning_count"] = view.warnings.size();
  return true;
}

bool validate_data_packs(const std::vector<std::string>& paths, Json& report,
                         std::string& error) {
  report = Json::object();
  report["packs"] = Json::array();
  report["warnings"] = Json::array();
  uint64_t total_gameplay = 0;
  for (size_t index = 0; index < paths.size(); ++index) {
    PackSource source;
    std::string open_error;
    if (!open_pack(paths[index], source, open_error)) {
      error = "data pack " + std::to_string(index) + ": " + open_error;
      return false;
    }
    Json pack_report;
    pack_report["index"] = index;
    pack_report["name"] = std::filesystem::path(paths[index]).filename().string();
    if (!read_pack_format(source, pack_report, open_error)) {
      error = "data pack " + pack_report["name"].get<std::string>() + ": " +
              open_error;
      return false;
    }
    Json categories = Json::object();
    uint64_t files = 0;
    uint64_t overrides = 0;
    for (const std::string& entry : source.entries) {
      if (entry.rfind("data/", 0) != 0) continue;
      std::vector<uint8_t> bytes;
      if (!read_entry(source, entry, bytes, open_error)) {
        report["warnings"].push_back(open_error);
        continue;
      }
      ++files;
      const std::string rest = entry.substr(std::string("data/").size());
      const size_t first = rest.find('/');
      const size_t second = first == std::string::npos
                                ? std::string::npos
                                : rest.find('/', first + 1);
      if (second == std::string::npos) continue;
      const std::string category = rest.substr(first + 1, second - first - 1);
      categories[category] = categories.value(category, 0ull) + 1;
      if (category == "recipe" || category == "loot_table" ||
          category == "tags" || category == "worldgen") {
        ++overrides;
      }
    }
    pack_report["files"] = files;
    pack_report["gameplay_overrides"] = overrides;
    pack_report["categories"] = std::move(categories);
    total_gameplay += overrides;
    report["packs"].push_back(std::move(pack_report));
  }
  report["pack_count"] = paths.size();
  report["gameplay_overrides"] = total_gameplay;
  report["note"] =
      "Data packs are validated and counted at startup. Runtime recipes/loot "
      "remain the native subset; Converter overlay applies pack data to "
      "generated JSON for diff/patch.";
  return true;
}

} // namespace reim::asset
