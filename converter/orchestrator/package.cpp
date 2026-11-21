/*
 * M2-E4/E5: NativeVersion packaging and NativeBuild assembly.
 *
 *   package  : mc_26.3.dll + generated manifest reference + ue_adapter.json
 *              -> native_version.json / verification.json / instance template
 *   assemble : UE Runtime package + NativeVersion -> final launchable NativeBuild
 *
 * Both are offline, deterministic and contain no absolute developer paths and
 * no timestamps (M2-E1/E4/E5/E7). Hash inputs are content only: the same
 * inputs and tool version produce identical files.
 */
#include "package.h"

#include "reim_abi.h"

#include "data/generated_manifest.h"
#include "util/file.h"
#include "util/hash.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

constexpr const char* kToolVersion = "1.0.0-m2";
constexpr const char* kDllFileName = "mc_26.3.dll";

struct FileDigest {
  std::string path;
  std::string sha256;
  uint64_t size = 0;
};

bool hash_file(const std::filesystem::path& path, FileDigest& out, std::string& error) {
  file::Bytes bytes;
  if (!file::read_all(path, bytes, error)) return false;
  out.sha256 = hash::hex(hash::sha256(bytes.data(), bytes.size()));
  out.size = bytes.size();
  return true;
}

std::string relative_posix(const std::filesystem::path& root,
                           const std::filesystem::path& path) {
  return path.lexically_relative(root).generic_string();
}

bool list_files(const std::filesystem::path& root, std::vector<std::filesystem::path>& out,
                std::string& error) {
  if (!file::is_directory(root)) {
    error = "directory not found: " + file::to_utf8(root);
    return false;
  }
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
    if (ec) {
      error = "cannot walk " + file::to_utf8(root) + ": " + ec.message();
      return false;
    }
    if (entry.is_regular_file()) out.push_back(entry.path());
  }
  if (ec) {
    error = "cannot walk " + file::to_utf8(root) + ": " + ec.message();
    return false;
  }
  std::sort(out.begin(), out.end());
  return true;
}

/* Content digest of a directory: sorted relative path + file sha256. */
bool digest_directory(const std::filesystem::path& root, std::string& out_digest,
                      uint64_t& out_files, std::string& error) {
  std::vector<std::filesystem::path> files;
  if (!list_files(root, files, error)) return false;
  std::string text;
  for (const auto& path : files) {
    FileDigest digest;
    digest.path = relative_posix(root, path);
    if (!hash_file(path, digest, error)) return false;
    text += digest.path;
    text.push_back('\0');
    text += digest.sha256;
    text.push_back('\n');
  }
  out_digest = hash::hex(hash::sha256(text));
  out_files = files.size();
  return true;
}

bool copy_directory(const std::filesystem::path& from, const std::filesystem::path& to,
                    std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(to, ec);
  if (ec) {
    error = "cannot create " + file::to_utf8(to) + ": " + ec.message();
    return false;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(from, ec)) {
    if (ec) {
      error = "cannot walk " + file::to_utf8(from) + ": " + ec.message();
      return false;
    }
    const auto target = to / entry.path().lexically_relative(from);
    if (entry.is_directory()) {
      std::filesystem::create_directories(target, ec);
      if (ec) {
        error = "cannot create " + file::to_utf8(target) + ": " + ec.message();
        return false;
      }
    } else if (entry.is_regular_file()) {
      std::filesystem::create_directories(target.parent_path(), ec);
      std::filesystem::copy_file(entry.path(), target,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        error = "cannot copy " + file::to_utf8(entry.path()) + ": " + ec.message();
        return false;
      }
    }
  }
  return true;
}

