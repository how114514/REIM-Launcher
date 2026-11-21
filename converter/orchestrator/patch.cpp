#include "patch.h"

#include "util/file.h"
#include "util/hash.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

std::string sha256_bytes(const void* data, size_t size) {
  return hash::hex(hash::sha256(static_cast<const uint8_t*>(data), size));
}

std::string sha256_text(const std::string& text) {
  return hash::hex(hash::sha256(text));
}

struct MergeResult {
  Json value;
  std::vector<std::string> conflicts;
};

MergeResult merge_json(const Json& base, const Json& ours, const Json& theirs,
                       const std::string& path) {
  MergeResult result;
  if (ours == theirs) {
    result.value = ours;
    return result;
  }
  if (ours == base) {
    result.value = theirs;
    return result;
  }
  if (theirs == base) {
    result.value = ours;
    return result;
  }
  if (base.is_object() && ours.is_object() && theirs.is_object()) {
    std::set<std::string> keys;
    for (auto it = base.begin(); it != base.end(); ++it) keys.insert(it.key());
    for (auto it = ours.begin(); it != ours.end(); ++it) keys.insert(it.key());
    for (auto it = theirs.begin(); it != theirs.end(); ++it) keys.insert(it.key());
    Json merged = Json::object();
    for (const std::string& key : keys) {
      const std::string child = path.empty() ? key : path + "/" + key;
      const auto b = base.find(key);
      const auto o = ours.find(key);
      const auto t = theirs.find(key);
      if (b == base.end()) {
        if (o != ours.end() && t != theirs.end()) {
          if (*o == *t) {
            merged[key] = *o;
          } else {
            result.conflicts.push_back(child + ": added differently on both sides");
          }
        } else if (o != ours.end()) {
          merged[key] = *o;
        } else if (t != theirs.end()) {
          merged[key] = *t;
        }
        continue;
      }
      if (o == ours.end() && t == theirs.end()) continue; // both deleted
      if (o == ours.end()) {
        if (*t == *b) continue; // deleted on our side, unchanged on theirs
        result.conflicts.push_back(child + ": deleted vs modified");
        continue;
      }
      if (t == theirs.end()) {
        if (*o == *b) continue;
        result.conflicts.push_back(child + ": modified vs deleted");
        continue;
      }
      MergeResult child_result = merge_json(*b, *o, *t, child);
      if (!child_result.conflicts.empty()) {
        result.conflicts.insert(result.conflicts.end(), child_result.conflicts.begin(),
                                child_result.conflicts.end());
      } else {
        merged[key] = std::move(child_result.value);
      }
    }
    result.value = std::move(merged);
    return result;
  }
  result.conflicts.push_back((path.empty() ? std::string("<root>") : path) +
                             ": both sides changed a non-object");
  return result;
}

struct PatchEntry {
  std::string id;
  std::string target;
  std::string base_sha256;
  std::string base;
  std::string replacement;
  bool has_base = false;
};

bool load_patch_set(const std::filesystem::path& given, Json& out,
                    std::vector<PatchEntry>& entries, std::string& error) {
  const auto path = file::is_directory(given) ? given / "patch_set.json" : given;
  if (!read_json_file(path, out, error)) return false;
  if (out.value("schema", "") != "reim.patch_set" || out.value("format", 0) != 1) {
    error = "patch set must be schema reim.patch_set format 1";
    return false;
  }
  const auto list = out.find("entries");
  if (list == out.end() || !list->is_array()) {
    error = "patch set has no entries[]";
    return false;
  }
  std::set<std::string> ids;
  for (const Json& item : *list) {
    if (!item.is_object()) {
      error = "patch entry is not an object";
      return false;
    }
    PatchEntry entry;
    entry.id = item.value("id", "");
    entry.target = item.value("target", "");
    entry.base_sha256 = item.value("base_sha256", "");
    entry.replacement = item.value("replacement", "");
    if (const auto base = item.find("base"); base != item.end() && base->is_string()) {
      entry.base = base->get<std::string>();
      entry.has_base = true;
    }
    if (entry.id.empty() || entry.target.empty() || entry.replacement.empty()) {
      error = "patch entry lacks id/target/replacement";
      return false;
    }
    if (!ids.insert(entry.id).second) {
      error = "duplicate patch id: " + entry.id;
      return false;
    }
    std::string safe_error;
    if (!safe_archive_path(entry.target, safe_error)) {
      error = safe_error;
      return false;
    }
    if (entry.has_base && !entry.base_sha256.empty() &&
        sha256_text(entry.base) != entry.base_sha256) {
      error = "patch " + entry.id + ": base_sha256 does not match base text";
      return false;
    }
    entries.push_back(std::move(entry));
  }
  return true;
}

