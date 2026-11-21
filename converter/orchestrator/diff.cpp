#include "diff.h"

#include "util/file.h"
#include "util/hash.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

struct MapChange {
  std::string key;
  std::string old_hash;
  std::string new_hash;
};

struct MapDiff {
  std::vector<std::string> added;
  std::vector<std::string> removed;
  std::vector<MapChange> changed;
};

bool read_json(const std::filesystem::path& path, Json& out, std::string& error) {
  return read_json_file(path, out, error);
}

std::string value_hash(const Json& value) {
  return hash::hex(hash::sha256(value.dump()));
}

void flatten_object(const Json& value, const std::string& prefix, int depth,
                    std::map<std::string, std::string>& out) {
  if (depth > 0 && value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      const std::string key = prefix.empty() ? it.key() : prefix + "/" + it.key();
      flatten_object(it.value(), key, depth - 1, out);
    }
    return;
  }
  out[prefix] = value_hash(value);
}

bool flatten_file(const std::filesystem::path& path, const std::string& key,
                  int depth, std::map<std::string, std::string>& out,
                  std::string& error) {
  Json root;
  if (!read_json(path, root, error)) return false;
  const auto found = root.find(key);
  if (found == root.end()) {
    out.clear();
    return true;
  }
  out.clear();
  flatten_object(*found, "", depth, out);
  return true;
}

MapDiff compare_maps(const std::map<std::string, std::string>& old_map,
                     const std::map<std::string, std::string>& new_map) {
  MapDiff diff;
  for (const auto& [key, value] : new_map) {
    const auto old = old_map.find(key);
    if (old == old_map.end()) {
      diff.added.push_back(key);
    } else if (old->second != value) {
      diff.changed.push_back(MapChange{key, old->second, value});
    }
  }
  for (const auto& [key, value] : old_map) {
    (void)value;
    if (new_map.find(key) == new_map.end()) diff.removed.push_back(key);
  }
  return diff;
}

void append_strings(Json& array, const std::vector<std::string>& values,
                    size_t limit) {
  size_t count = 0;
  for (const std::string& value : values) {
    if (count++ >= limit) break;
    array.push_back(value);
  }
}

Json subsystem_report(const char* name, const MapDiff& diff, const char* owner,
                      const char* severity, const char* action, size_t limit = 256) {
  Json changes = Json::array();
  for (const MapChange& change : diff.changed) {
    changes.push_back(Json{{"key", change.key},
                           {"old", change.old_hash},
                           {"new", change.new_hash}});
    if (changes.size() >= limit) break;
  }
  Json added = Json::array();
  Json removed = Json::array();
  append_strings(added, diff.added, limit);
  append_strings(removed, diff.removed, limit);
  return Json{{"area", name},
              {"added", diff.added.size()},
              {"removed", diff.removed.size()},
              {"changed", diff.changed.size()},
              {"added_ids", added},
              {"removed_ids", removed},
              {"changed_ids", changes},
              {"owner", owner},
              {"severity", severity},
              {"action", action},
              {"truncated", diff.added.size() > limit ||
                                diff.removed.size() > limit ||
                                diff.changed.size() > limit}};
}

bool compare_subsystem(const std::filesystem::path& old_dir,
                       const std::filesystem::path& new_dir, const char* file,
                       const char* key, int depth, Json& out, const char* area,
                       const char* owner, const char* severity, const char* action,
                       std::string& error) {
  std::map<std::string, std::string> old_map;
  std::map<std::string, std::string> new_map;
  if (!flatten_file(old_dir / file, key, depth, old_map, error)) return false;
  if (!flatten_file(new_dir / file, key, depth, new_map, error)) return false;
  out = subsystem_report(area, compare_maps(old_map, new_map), owner, severity, action);
  return true;
}

Json assets_map(const std::filesystem::path& dir) {
  Json root;
  std::string error;
  Json out = Json::object();
  if (!read_json(dir / "manifest.json", root, error)) return out;
  const auto generated = root.find("generated");
  if (generated == root.end() || !generated->is_array()) return out;
  for (const Json& entry : *generated) {
    out[entry.value("path", "")] = entry.value("sha256", "");
  }
  return out;
}