std::optional<Json> read_adapter(const std::filesystem::path& adapter_dir, Json& out,
                                 std::string& adapter_text, std::string& error) {
  const auto path = adapter_dir / "ue_adapter.json";
  if (!file::read_text(path, adapter_text, error)) return std::nullopt;
  out = parse_json_noexcept(adapter_text);
  if (out.is_discarded() || !out.is_object()) {
    error = "ue_adapter.json is not valid JSON: " + file::to_utf8(path);
    return std::nullopt;
  }
  const int format = out.value("format", 0);
  const int abi = out.value("abi_version", 0);
  const int render = out.value("render_level", -1);
  const std::string ue = out.value("ue_version", std::string());
  if (format != 2 || abi != static_cast<int>(REIM_ABI_VERSION) || render != 0 ||
      ue != "5.8.4") {
    error = std::format(
        "ue_adapter.json rejected: format={} abi={} render_level={} ue_version='{}' "
        "(need 2/{}/0/5.8.4)",
        format, abi, render, ue, REIM_ABI_VERSION);
    return std::nullopt;
  }
  if (!out.contains("adapter_id") || !out.contains("adapter_version") ||
      !out.contains("asset_root_sha256")) {
    error = "ue_adapter.json lacks adapter_id/adapter_version/asset_root_sha256";
    return std::nullopt;
  }
  return out;
}

Json instance_template() {
  return Json{{"world_path", ""},
              {"generated_path", ""},
              {"native_version_path", "."},
              {"minecraft_dir", ""},
              {"render_distance", 6},
              {"mouse_sensitivity", 0.15},
              {"invert_y", false},
              {"render_level", 0},
              {"auto_start", true},
              {"log_dir", "logs"}};
}

} // namespace