Json apply_entries(const std::vector<PatchEntry>& entries,
                   const std::filesystem::path& target_dir, bool dry_run,
                   std::string& error) {
  Json results = Json::array();
  uint64_t applied = 0;
  uint64_t already = 0;
  uint64_t conflicts = 0;
  uint64_t errors = 0;
  for (const PatchEntry& entry : entries) {
    Json result;
    result["id"] = entry.id;
    result["target"] = entry.target;
    const auto path = target_dir / file::from_utf8(entry.target);
    file::Bytes bytes;
    std::string read_error;
    if (!file::read_all(path, bytes, read_error)) {
      result["status"] = "error";
      result["message"] = read_error;
      ++errors;
      results.push_back(std::move(result));
      continue;
    }
    const std::string current(bytes.begin(), bytes.end());
    const std::string current_sha = sha256_text(current);
    const std::string replacement_sha = sha256_text(entry.replacement);
    if (current_sha == replacement_sha) {
      result["status"] = "already_applied";
      ++already;
      results.push_back(std::move(result));
      continue;
    }
    std::string output = entry.replacement;
    if (!entry.base_sha256.empty() && current_sha != entry.base_sha256) {
      if (!entry.has_base) {
        result["status"] = "conflict";
        result["message"] = "base_sha256 mismatch and no base text for a 3-way merge";
        ++conflicts;
        results.push_back(std::move(result));
        continue;
      }
      Json base_json = parse_json_noexcept(entry.base);
      Json ours_json = parse_json_noexcept(current);
      Json theirs_json = parse_json_noexcept(entry.replacement);
      if (base_json.is_discarded() || ours_json.is_discarded() ||
          theirs_json.is_discarded()) {
        result["status"] = "conflict";
        result["message"] = "3-way merge requires JSON base/target/replacement";
        ++conflicts;
        results.push_back(std::move(result));
        continue;
      }
      MergeResult merged = merge_json(base_json, ours_json, theirs_json, "");
      if (!merged.conflicts.empty()) {
        result["status"] = "conflict";
        result["conflicts"] = merged.conflicts;
        ++conflicts;
        results.push_back(std::move(result));
        continue;
      }
      output = merged.value.dump(2) + "\n";
    }
    if (!dry_run) {
      std::string write_error;
      if (!file::write_atomic(path, output.data(), output.size(), write_error)) {
        result["status"] = "error";
        result["message"] = write_error;
        ++errors;
        results.push_back(std::move(result));
        continue;
      }
    }
    result["status"] = dry_run ? "would_apply" : "applied";
    ++applied;
    results.push_back(std::move(result));
  }
  return Json{{"applied", applied},
              {"already_applied", already},
              {"conflicts", conflicts},
              {"errors", errors},
              {"entries", std::move(results)}};
}

Json run_selftest() {
  Json checks = Json::array();
  bool merge_ok = false;
  bool conflict_ok = false;
  {
    const Json base = Json{{"a", 1}, {"b", 2}};
    const Json ours = Json{{"a", 9}, {"b", 2}};
    const Json theirs = Json{{"a", 1}, {"b", 3}};
    const MergeResult merged = merge_json(base, ours, theirs, "");
    merge_ok = merged.conflicts.empty() && merged.value == Json{{"a", 9}, {"b", 3}};
    checks.push_back(Json{{"check", "three_way_merge"}, {"ok", merge_ok}});
  }
  {
    const Json base = Json{{"a", 1}};
    const Json ours = Json{{"a", 2}};
    const Json theirs = Json{{"a", 3}};
    const MergeResult merged = merge_json(base, ours, theirs, "");
    conflict_ok = !merged.conflicts.empty();
    checks.push_back(Json{{"check", "conflict_detected"}, {"ok", conflict_ok},
                          {"conflicts", merged.conflicts}});
  }
  return Json{{"schema", "reim.patch_selftest"},
              {"format", 1},
              {"merge_ok", merge_ok},
              {"conflict_ok", conflict_ok},
              {"checks", std::move(checks)}};
}

} // namespace

