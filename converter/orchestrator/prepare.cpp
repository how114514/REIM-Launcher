#include "prepare.h"

#include "util/file.h"
#include "util/hash.h"

#include <algorithm>
#include <format>
#include <set>

namespace reim::conv {
namespace {

struct ResolvedInputs {
  std::filesystem::path minecraft_dir;
  std::filesystem::path version_dir;
  std::filesystem::path version_json;
  std::filesystem::path client_jar;
  std::filesystem::path assets_dir;
  std::filesystem::path asset_index;
  std::filesystem::path libraries_dir;
  std::string version_id;
  std::string asset_index_id;
};

std::string get_string(const Json& object, const char* name,
                       std::string_view fallback = {}) {
  if (!object.is_object()) return std::string(fallback);
  const auto it = object.find(name);
  if (it == object.end() || !it->is_string()) return std::string(fallback);
  return it->get<std::string>();
}

uint64_t get_u64(const Json& object, const char* name, uint64_t fallback = 0) {
  if (!object.is_object()) return fallback;
  const auto it = object.find(name);
  if (it == object.end()) return fallback;
  if (it->is_number_unsigned()) return it->get<uint64_t>();
  if (it->is_number_integer()) {
    const int64_t value = it->get<int64_t>();
    return value < 0 ? fallback : static_cast<uint64_t>(value);
  }
  return fallback;
}

bool resolve_inputs(const std::filesystem::path& given, const std::string& version,
                    ResolvedInputs& out, std::string& error) {
  if (!file::is_directory(given)) {
    error = "minecraft directory not found: " + file::to_utf8(given);
    return false;
  }

  std::filesystem::path mc_root;
  std::filesystem::path version_dir;
  std::filesystem::path version_json;

  const auto standard = given / "versions" / version / (version + ".json");
  if (file::exists(standard)) {
    mc_root = given;
    version_dir = given / "versions" / version;
    version_json = standard;
  } else {
    const auto direct = given / (version + ".json");
    if (file::exists(direct)) {
      version_dir = given;
      version_json = direct;
      mc_root = given.parent_path().filename() == "versions"
                    ? given.parent_path().parent_path()
                    : given;
    } else {
      error = "26.3 version JSON not found under " + file::to_utf8(given) +
              " (expected versions/" + version + "/" + version + ".json)";
      return false;
    }
  }

  Json version_doc;
  if (!read_json_file(version_json, version_doc, error)) return false;
  if (!version_doc.is_object()) {
    error = "version JSON is not an object";
    return false;
  }

  const std::string id = get_string(version_doc, "id", version);
  const std::string jar_name = get_string(version_doc, "jar", version);
  std::filesystem::path jar = version_dir / (jar_name + ".jar");
  if (!file::exists(jar)) jar = version_dir / "client.jar";
  if (!file::exists(jar)) {
    error = "client JAR not found in " + file::to_utf8(version_dir);
    return false;
  }

  const std::filesystem::path assets = mc_root / "assets";
  std::string asset_id = version;
  if (version_doc.contains("assetIndex")) {
    const Json& asset_index_doc = version_doc["assetIndex"];
    if (asset_index_doc.is_string()) {
      asset_id = asset_index_doc.get<std::string>();
    } else if (asset_index_doc.is_object() && asset_index_doc.contains("id") &&
               asset_index_doc["id"].is_string()) {
      asset_id = asset_index_doc["id"].get<std::string>();
    }
  }
  std::filesystem::path index = assets / "indexes" / (asset_id + ".json");
  if (!file::exists(index)) {
    // A few installations use assets/indexes/<version>.json.
    index = assets / "indexes" / (version + ".json");
  }
  if (!file::exists(index)) {
    error = "asset index not found under " + file::to_utf8(assets / "indexes");
    return false;
  }

  out.minecraft_dir = mc_root;
  out.version_dir = version_dir;
  out.version_json = version_json;
  out.client_jar = jar;
  out.assets_dir = assets;
  out.asset_index = index;
  out.libraries_dir = mc_root / "libraries";
  out.version_id = id;
  out.asset_index_id = asset_id;
  return true;
}

void hash_file(const std::filesystem::path& path, std::string& sha1_hex,
               std::string& sha256_hex, uint64_t& size, std::string& error) {
  file::Bytes bytes;
  if (!file::read_all(path, bytes, error)) return;
  size = bytes.size();
  sha1_hex = hash::hex(hash::sha1(bytes.data(), bytes.size()));
  sha256_hex = hash::hex(hash::sha256(bytes.data(), bytes.size()));
}

} // namespace

bool load_input_manifest(const std::filesystem::path& profile_dir, InputManifest& out,
                         std::string& error) {
  const auto path = profile_dir / "input_manifest.json";
  if (!read_json_file(path, out.json, error)) return false;
  if (!out.json.is_object()) {
    error = "input_manifest.json is not an object";
    return false;
  }
  out.profile = get_string(out.json, "profile");
  out.minecraft_dir = file::from_utf8(get_string(out.json, "minecraft_dir"));
  out.version_dir = file::from_utf8(get_string(out.json, "version_dir"));
  out.version_json = file::from_utf8(get_string(out.json, "version_json"));
  out.client_jar = file::from_utf8(get_string(out.json, "client_jar"));
  out.asset_index = file::from_utf8(get_string(out.json, "asset_index"));
  out.version_id = get_string(out.json, "version_id");
  out.client_jar_sha256 = get_string(out.json, "client_jar_sha256");
  out.version_json_sha256 = get_string(out.json, "version_json_sha256");
  out.asset_index_sha256 = get_string(out.json, "asset_index_sha256");
  out.client_jar_size = get_u64(out.json, "client_jar_size");
  out.version_json_size = get_u64(out.json, "version_json_size");
  out.asset_index_size = get_u64(out.json, "asset_index_size");
  if (out.profile.empty() || out.client_jar.empty()) {
    error = "input_manifest.json lacks profile/client_jar; run prepare first";
    return false;
  }
  return true;
}

int run_prepare(const Args& args, const std::filesystem::path&) {
  const auto minecraft_arg = args.get("minecraft-dir");
  const auto out_arg = args.get("out");
  const std::string version = args.get("version").value_or("26.3");
  if (!minecraft_arg || !out_arg) {
    log_error("prepare requires --minecraft-dir <dir> --version 26.3 --out <dir>");
    return 2;
  }

  const auto minecraft_dir = file::from_utf8(*minecraft_arg);
  const auto out_dir = file::from_utf8(*out_arg);
  std::string error;
  if (!file::ensure_directory(out_dir, error)) {
    log_error(error);
    return 3;
  }

  ResolvedInputs inputs;
  if (!resolve_inputs(minecraft_dir, version, inputs, error)) {
    log_error(error);
    return 4;
  }

  std::string client_sha1;
  std::string client_sha256;
  uint64_t client_size = 0;
  hash_file(inputs.client_jar, client_sha1, client_sha256, client_size, error);
  if (!error.empty()) {
    log_error(error);
    return 5;
  }
  std::string version_sha1_unused;
  std::string version_sha256;
  uint64_t version_size = 0;
  hash_file(inputs.version_json, version_sha1_unused, version_sha256, version_size, error);
  if (!error.empty()) {
    log_error(error);
    return 5;
  }
  std::string index_sha1_unused;
  std::string index_sha256;
  uint64_t index_size = 0;
  hash_file(inputs.asset_index, index_sha1_unused, index_sha256, index_size, error);
  if (!error.empty()) {
    log_error(error);
    return 5;
  }

  Json version_doc;
  if (!read_json_file(inputs.version_json, version_doc, error)) {
    log_error(error);
    return 6;
  }

  std::vector<std::string> warnings;
  const Json* downloads = version_doc.contains("downloads") ? &version_doc["downloads"] : nullptr;
  if (downloads && downloads->is_object() && downloads->contains("client")) {
    const Json& client = (*downloads)["client"];
    if (client.is_object() && client.contains("sha1") && client["sha1"].is_string()) {
      const std::string expected = client["sha1"].get<std::string>();
      if (expected != client_sha1) {
        log_error("client JAR SHA-1 mismatch: version JSON " + expected +
                  ", actual " + client_sha1);
        return 7;
      }
    }
    if (client.is_object() && client.contains("size") && client["size"].is_number_unsigned()) {
      const uint64_t expected_size = client["size"].get<uint64_t>();
      if (expected_size != client_size) {
        log_error("client JAR size mismatch: version JSON " +
                  std::to_string(expected_size) + ", actual " +
                  std::to_string(client_size));
        return 7;
      }
    }
  }

  int library_total = 0;
  int library_missing = 0;
  std::vector<std::string> missing_libraries;
  if (version_doc.contains("libraries") && version_doc["libraries"].is_array()) {
    for (const Json& library : version_doc["libraries"]) {
      if (!library.is_object() || !library.contains("downloads")) continue;
      const Json& dls = library["downloads"];
      if (!dls.is_object() || !dls.contains("artifact")) continue;
      const Json& artifact = dls["artifact"];
      if (!artifact.is_object()) continue;
      const std::string relative = get_string(artifact, "path");
      if (relative.empty()) continue;
      ++library_total;
      const auto path = inputs.libraries_dir / file::from_utf8(relative);
      if (!file::exists(path)) {
        ++library_missing;
        if (missing_libraries.size() < 32) missing_libraries.push_back(relative);
      }
    }
  }
  if (library_missing > 0) {
    warnings.push_back(std::to_string(library_missing) + " of " +
                       std::to_string(library_total) + " library artifacts are missing");
  }

  Json index_doc;
  if (!read_json_file(inputs.asset_index, index_doc, error)) {
    log_error(error);
    return 8;
  }
  int asset_total = 0;
  int asset_missing = 0;
  std::vector<std::string> missing_assets;
  if (index_doc.contains("objects") && index_doc["objects"].is_object()) {
    for (auto it = index_doc["objects"].begin(); it != index_doc["objects"].end(); ++it) {
      if (!it.value().is_object()) continue;
      const std::string hash_value = get_string(it.value(), "hash");
      if (hash_value.size() < 4) continue;
      ++asset_total;
      const auto path = inputs.assets_dir / "objects" / hash_value.substr(0, 2) / hash_value;
      if (!file::exists(path)) {
        ++asset_missing;
        if (missing_assets.size() < 32) missing_assets.push_back(it.key());
      }
    }
  }
  if (asset_total == 0) {
    log_error("asset index has no objects");
    return 8;
  }
  if (asset_missing > 0) {
    warnings.push_back(std::to_string(asset_missing) + " of " +
                       std::to_string(asset_total) + " asset objects are missing");
  }

  Json server_jar = nullptr;
  if (const auto server = args.get("server-jar"); server && !server->empty()) {
    const auto path = file::from_utf8(*server);
    if (!file::exists(path)) {
      warnings.push_back("server JAR not found: " + *server);
    } else {
      std::string sha1;
      std::string sha256;
      uint64_t size = 0;
      hash_file(path, sha1, sha256, size, error);
      server_jar = Json{{"path", *server},
                        {"size", size},
                        {"sha1", sha1},
                        {"sha256", sha256}};
    }
  }

  Json missing_libs = Json::array();
  for (const std::string& value : missing_libraries) missing_libs.push_back(value);
  Json missing_assets_json = Json::array();
  for (const std::string& value : missing_assets) missing_assets_json.push_back(value);
  Json warnings_json = Json::array();
  for (const std::string& value : warnings) warnings_json.push_back(value);

  Json manifest;
  manifest["format"] = 1;
  manifest["profile"] = version;
  manifest["version_id"] = inputs.version_id;
  manifest["minecraft_dir"] = file::to_utf8(inputs.minecraft_dir);
  manifest["version_dir"] = file::to_utf8(inputs.version_dir);
  manifest["version_json"] = file::to_utf8(inputs.version_json);
  manifest["version_json_size"] = version_size;
  manifest["version_json_sha256"] = version_sha256;
  manifest["client_jar"] = file::to_utf8(inputs.client_jar);
  manifest["client_jar_size"] = client_size;
  manifest["client_jar_sha1"] = client_sha1;
  manifest["client_jar_sha256"] = client_sha256;
  manifest["asset_index"] = file::to_utf8(inputs.asset_index);
  manifest["asset_index_id"] = inputs.asset_index_id;
  manifest["asset_index_size"] = index_size;
  manifest["asset_index_sha256"] = index_sha256;
  manifest["assets_dir"] = file::to_utf8(inputs.assets_dir);
  manifest["assets_objects"] = asset_total;
  manifest["assets_missing"] = asset_missing;
  manifest["assets_missing_sample"] = missing_assets_json;
  manifest["libraries_dir"] = file::to_utf8(inputs.libraries_dir);
  manifest["libraries_total"] = library_total;
  manifest["libraries_missing"] = library_missing;
  manifest["libraries_missing_sample"] = missing_libs;
  manifest["server_jar"] = server_jar;
  manifest["warnings"] = warnings_json;
  int java_major = 0;
  if (version_doc.contains("javaVersion") && version_doc["javaVersion"].is_object()) {
    const Json& java_doc = version_doc["javaVersion"];
    if (java_doc.contains("majorVersion") && java_doc["majorVersion"].is_number_integer()) {
      java_major = java_doc["majorVersion"].get<int>();
    }
  }
  manifest["java_version"] = java_major;

  const auto output = out_dir / "input_manifest.json";
  if (!write_json_file(output, manifest, error)) {
    log_error(error);
    return 9;
  }

  log_info(std::format("prepare: {} -> {}", file::to_utf8(inputs.client_jar),
                       file::to_utf8(output)));
  log_info(std::format("prepare: client.jar {} bytes, sha256 {}, libraries {}/{}, assets {}/{}",
                       client_size, client_sha256, library_total - library_missing,
                       library_total, asset_total - asset_missing, asset_total));
  for (const std::string& warning : warnings) log_warn("prepare: " + warning);
  return 0;
}

} // namespace reim::conv