int run_package(const Args& args, const std::filesystem::path&) {
  const std::string profile = args.get("profile").value_or("26.3");
  const auto generated_arg = args.get("generated");
  const auto dll_arg = args.get("dll");
  const auto adapter_arg = args.get("ue-adapter");
  const auto out_arg = args.get("out");
  if (!generated_arg || !dll_arg || !adapter_arg || !out_arg) {
    log_error("package requires --generated <dir> --dll <mc_26.3.dll> "
              "--ue-adapter <dir> --out <dir> [--profile 26.3]");
    return 2;
  }
  const auto generated_dir = file::from_utf8(*generated_arg);
  const auto dll_path = file::from_utf8(*dll_arg);
  const auto adapter_dir = file::from_utf8(*adapter_arg);
  const auto out_dir = file::from_utf8(*out_arg);

  std::string error;
  reim::data::GeneratedManifest manifest;
  if (!reim::data::validate_generated(generated_dir, profile, REIM_ABI_VERSION, manifest,
                                      error)) {
    log_error("package: generated data rejected: " + error);
    return 3;
  }
  log_info(std::format("package: generated manifest OK (format {}, ABI {}, {} files, "
                       "{} blocks)",
                       manifest.format, manifest.abi_version, manifest.files.size(),
                       manifest.blocks_total));

  FileDigest dll_digest;
  if (!file::exists(dll_path)) {
    log_error("package: DLL not found: " + file::to_utf8(dll_path));
    return 4;
  }
  if (dll_path.filename() != kDllFileName) {
    log_error(std::string("package: DLL must be named ") + kDllFileName + ", got " +
              file::to_utf8(dll_path.filename()));
    return 4;
  }
  if (!hash_file(dll_path, dll_digest, error)) {
    log_error("package: " + error);
    return 4;
  }

  Json adapter;
  std::string adapter_text;
  if (!read_adapter(adapter_dir, adapter, adapter_text, error)) {
    log_error("package: " + error);
    return 5;
  }
  const std::string adapter_sha256 = hash::hex(hash::sha256(adapter_text));

  std::string manifest_text;
  if (!file::read_text(generated_dir / "manifest.json", manifest_text, error)) {
    log_error("package: " + error);
    return 3;
  }
  const std::string manifest_sha256 = hash::hex(hash::sha256(manifest_text));

  if (!file::ensure_directory(out_dir, error)) {
    log_error("package: " + error);
    return 6;
  }
  std::error_code ec;
  std::filesystem::copy_file(dll_path, out_dir / kDllFileName,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    log_error("package: cannot copy DLL: " + ec.message());
    return 6;
  }
  if (!file::write_text(out_dir / "ue_adapter.json", adapter_text, error)) {
    log_error("package: " + error);
    return 6;
  }
  if (!write_json_file(out_dir / "instance.json.template", instance_template(), error)) {
    log_error("package: " + error);
    return 6;
  }

  Json files = Json::array();
  for (const auto& entry : manifest.files) {
    files.push_back(Json{{"path", entry.path},
                         {"sha256", entry.sha256},
                         {"size", entry.size}});
  }

  Json native_version;
  native_version["schema"] = "reim.native_version";
  native_version["format"] = 2;
  native_version["profile"] = profile;
  native_version["abi_version"] = REIM_ABI_VERSION;
  native_version["tool"] = "reim-convert";
  native_version["tool_version"] = kToolVersion;
  native_version["dll"] = Json{{"file", kDllFileName},
                               {"sha256", dll_digest.sha256},
                               {"size", dll_digest.size}};
  native_version["generated"] = Json{{"manifest_sha256", manifest_sha256},
                                     {"files", files}};
  native_version["adapter"] = Json{
      {"file", "ue_adapter.json"},
      {"sha256", adapter_sha256},
      {"id", adapter["adapter_id"]},
      {"version", adapter["adapter_version"]},
      {"ue_version", adapter["ue_version"]},
      {"abi_version", adapter["abi_version"]},
      {"render_level", adapter["render_level"]},
      {"asset_root_sha256", adapter["asset_root_sha256"]}};
  native_version["capabilities"] = manifest.capabilities;
  if (!write_json_file(out_dir / "native_version.json", native_version, error)) {
    log_error("package: " + error);
    return 6;
  }

  Json verification;
  verification["schema"] = "reim.native_version_verification";
  verification["format"] = 2;
  verification["tool"] = "reim-convert";
  verification["tool_version"] = kToolVersion;
  verification["profile"] = profile;
  verification["abi_version"] = REIM_ABI_VERSION;
  verification["result"] = "ok";
  verification["checks"] = Json::array(
      {Json{{"name", "generated_manifest"}, {"ok", true}, {"format", manifest.format}},
       Json{{"name", "generated_files"},
            {"ok", true},
            {"count", manifest.files.size()}},
       Json{{"name", "dll"},
            {"ok", true},
            {"size", dll_digest.size},
            {"sha256", dll_digest.sha256}},
       Json{{"name", "adapter"},
            {"ok", true},
            {"id", adapter["adapter_id"]},
            {"version", adapter["adapter_version"]},
            {"assets", adapter["assets"].size()}}});
  if (!write_json_file(out_dir / "verification.json", verification, error)) {
    log_error("package: " + error);
    return 6;
  }

  log_info("package: wrote NativeVersion to " + file::to_utf8(out_dir));
  log_info("package: dll sha256=" + dll_digest.sha256 + " adapter=" +
           adapter["adapter_id"].get<std::string>() + " " +
           adapter["adapter_version"].get<std::string>() + " manifest=" + manifest_sha256);
  return 0;
}