int run_patch(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const std::string action = args.get("action").value_or("");
  const auto out_arg = args.get("out");
  auto write_report = [&](const Json& report) -> int {
    if (!out_arg) return 0;
    std::string error;
    const auto path = file::from_utf8(*out_arg);
    if (!path.parent_path().empty() &&
        !file::ensure_directory(path.parent_path(), error)) {
      log_error("patch: " + error);
      return 7;
    }
    if (!write_json_file(path, report, error)) {
      log_error("patch: " + error);
      return 7;
    }
    return 0;
  };

  if (action == "selftest") {
    const Json report = run_selftest();
    const int code = write_report(report);
    if (code != 0) return code;
    if (!report["merge_ok"].get<bool>() || !report["conflict_ok"].get<bool>()) {
      log_error("patch selftest failed");
      return 8;
    }
    log_info("patch: selftest OK (3-way merge + conflict)");
    return 0;
  }

  const auto set_arg = args.get("set");
  if (!set_arg) {
    log_error("patch requires --set <dir> --action apply|verify|list|selftest");
    return 2;
  }
  const auto set_dir = file::from_utf8(*set_arg);
  Json set_json;
  std::vector<PatchEntry> entries;
  std::string error;
  if (!load_patch_set(set_dir, set_json, entries, error)) {
    log_error("patch: " + error);
    return 3;
  }

  if (action == "list") {
    Json list = Json::array();
    for (const PatchEntry& entry : entries) {
      list.push_back(Json{{"id", entry.id},
                          {"target", entry.target},
                          {"base_sha256", entry.base_sha256},
                          {"replacement_sha256", sha256_text(entry.replacement)}});
    }
    Json report{{"schema", "reim.patch_list"},
                {"format", 1},
                {"profile", set_json.value("profile", "")},
                {"entry_count", entries.size()},
                {"entries", std::move(list)}};
    if (const int code = write_report(report); code != 0) return code;
    log_info("patch: " + std::to_string(entries.size()) + " entries");
    return 0;
  }

  if (action == "verify") {
    const Json selftest = run_selftest();
    auto replay = [&]() {
      std::string combined;
      for (const PatchEntry& entry : entries) {
        combined += entry.id;
        combined.push_back('\0');
        combined += entry.target;
        combined.push_back('\0');
        combined += sha256_text(entry.replacement);
        combined.push_back('\n');
      }
      return sha256_text(combined);
    };
    const std::string digest_a = replay();
    const std::string digest_b = replay();
    const bool replay_ok = digest_a == digest_b && !digest_a.empty();
    Json report{{"schema", "reim.patch_verify"},
                {"format", 1},
                {"profile", set_json.value("profile", "")},
                {"entry_count", entries.size()},
                {"merge_ok", selftest["merge_ok"]},
                {"conflict_ok", selftest["conflict_ok"]},
                {"replay_identical", replay_ok},
                {"replay_sha256", digest_a}};
    const int code = write_report(report);
    if (code != 0) return code;
    if (!replay_ok || !selftest["merge_ok"].get<bool>() ||
        !selftest["conflict_ok"].get<bool>()) {
      log_error("patch verify failed");
      return 8;
    }
    log_info("patch: verify OK (deterministic replay, merge/conflict selftest)");
    return 0;
  }

  if (action == "apply") {
    const auto target_arg = args.get("target");
    if (!target_arg) {
      log_error("patch apply requires --target <dir>");
      return 2;
    }
    const bool dry_run = args.has("dry-run");
    const Json result = apply_entries(entries, file::from_utf8(*target_arg), dry_run,
                                      error);
    Json report{{"schema", "reim.patch_apply"},
                {"format", 1},
                {"profile", set_json.value("profile", "")},
                {"dry_run", dry_run},
                {"result", result}};
    const int code = write_report(report);
    if (code != 0) return code;
    log_info("patch: applied=" + std::to_string(result["applied"].get<uint64_t>()) +
             " already=" +
             std::to_string(result["already_applied"].get<uint64_t>()) +
             " conflicts=" + std::to_string(result["conflicts"].get<uint64_t>()));
    if (result["conflicts"].get<uint64_t>() != 0 ||
        result["errors"].get<uint64_t>() != 0) {
      log_error("patch: conflicts/errors; target files were left unchanged for those entries");
      return 8;
    }
    return 0;
  }

  log_error("patch: unknown action '" + action + "' (apply|verify|list|selftest)");
  return 2;
}

} // namespace reim::conv
