#include "ledger.h"

#include "util/file.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

const char* const kStatuses[] = {"complete", "partial", "not_started", "not_run"};
const char* const kKinds[] = {"native", "generated", "patch", "analysis"};

bool known_status(const std::string& value) {
  for (const char* status : kStatuses) {
    if (value == status) return true;
  }
  return false;
}

bool known_kind(const std::string& value) {
  for (const char* kind : kKinds) {
    if (value == kind) return true;
  }
  return false;
}

} // namespace

int run_ledger(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const std::string profile = args.get("profile").value_or("26.3");
  const auto out_arg = args.get("out");
  if (!out_arg) {
    log_error("ledger requires --profile 26.3 --out work/26.3 "
              "[--ledger converter/ledger/26.3.json] [--check]");
    return 2;
  }
  const auto out_dir = file::from_utf8(*out_arg);
  const auto ledger_path = args.get("ledger").has_value()
      ? file::from_utf8(*args.get("ledger"))
      : std::filesystem::path("converter") / "ledger" / (profile + ".json");
  std::string error;
  Json ledger;
  if (!read_json_file(ledger_path, ledger, error)) {
    log_error("ledger: " + error);
    return 3;
  }
  if (ledger.value("schema", "") != "reim.port_ledger" ||
      ledger.value("format", 0) != 1) {
    log_error("ledger: expected schema reim.port_ledger format 1");
    return 4;
  }
  if (ledger.value("profile", "") != profile) {
    log_error("ledger: profile mismatch: ledger '" + ledger.value("profile", "") +
              "', requested '" + profile + "'");
    return 4;
  }
  const auto entries_it = ledger.find("entries");
  if (entries_it == ledger.end() || !entries_it->is_array()) {
    log_error("ledger: entries[] missing");
    return 4;
  }

  Json report_entries = Json::array();
  std::map<std::string, std::map<std::string, uint64_t>> by_area_status;
  std::map<std::string, uint64_t> by_kind;
  std::set<std::string> missing_modules;
  std::vector<std::string> problems;
  std::set<std::string> seen_symbols;
  for (const Json& entry : *entries_it) {
    if (!entry.is_object()) {
      problems.push_back("entry is not an object");
      continue;
    }
    const std::string area = entry.value("area", "");
    const std::string status = entry.value("status", "");
    const std::string kind = entry.value("kind", "");
    const std::string symbol = entry.value("java_symbol", "");
    const std::string module = entry.value("native_module", "");
    if (area.empty() || symbol.empty()) {
      problems.push_back("entry lacks area/java_symbol");
      continue;
    }
    if (!known_status(status)) {
      problems.push_back(symbol + ": unknown status '" + status + "'");
    }
    if (!known_kind(kind)) {
      problems.push_back(symbol + ": unknown kind '" + kind + "'");
    }
    if (!seen_symbols.insert(symbol).second) {
      problems.push_back(symbol + ": duplicate entry");
    }
    bool module_exists = true;
    if (!module.empty() && kind != "generated" && kind != "analysis") {
      module_exists = file::exists(file::from_utf8(module));
      if (!module_exists) missing_modules.insert(module);
    }
    by_area_status[area][status] += 1;
    by_kind[kind] += 1;
    report_entries.push_back(Json{{"java_symbol", symbol},
                                  {"area", area},
                                  {"kind", kind},
                                  {"native_module", module},
                                  {"module_exists", module_exists},
                                  {"status", status},
                                  {"evidence", entry.value("evidence", "")}});
  }

  Json areas = Json::object();
  for (const auto& [area, statuses] : by_area_status) {
    Json counts = Json::object();
    for (const auto& [status, count] : statuses) counts[status] = count;
    areas[area] = std::move(counts);
  }
  Json kinds = Json::object();
  for (const auto& [kind, count] : by_kind) kinds[kind] = count;

  Json report;
  report["schema"] = "reim.port_ledger_report";
  report["format"] = 1;
  report["profile"] = profile;
  report["entry_count"] = report_entries.size();
  report["by_area"] = std::move(areas);
  report["by_kind"] = std::move(kinds);
  report["missing_modules"] = Json::array();
  for (const std::string& module : missing_modules) {
    report["missing_modules"].push_back(module);
  }
  report["problems"] = Json::array();
  for (const std::string& problem : problems) report["problems"].push_back(problem);
  report["entries"] = std::move(report_entries);
  report["note"] =
      "Status is evidence-based: complete/partial/not_started/not_run. A "
      "generated row means data exists, not that gameplay consumes it.";

  if (!file::ensure_directory(out_dir / "analysis", error)) {
    log_error(error);
    return 5;
  }
  if (!write_json_file(out_dir / "analysis" / "port_ledger.json", report, error)) {
    log_error(error);
    return 5;
  }
  log_info("ledger: " + std::to_string(report["entry_count"].get<uint64_t>()) +
           " entries, " + std::to_string(problems.size()) + " problems, " +
           std::to_string(missing_modules.size()) + " missing modules");

  if (args.has("check") && (!problems.empty() || !missing_modules.empty())) {
    for (const std::string& problem : problems) log_error("ledger: " + problem);
    for (const std::string& module : missing_modules) {
      log_error("ledger: native module missing: " + module);
    }
    return 6;
  }
  return 0;
}

} // namespace reim::conv