int run_assemble(const Args& args, const std::filesystem::path&) {
  const auto nv_arg = args.get("native-version");
  const auto runtime_arg = args.get("runtime-package");
  const auto out_arg = args.get("out");
  if (!nv_arg || !runtime_arg || !out_arg) {
    log_error("assemble requires --native-version <dir> --runtime-package <dir> "
              "--out <dir>");
    return 2;
  }
  const auto nv_dir = file::from_utf8(*nv_arg);
  const auto runtime_dir = file::from_utf8(*runtime_arg);
  const auto out_dir = file::from_utf8(*out_arg);

  std::string error;
  Json nv;
  if (!read_json_file(nv_dir / "native_version.json", nv, error)) {
    log_error("assemble: " + error);
    return 3;
  }
  const int format = nv.value("format", 0);
  const int abi = nv.value("abi_version", 0);
  const std::string profile = nv.value("profile", std::string());
  if (format != 2 || abi != static_cast<int>(REIM_ABI_VERSION) || profile.empty()) {
    log_error(std::format("assemble: native_version.json rejected (format={} abi={} "
                          "profile='{}')",
                          format, abi, profile));
    return 3;
  }
  const std::string dll_file = nv.value("dll", Json::object()).value("file", std::string());
  const std::string dll_sha = nv.value("dll", Json::object()).value("sha256", std::string());
  if (dll_file != kDllFileName || dll_sha.empty()) {
    log_error("assemble: native_version.json has no valid dll descriptor");
    return 3;
  }
  FileDigest nv_dll;
  if (!hash_file(nv_dir / dll_file, nv_dll, error)) {
    log_error("assemble: " + error);
    return 3;
  }
  if (nv_dll.sha256 != dll_sha) {
    log_error("assemble: NativeVersion DLL hash mismatch (tampered package)");
    return 3;
  }
  if (!file::exists(runtime_dir)) {
    log_error("assemble: runtime package not found: " + file::to_utf8(runtime_dir));
    return 4;
  }

  std::error_code ec;
  std::filesystem::remove_all(out_dir, ec);
  const auto runtime_out = out_dir / "REIMRuntime";
  const auto nv_out = out_dir / "NativeVersions" / profile;
  if (!copy_directory(runtime_dir, runtime_out, error) ||
      !copy_directory(nv_dir, nv_out, error)) {
    log_error("assemble: " + error);
    return 5;
  }
  file::Bytes templ;
  if (file::read_all(nv_dir / "instance.json.template", templ, error)) {
    if (!file::write_all(out_dir / "instance.json.template", templ.data(), templ.size(),
                         error)) {
      log_error("assemble: " + error);
      return 5;
    }
  }

  std::string runtime_digest;
  std::string nv_digest;
  uint64_t runtime_files = 0;
  uint64_t nv_files = 0;
  if (!digest_directory(runtime_out, runtime_digest, runtime_files, error) ||
      !digest_directory(nv_out, nv_digest, nv_files, error)) {
    log_error("assemble: " + error);
    return 5;
  }

  Json adapter = nv.value("adapter", Json::object());
  std::string adapter_identity = adapter.value("id", std::string()) + "/" +
                                 adapter.value("version", std::string()) + "/" +
                                 adapter.value("asset_root_sha256", std::string());
  const std::string build_text = runtime_digest + "\n" + nv_digest + "\n" +
                                 adapter_identity + "\n" + std::string(kToolVersion);
  const std::string build_sha = hash::hex(hash::sha256(build_text));

  Json build;
  build["schema"] = "reim.native_build";
  build["format"] = 2;
  build["profile"] = profile;
  build["abi_version"] = REIM_ABI_VERSION;
  build["ue_version"] = adapter.value("ue_version", std::string("5.8.4"));
  build["tool"] = "reim-convert";
  build["tool_version"] = kToolVersion;
  build["runtime"] = Json{{"path", "REIMRuntime"},
                          {"files", runtime_files},
                          {"sha256", runtime_digest}};
  build["native_version"] = Json{{"path", "NativeVersions/" + profile},
                                 {"files", nv_files},
                                 {"sha256", nv_digest},
                                 {"dll_sha256", nv_dll.sha256},
                                 {"native_version_json_sha256",
                                  [&]() {
                                    FileDigest digest;
                                    std::string ignored;
                                    hash_file(nv_dir / "native_version.json", digest, ignored);
                                    return digest.sha256;
                                  }()}};
  build["adapter"] = Json{{"id", adapter.value("id", std::string())},
                          {"version", adapter.value("version", std::string())},
                          {"ue_version", adapter.value("ue_version", std::string())},
                          {"asset_root_sha256",
                           adapter.value("asset_root_sha256", std::string())},
                          {"ue_adapter_sha256", adapter.value("sha256", std::string())}};
  build["native_build_sha256"] = build_sha;
  if (!write_json_file(out_dir / "native_build.json", build, error)) {
    log_error("assemble: " + error);
    return 6;
  }

  log_info("assemble: wrote NativeBuild to " + file::to_utf8(out_dir));
  log_info(std::format("assemble: runtime {} files digest {}; native version {} files "
                       "digest {}; build {}",
                       runtime_files, runtime_digest, nv_files, nv_digest, build_sha));
  return 0;
}

} // namespace reim::conv