Json compare_analysis(const std::filesystem::path& old_dir,
                      const std::filesystem::path& new_dir, std::string& error) {
  Json old_classes;
  Json new_classes;
  if (!read_json(old_dir / "classes.json", old_classes, error) ||
      !read_json(new_dir / "classes.json", new_classes, error)) {
    error.clear();
    return Json{{"available", false}};
  }
  std::map<std::string, std::string> old_map;
  std::map<std::string, std::string> new_map;
  for (const Json& item : old_classes["classes"]) {
    old_map[item.value("name", "")] = item.value("bytecode_sha256", "");
  }
  for (const Json& item : new_classes["classes"]) {
    new_map[item.value("name", "")] = item.value("bytecode_sha256", "");
  }
  const MapDiff diff = compare_maps(old_map, new_map);
  return Json{{"available", true},
              {"class_count_old", old_map.size()},
              {"class_count_new", new_map.size()},
              {"added", diff.added.size()},
              {"removed", diff.removed.size()},
              {"changed", diff.changed.size()},
              {"added_classes", diff.added},
              {"removed_classes", diff.removed}};
}

} // namespace

int run_diff(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const auto old_arg = args.get("old");
  const auto new_arg = args.get("new");
  const auto out_arg = args.get("out");
  if (!old_arg || !new_arg || !out_arg) {
    log_error("diff requires --old <generated-old> --new <generated-new> --out <dir>");
    return 2;
  }
  const auto old_dir = file::from_utf8(*old_arg);
  const auto new_dir = file::from_utf8(*new_arg);
  const auto out_dir = file::from_utf8(*out_arg);
  std::string error;
  if (!file::ensure_directory(out_dir, error)) {
    log_error(error);
    return 3;
  }

  Json manifest_old;
  Json manifest_new;
  if (!read_json(old_dir / "manifest.json", manifest_old, error) ||
      !read_json(new_dir / "manifest.json", manifest_new, error)) {
    log_error("diff: " + error);
    return 4;
  }

  Json subsystems = Json::array();
  struct Spec {
    const char* file;
    const char* key;
    int depth;
    const char* area;
    const char* owner;
    const char* severity;
    const char* action;
  };
  const Spec specs[] = {
      {"blocks.json", "blocks", 1, "blocks", "C1/C2 native table", "high",
       "review block behavior/state table and regenerate native meshes"},
      {"items.json", "items", 1, "items", "D3 native table", "medium",
       "review item attributes; data_only rows do not add gameplay"},
      {"entities.json", "entities", 1, "entities", "F4/F5 native table", "high",
       "review entity models/AI/spawn rules (Java code source)"},
      {"models.json", "blocks", 1, "models", "C7 model table", "medium",
       "re-extract models and check multipart/weight coverage"},
      {"recipes.json", "recipes", 1, "recipes", "E3 native table + K4 data",
       "medium", "update generated recipes and native recipe subset"},
      {"loot_tables.json", "tables", 1, "loot_tables", "E6 native subset + K4 data",
       "medium", "update loot data; confirm unsupported functions"},
      {"tags.json", "types", 2, "tags", "K4 data", "low",
       "regenerate tags; no gameplay consumer yet"},
      {"worldgen.json", "types", 2, "worldgen", "G2/G3 native profile + K5 data",
       "high", "diff worldgen definitions; native profile needs a follow-up"},
      {"sounds.json", "registry", 1, "sounds", "I1/I3 native audio", "low",
       "update sound index and OGG assets"},
  };
  for (const Spec& spec : specs) {
    Json report;
    if (!compare_subsystem(old_dir, new_dir, spec.file, spec.key, spec.depth, report,
                           spec.area, spec.owner, spec.severity, spec.action, error)) {
      log_error("diff: " + error);
      return 5;
    }
    subsystems.push_back(std::move(report));
  }

  // Language values live at lang/en_us.json, not lang.json.
  {
    std::map<std::string, std::string> old_map;
    std::map<std::string, std::string> new_map;
    auto load = [&](const std::filesystem::path& dir,
                    std::map<std::string, std::string>& out) {
      Json values;
      std::string ignored;
      if (!read_json(dir / "lang" / "en_us.json", values, ignored)) return;
      for (auto it = values.begin(); it != values.end(); ++it) {
        out[it.key()] = value_hash(it.value());
      }
    };
    load(old_dir, old_map);
    load(new_dir, new_map);
    subsystems.push_back(subsystem_report("lang_values", compare_maps(old_map, new_map),
                                          "H4 runtime text", "low",
                                          "update language file"));
  }

  const Json old_assets = assets_map(old_dir);
  const Json new_assets = assets_map(new_dir);
  std::map<std::string, std::string> old_files;
  std::map<std::string, std::string> new_files;
  for (auto it = old_assets.begin(); it != old_assets.end(); ++it) {
    old_files[it.key()] = it.value().get<std::string>();
  }
  for (auto it = new_assets.begin(); it != new_assets.end(); ++it) {
    new_files[it.key()] = it.value().get<std::string>();
  }
  subsystems.push_back(subsystem_report("assets", compare_maps(old_files, new_files),
                                        "K6/I4 assets", "medium",
                                        "rebuild package; verify asset hashes"));

  Json analysis = Json{{"available", false}};
  if (const auto old_analysis = args.get("old-analysis");
      old_analysis && args.get("new-analysis")) {
    analysis = compare_analysis(file::from_utf8(*old_analysis),
                                file::from_utf8(*args.get("new-analysis")), error);
  }

  Json work_items = Json::array();
  for (const Json& subsystem : subsystems) {
    const uint64_t total = subsystem.value("added", 0ull) +
                           subsystem.value("removed", 0ull) +
                           subsystem.value("changed", 0ull);
    if (total == 0) continue;
    work_items.push_back(Json{
        {"id", std::string("diff-") + subsystem.value("area", "")},
        {"area", subsystem.value("area", "")},
        {"owner", subsystem.value("owner", "")},
        {"severity", subsystem.value("severity", "")},
        {"count", total},
        {"action", subsystem.value("action", "")},
    });
  }

  uint64_t added_total = 0;
  uint64_t removed_total = 0;
  uint64_t changed_total = 0;
  for (const Json& subsystem : subsystems) {
    added_total += subsystem.value("added", 0ull);
    removed_total += subsystem.value("removed", 0ull);
    changed_total += subsystem.value("changed", 0ull);
  }
  Json summary;
  summary["added"] = added_total;
  summary["removed"] = removed_total;
  summary["changed"] = changed_total;

  Json report;
  report["schema"] = "reim.diff";
  report["format"] = 3;
  report["kind"] = "version_upgrade_candidate";
  report["old_manifest_sha256"] = [&]() {
    file::Bytes bytes;
    std::string ignored;
    if (!file::read_all(old_dir / "manifest.json", bytes, ignored)) return std::string();
    return hash::hex(hash::sha256(bytes.data(), bytes.size()));
  }();
  report["new_manifest_sha256"] = [&]() {
    file::Bytes bytes;
    std::string ignored;
    if (!file::read_all(new_dir / "manifest.json", bytes, ignored)) return std::string();
    return hash::hex(hash::sha256(bytes.data(), bytes.size()));
  }();
  report["old_profile"] = manifest_old.value("profile", "");
  report["new_profile"] = manifest_new.value("profile", "");
  report["summary"] = std::move(summary);
  report["subsystems"] = std::move(subsystems);
  report["analysis"] = std::move(analysis);
  report["work_items"] = std::move(work_items);
  report["deterministic"] = true;
  report["note"] =
      "Per-key hashes only; no timestamps or absolute paths. `analysis` is "
      "available when --old-analysis/--new-analysis are given.";
  if (!write_json_file(out_dir / "diff.json", report, error)) {
    log_error(error);
    return 6;
  }

  std::string markdown = "# REIM data diff\n\n";
  markdown += "- old profile: " + report["old_profile"].get<std::string>() + "\n";
  markdown += "- new profile: " + report["new_profile"].get<std::string>() + "\n";
  markdown += "- added/removed/changed: " + std::to_string(added_total) + "/" +
              std::to_string(removed_total) + "/" + std::to_string(changed_total) +
              "\n\n";
  markdown += "## Work items\n\n";
  for (const Json& item : report["work_items"]) {
    markdown += "- **" + item["area"].get<std::string>() + "** (" +
                item["severity"].get<std::string>() + ", " +
                std::to_string(item["count"].get<uint64_t>()) + "): " +
                item["action"].get<std::string>() + "\n";
  }
  if (!write_text_file(out_dir / "diff.md", markdown, error)) {
    log_error(error);
    return 6;
  }
  log_info("diff: wrote " + file::to_utf8(out_dir / "diff.json") + " (" +
           std::to_string(report["work_items"].size()) + " work items)");
  return 0;
}

} // namespace reim::conv
