#include "data.h"

#include "prepare.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/utf.h"
#include "util/zip.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <stdlib.h>
#endif

namespace reim::conv {
namespace {

/* The M3 event set. K6 copies only the OGG files these events reference; the
 * complete asset index (with object hashes) is still written for packaging. */
const char* const kM3SoundEvents[] = {
    "block.stone.break",   "block.stone.place",   "block.stone.step",
    "block.stone.hit",     "block.wood.break",    "block.wood.place",
    "block.wood.step",     "block.wood.hit",      "block.grass.break",
    "block.grass.place",   "block.grass.step",    "block.grass.hit",
    "block.gravel.break",  "block.gravel.place",  "block.gravel.step",
    "block.sand.break",    "block.sand.place",    "block.sand.step",
    "block.glass.break",   "block.glass.place",   "block.glass.step",
    "block.wool.break",    "block.wool.place",    "block.wool.step",
    "block.metal.break",   "block.metal.place",   "block.metal.step",
    "block.deepslate.break", "block.deepslate.place", "block.deepslate.step",
    "block.rooted_dirt.break", "block.rooted_dirt.place", "block.rooted_dirt.step",
    "block.netherrack.break", "block.netherrack.place",
    "entity.player.hurt",  "entity.player.death", "entity.player.attack.strong",
    "entity.player.attack.sweep", "entity.item.pickup",
    "entity.experience_orb.pickup", "entity.generic.explode",
    "entity.zombie.ambient", "entity.zombie.hurt", "entity.zombie.death",
    "entity.skeleton.ambient", "entity.skeleton.hurt", "entity.skeleton.death",
    "entity.creeper.primed", "entity.creeper.hurt", "entity.creeper.death",
    "entity.spider.ambient", "entity.spider.hurt", "entity.spider.death",
    "entity.cow.ambient", "entity.cow.hurt", "entity.cow.death",
    "entity.pig.ambient", "entity.pig.hurt", "entity.pig.death",
    "entity.sheep.ambient", "entity.sheep.hurt", "entity.sheep.death",
    "entity.chicken.ambient", "entity.chicken.hurt", "entity.chicken.death",
    "ui.button.click", "weather.rain", "weather.rain.above",
    "entity.lightning_bolt.thunder", "ambient.cave",
};

std::string entry_to_utf8(const std::filesystem::path& path) {
  return file::to_utf8(path);
}

bool read_entry_text(const zip::Archive& archive, const std::string& entry,
                     std::string& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!archive.read(entry, bytes, error)) return false;
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

bool write_entry(const zip::Archive& archive, const std::string& entry,
                 const std::filesystem::path& out_root, uint64_t& bytes_written,
                 std::string& error) {
  if (!safe_archive_path(entry, error)) return false;
  std::vector<uint8_t> bytes;
  if (!archive.read(entry, bytes, error)) return false;
  std::string normalized = entry;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const auto dest = out_root / file::from_utf8(normalized);
  if (!file::ensure_directory(dest.parent_path(), error)) return false;
  if (!file::write_all(dest, bytes.data(), bytes.size(), error)) return false;
  bytes_written += bytes.size();
  return true;
}

std::string sha256_of_file(const std::filesystem::path& path) {
  file::Bytes bytes;
  std::string error;
  if (!file::read_all(path, bytes, error)) return {};
  return hash::hex(hash::sha256(bytes.data(), bytes.size()));
}

std::string sha256_of_text(const std::string& text) {
  return hash::hex(hash::sha256(text));
}

/* Deterministic digest over sorted relative paths and their SHA-256. */
std::string digest_directory(const std::filesystem::path& root, uint64_t& file_count,
                             uint64_t* byte_count, std::string& error) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    error = "directory not found: " + file::to_utf8(root);
    return {};
  }
  for (std::filesystem::recursive_directory_iterator it(root, ec), end;
       it != end; it.increment(ec)) {
    if (ec) {
      error = "directory iteration failed: " + ec.message();
      return {};
    }
    if (!it->is_regular_file()) continue;
    files.push_back(std::filesystem::relative(it->path(), root, ec));
  }
  std::sort(files.begin(), files.end());
  std::string combined;
  file_count = 0;
  for (const auto& relative : files) {
    file::Bytes bytes;
    if (!file::read_all(root / relative, bytes, error)) return {};
    std::string name = file::to_utf8(relative);
    std::replace(name.begin(), name.end(), '\\', '/');
    combined += name;
    combined.push_back('\0');
    combined += hash::hex(hash::sha256(bytes.data(), bytes.size()));
    combined.push_back('\n');
    ++file_count;
    if (byte_count) *byte_count += bytes.size();
  }
  return sha256_of_text(combined);
}

std::filesystem::path java_from_environment() {
#ifdef _WIN32
  const wchar_t* home = _wgetenv(L"JAVA_HOME");
  if (home && home[0]) {
    const auto candidate = std::filesystem::path(home) / "bin" / "java.exe";
    if (file::exists(candidate)) return candidate;
  }
#else
  const char* home = std::getenv("JAVA_HOME");
  if (home && home[0]) {
    const auto candidate = std::filesystem::path(home) / "bin" / "java";
    if (file::exists(candidate)) return candidate;
  }
#endif
  return {};
}

std::string normalize_asset_name(std::string name) {
  if (name.find(':') != std::string::npos) name = strip_namespace(name);
  while (!name.empty() && name.front() == '/') name.erase(name.begin());
  return name;
}

struct ObjectEntry {
  std::string hash;
  uint64_t size = 0;
};

bool load_asset_index(const std::filesystem::path& path,
                      std::map<std::string, ObjectEntry>& objects, std::string& error) {
  Json root;
  if (!read_json_file(path, root, error)) return false;
  const auto found = root.find("objects");
  if (found == root.end() || !found->is_object()) {
    error = "asset index has no objects object: " + file::to_utf8(path);
    return false;
  }
  for (auto it = found->begin(); it != found->end(); ++it) {
    if (!it.value().is_object()) continue;
    ObjectEntry entry;
    entry.hash = it.value().value("hash", "");
    entry.size = it.value().value("size", 0ull);
    if (!entry.hash.empty()) objects.emplace(it.key(), entry);
  }
  if (objects.empty()) {
    error = "asset index contains no objects: " + file::to_utf8(path);
    return false;
  }
  return true;
}

std::filesystem::path object_path_for(const std::filesystem::path& assets_root,
                                      const std::string& hash) {
  if (hash.size() < 2) return {};
  return assets_root / "objects" / hash.substr(0, 2) / hash;
}

bool copy_asset_object(const std::filesystem::path& assets_root,
                       const std::string& key, const ObjectEntry& object,
                       const std::filesystem::path& out_assets,
                       std::set<std::string>& copied, std::string& error) {
  if (copied.count(key) != 0) return true;
  const auto source = object_path_for(assets_root, object.hash);
  if (!file::exists(source)) {
    error = "asset object missing: " + key + " (" + object.hash + ")";
    return false;
  }
  file::Bytes bytes;
  if (!file::read_all(source, bytes, error)) return false;
  const auto dest = out_assets / file::from_utf8(key);
  if (!file::ensure_directory(dest.parent_path(), error)) return false;
  if (!file::write_all(dest, bytes.data(), bytes.size(), error)) return false;
  copied.insert(key);
  return true;
}

void collect_sound_files(const Json& root, const std::string& event,
                         std::set<std::string>& files, std::set<std::string>& visiting) {
  if (visiting.count(event) != 0) return;
  const auto it = root.find(event);
  if (it == root.end() || !it->is_object()) return;
  visiting.insert(event);
  const auto sounds = it->find("sounds");
  if (sounds != it->end() && sounds->is_array()) {
    for (const Json& item : *sounds) {
      std::string name;
      std::string type;
      if (item.is_string()) {
        name = item.get<std::string>();
      } else if (item.is_object()) {
        name = item.value("name", "");
        type = item.value("type", "");
      }
      if (name.empty()) continue;
      if (type == "event") {
        collect_sound_files(root, normalize_asset_name(name), files, visiting);
        continue;
      }
      name = normalize_asset_name(name);
      if (name.size() >= 4 && name.substr(name.size() - 4) == ".ogg") {
        files.insert("minecraft/sounds/" + name);
      } else {
        files.insert("minecraft/sounds/" + name + ".ogg");
      }
    }
  }
  visiting.erase(event);
}

std::string asset_kind(const std::string& key) {
  if (key.ends_with(".ogg")) return "sound";
  if (key.rfind("minecraft/lang/", 0) == 0) return "lang";
  if (key.rfind("minecraft/font/", 0) == 0) return "font";
  if (key.rfind("minecraft/textures/font/", 0) == 0) return "font_texture";
  if (key.rfind("minecraft/textures/", 0) == 0) return "texture";
  if (key.rfind("minecraft/models/", 0) == 0) return "model";
  if (key == "minecraft/sounds.json") return "sound_index";
  return "other";
}

bool unpack_datapack(const std::filesystem::path& server_jar,
                     const std::filesystem::path& out_datapack, uint64_t& files,
                     uint64_t& json_files, uint64_t& bytes_total,
                     std::string& digest, std::string& error) {
  zip::Archive outer;
  if (!outer.open(entry_to_utf8(server_jar), error)) return false;

  std::string inner_entry;
  std::string versions;
  if (outer.contains("META-INF/versions.list") &&
      read_entry_text(outer, "META-INF/versions.list", versions, error)) {
    size_t start = 0;
    while (start < versions.size()) {
      size_t end = versions.find('\n', start);
      if (end == std::string::npos) end = versions.size();
      const std::string line = versions.substr(start, end - start);
      const size_t t1 = line.find('\t');
      const size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
      if (t2 != std::string::npos) {
        inner_entry = "META-INF/versions/" + line.substr(t2 + 1);
        break;
      }
      start = end + 1;
    }
  }

  zip::Archive inner;
  const zip::Archive* source = &outer;
  if (!inner_entry.empty() && outer.contains(inner_entry)) {
    std::vector<uint8_t> inner_bytes;
    if (!outer.read(inner_entry, inner_bytes, error)) return false;
    if (!inner.open_memory(std::move(inner_bytes), error)) return false;
    source = &inner;
  }

  std::error_code ec;
  std::filesystem::remove_all(out_datapack, ec);
  if (!file::ensure_directory(out_datapack, error)) return false;
  files = 0;
  json_files = 0;
  bytes_total = 0;
  std::vector<std::string> entries;
  for (const std::string& entry : source->list("data/")) {
    std::string safe_error;
    if (!safe_archive_path(entry, safe_error)) {
      error = safe_error;
      return false;
    }
    std::vector<uint8_t> bytes;
    if (!source->read(entry, bytes, error)) return false;
    const auto dest = out_datapack / file::from_utf8(entry);
    if (!file::ensure_directory(dest.parent_path(), error)) return false;
    if (!file::write_all(dest, bytes.data(), bytes.size(), error)) return false;
    ++files;
    if (entry.ends_with(".json")) ++json_files;
    bytes_total += bytes.size();
    entries.push_back(entry + "\0" + hash::hex(hash::sha256(bytes.data(), bytes.size())));
  }
  if (entries.empty()) {
    error = "server JAR contains no data/ entries (bundler layout not recognised)";
    return false;
  }
  std::sort(entries.begin(), entries.end());
  std::string combined;
  for (const std::string& line : entries) {
    combined += line;
    combined.push_back('\n');
  }
  digest = sha256_of_text(combined);
  return true;
}

bool copy_data_pack(const std::filesystem::path& pack_path,
                    const std::filesystem::path& out_datapack,
                    Json& pack_report, std::string& error) {
  pack_report = Json::object();
  pack_report["name"] = pack_path.filename().string();
  zip::Archive archive;
  const bool is_zip = file::exists(pack_path) && !file::is_directory(pack_path);
  std::vector<std::string> entries;
  if (is_zip) {
    if (!archive.open(entry_to_utf8(pack_path), error)) return false;
    entries = archive.list("data/");
  } else if (file::is_directory(pack_path)) {
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(pack_path, ec), end;
         it != end; it.increment(ec)) {
      if (ec) {
        error = "data pack iteration failed: " + ec.message();
        return false;
      }
      if (!it->is_regular_file()) continue;
      std::string name = file::to_utf8(std::filesystem::relative(it->path(), pack_path, ec));
      std::replace(name.begin(), name.end(), '\\', '/');
      if (name.rfind("data/", 0) == 0) entries.push_back(std::move(name));
    }
  } else {
    error = "data pack not found: " + entry_to_utf8(pack_path);
    return false;
  }

  auto read = [&](const std::string& name, std::vector<uint8_t>& bytes) -> bool {
    if (is_zip) return archive.read(name, bytes, error);
    return file::read_all(pack_path / file::from_utf8(name), bytes, error);
  };
  std::vector<uint8_t> meta_bytes;
  if (!read("pack.mcmeta", meta_bytes)) {
    error = "data pack has no pack.mcmeta";
    return false;
  }
  const Json meta = parse_json_noexcept(std::string_view(
      reinterpret_cast<const char*>(meta_bytes.data()), meta_bytes.size()));
  if (meta.is_discarded() || !meta.is_object()) {
    error = "data pack pack.mcmeta is not a JSON object";
    return false;
  }
  const auto pack = meta.find("pack");
  if (pack == meta.end() || !pack->is_object()) {
    error = "data pack pack.mcmeta has no pack object";
    return false;
  }
  const int format = pack->value("pack_format", 0);
  if (format < 90) {
    error = "data pack format is not 26.3-compatible (expected >= 90 / 97.1)";
    return false;
  }
  std::sort(entries.begin(), entries.end());
  uint64_t files = 0;
  for (const std::string& entry : entries) {
    std::string safe_error;
    if (!safe_archive_path(entry, safe_error)) {
      error = safe_error;
      return false;
    }
    std::vector<uint8_t> bytes;
    if (!read(entry, bytes)) return false;
    const auto dest = out_datapack / file::from_utf8(entry);
    if (!file::ensure_directory(dest.parent_path(), error)) return false;
    if (!file::write_all(dest, bytes.data(), bytes.size(), error)) return false;
    ++files;
  }
  pack_report["files"] = files;
  pack_report["format"] = format;
  return true;
}

bool run_reports(const std::filesystem::path& server_jar,
                 const std::filesystem::path& reports_root,
                 const std::filesystem::path& java, bool force,
                 std::string& server_sha, std::string& error) {
  server_sha = sha256_of_file(server_jar);
  if (server_sha.empty()) {
    error = "cannot hash server JAR: " + entry_to_utf8(server_jar);
    return false;
  }
  const auto stamp_path = reports_root / "server.sha256";
  const auto registries = reports_root / "reports" / "registries.json";
  std::string stamp;
  if (!force && file::exists(registries) && file::exists(stamp_path)) {
    if (file::read_text(stamp_path, stamp, error)) {
      while (!stamp.empty() && (stamp.back() == '\n' || stamp.back() == '\r')) {
        stamp.pop_back();
      }
      if (stamp == server_sha) {
        log_info("data: reports cache hit for server JAR " + server_sha.substr(0, 16));
        return true;
      }
    }
  }
  if (!file::ensure_directory(reports_root, error)) return false;
  if (!file::exists(server_jar)) {
    error = "server JAR not found: " + entry_to_utf8(server_jar);
    return false;
  }
  const std::filesystem::path java_path =
      java.empty() ? java_from_environment() : java;
  const std::string java_arg = java_path.empty() ? "java" : entry_to_utf8(java_path);
  std::string command = quote_process_arg(java_arg) +
      " -DbundlerMainClass=net.minecraft.data.Main -jar " +
      quote_process_arg(entry_to_utf8(server_jar)) + " --reports --output " +
      quote_process_arg(entry_to_utf8(reports_root));
  log_info("data: running offline data generator (Converter only, no game process)");
  int exit_code = 0;
  if (!run_process_utf8(java_path.empty() ? std::string() : entry_to_utf8(java_path),
                        command, exit_code, error)) {
    return false;
  }
  if (exit_code != 0) {
    error = "data generator failed with exit code " + std::to_string(exit_code) +
            " (install JDK 25 or pass --java <java.exe>)";
    return false;
  }
  if (!file::exists(registries)) {
    error = "data generator did not produce reports/reports/registries.json";
    return false;
  }
  if (!file::write_text(stamp_path, server_sha + "\n", error)) return false;
  return true;
}

} // namespace

int run_data(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const std::string profile = args.get("profile").value_or("26.3");
  const auto out_arg = args.get("out");
  if (!out_arg) {
    log_error("data requires --profile 26.3 --out work/26.3 "
              "[--server-jar <server.jar>] [--java <java.exe>]");
    return 2;
  }
  const auto out_dir = file::from_utf8(*out_arg);
  const bool force = args.has("force-reports");
  const bool skip_reports = args.has("skip-reports");
  std::string error;
  if (!file::ensure_directory(out_dir, error)) {
    log_error(error);
    return 3;
  }

  Json input_manifest;
  const auto input_manifest_path = out_dir / "input_manifest.json";
  const bool has_input_manifest =
      file::exists(input_manifest_path) &&
      read_json_file(input_manifest_path, input_manifest, error);

  std::filesystem::path server_jar;
  if (const auto value = args.get("server-jar"); value && !value->empty()) {
    server_jar = file::from_utf8(*value);
  } else if (has_input_manifest) {
    const auto found = input_manifest.find("server_jar");
    if (found != input_manifest.end() && found->is_object()) {
      server_jar = file::from_utf8(found->value("path", ""));
    }
  }
  if (server_jar.empty() || !file::exists(server_jar)) {
    log_error("data: server JAR not found (pass --server-jar <server.jar>)");
    return 4;
  }

  std::filesystem::path java;
  if (const auto value = args.get("java"); value && !value->empty()) {
    java = file::from_utf8(*value);
  }

  // ---- K1a: official reports ------------------------------------------------
  std::string server_sha;
  const auto reports_root = out_dir / "reports";
  if (!skip_reports) {
    if (!run_reports(server_jar, reports_root, java, force, server_sha, error)) {
      log_error("data: " + error);
      return 5;
    }
  } else {
    if (!file::exists(reports_root / "reports" / "registries.json")) {
      log_error("data: --skip-reports given but reports/reports/registries.json is missing");
      return 5;
    }
    server_sha = sha256_of_file(server_jar);
  }
  const auto reports_dir = reports_root / "reports";
  const auto registries_path = reports_dir / "registries.json";
  const auto report_blocks_path = reports_dir / "blocks.json";
  if (!file::exists(registries_path) || !file::exists(report_blocks_path)) {
    log_error("data: reports are incomplete (need registries.json and blocks.json)");
    return 5;
  }

  // ---- K1b: server datapack ------------------------------------------------
  const auto datapack_dir = out_dir / "datapack";
  uint64_t datapack_files = 0;
  uint64_t datapack_json = 0;
  uint64_t datapack_bytes = 0;
  std::string datapack_digest;
  if (!unpack_datapack(server_jar, datapack_dir, datapack_files, datapack_json,
                       datapack_bytes, datapack_digest, error)) {
    log_error("data: " + error);
    return 6;
  }
  Json pack_reports = Json::array();
  if (const auto packs_arg = args.get("datapack"); packs_arg && !packs_arg->empty()) {
    size_t start = 0;
    while (start <= packs_arg->size()) {
      const size_t comma = packs_arg->find(',', start);
      const std::string path = packs_arg->substr(
          start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!path.empty()) {
        Json pack_report;
        if (!copy_data_pack(file::from_utf8(path), datapack_dir, pack_report, error)) {
          log_error("data: " + error);
          return 6;
        }
        pack_reports.push_back(std::move(pack_report));
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    datapack_bytes = 0;
    datapack_digest = digest_directory(datapack_dir, datapack_files, &datapack_bytes, error);
    if (datapack_digest.empty()) {
      log_error("data: " + error);
      return 6;
    }
    datapack_json = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(datapack_dir, ec), end;
         it != end; it.increment(ec)) {
      if (ec) break;
      if (it->is_regular_file() && it->path().extension() == ".json") ++datapack_json;
    }
  }

  // ---- K1c: client assets --------------------------------------------------
  Json assets_report = Json::object();
  if (!has_input_manifest) {
    log_error("data: input_manifest.json is missing; run `reim-convert prepare` first");
    return 7;
  }
  const auto assets_dir = file::from_utf8(input_manifest.value("assets_dir", ""));
  const auto asset_index_path =
      file::from_utf8(input_manifest.value("asset_index", ""));
  if (assets_dir.empty() || !file::exists(asset_index_path)) {
    log_error("data: asset index/assets directory missing from input_manifest.json");
    return 7;
  }
  std::map<std::string, ObjectEntry> objects;
  if (!load_asset_index(asset_index_path, objects, error)) {
    log_error("data: " + error);
    return 7;
  }

  const auto out_assets = out_dir / "assets";
  std::error_code ec;
  std::filesystem::remove_all(out_assets, ec);
  if (!file::ensure_directory(out_assets, error)) {
    log_error(error);
    return 7;
  }

  std::set<std::string> copied;
  std::vector<std::string> asset_warnings;
  Json sounds_json;
  bool sounds_available = false;
  const auto sounds_it = objects.find("minecraft/sounds.json");
  if (sounds_it != objects.end()) {
    const auto source = object_path_for(assets_dir, sounds_it->second.hash);
    file::Bytes bytes;
    if (file::read_all(source, bytes, error)) {
      sounds_json = parse_json_noexcept(
          std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      sounds_available = !sounds_json.is_discarded() && sounds_json.is_object();
    }
    if (!sounds_available) asset_warnings.push_back("sounds.json could not be parsed");
  } else {
    asset_warnings.push_back("client asset index has no minecraft/sounds.json");
  }

  std::set<std::string> sound_files;
  if (sounds_available) {
    std::set<std::string> visiting;
    for (const char* event : kM3SoundEvents) {
      if (sounds_json.contains(event)) {
        collect_sound_files(sounds_json, event, sound_files, visiting);
      } else {
        asset_warnings.push_back(std::string("sound event not in sounds.json: ") + event);
      }
    }
  }

  auto copy_key = [&](const std::string& key, bool required) -> bool {
    const auto it = objects.find(key);
    if (it == objects.end()) {
      if (required) asset_warnings.push_back("missing asset: " + key);
      return false;
    }
    std::string copy_error;
    if (!copy_asset_object(assets_dir, key, it->second, out_assets, copied, copy_error)) {
      asset_warnings.push_back(copy_error);
      return false;
    }
    return true;
  };

  copy_key("minecraft/sounds.json", true);
  const bool lang_from_index = copy_key("minecraft/lang/en_us.json", false);
  for (const std::string& file : sound_files) copy_key(file, false);
  for (const auto& [key, object] : objects) {
    (void)object;
    if (key.rfind("minecraft/font/", 0) == 0 ||
        key.rfind("minecraft/textures/font/", 0) == 0) {
      copy_key(key, false);
    }
  }

  /* Lang and font data are inside client.jar in this profile (they are not
   * asset-index objects). Copy the shipped versions as a fallback so a
   * resource pack can override them at startup. */
  const auto client_jar_path =
      file::from_utf8(input_manifest.value("client_jar", ""));
  if (!client_jar_path.empty() && file::exists(client_jar_path)) {
    zip::Archive client;
    std::string client_error;
    if (client.open(entry_to_utf8(client_jar_path), client_error)) {
      auto copy_client_asset = [&](const std::string& entry) {
        std::string normalized = entry;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        if (!safe_archive_path(normalized, client_error)) {
          asset_warnings.push_back(client_error);
          return;
        }
        if (normalized.rfind("assets/", 0) != 0) return;
        const std::string key = normalized.substr(std::string("assets/").size());
        if (copied.count(key) != 0) return;
        std::vector<uint8_t> bytes;
        if (!client.read(normalized, bytes, client_error)) {
          asset_warnings.push_back(client_error);
          return;
        }
        const auto dest = out_assets / file::from_utf8(key);
        if (!file::ensure_directory(dest.parent_path(), client_error) ||
            !file::write_all(dest, bytes.data(), bytes.size(), client_error)) {
          asset_warnings.push_back(client_error);
          return;
        }
        copied.insert(key);
      };
      if (!lang_from_index) copy_client_asset("assets/minecraft/lang/en_us.json");
      for (const std::string& entry : client.list("assets/minecraft/font/")) {
        copy_client_asset(entry);
      }
      for (const std::string& entry : client.list("assets/minecraft/textures/font/")) {
        copy_client_asset(entry);
      }
    } else {
      asset_warnings.push_back("client JAR could not be opened for lang/font: " +
                               client_error);
    }
  }
  if (copied.count("minecraft/lang/en_us.json") == 0) {
    asset_warnings.push_back("minecraft/lang/en_us.json not found in assets or client JAR");
  }

  Json index_out;
  index_out["schema"] = "reim.asset_index";
  index_out["format"] = 1;
  index_out["profile"] = profile;
  index_out["source"] = "client asset index objects";
  index_out["asset_index_id"] = input_manifest.value("asset_index_id", "");
  index_out["asset_index_sha256"] = input_manifest.value("asset_index_sha256", "");
  index_out["object_count"] = objects.size();
  Json entries = Json::array();
  for (const auto& [key, object] : objects) {
    Json entry;
    entry["path"] = key;
    entry["hash"] = object.hash;
    entry["size"] = object.size;
    entry["kind"] = asset_kind(key);
    entry["local"] = copied.count(key) != 0;
    entries.push_back(std::move(entry));
  }
  index_out["objects"] = std::move(entries);
  if (!write_json_file(out_dir / "assets_index.json", index_out, error)) {
    log_error(error);
    return 8;
  }

  uint64_t assets_bytes = 0;
  uint64_t local_asset_files = 0;
  std::string asset_digest_error;
  const std::string assets_digest_real =
      digest_directory(out_assets, local_asset_files, &assets_bytes, asset_digest_error);
  if (assets_digest_real.empty()) {
    log_error("data: " + asset_digest_error);
    return 8;
  }

  assets_report["available"] = true;
  assets_report["index_id"] = input_manifest.value("asset_index_id", "");
  assets_report["index_sha256"] = input_manifest.value("asset_index_sha256", "");
  assets_report["objects"] = objects.size();
  assets_report["local_files"] = local_asset_files;
  assets_report["local_bytes"] = assets_bytes;
  assets_report["digest"] = assets_digest_real;
  assets_report["sounds_indexed"] = sound_files.size();
  assets_report["copied"] = copied.size();

  Json report;
  report["schema"] = "reim.data_report";
  report["format"] = 1;
  report["profile"] = profile;
  report["server_jar"] = Json{{"sha256", server_sha},
                              {"size", static_cast<uint64_t>(
                                           std::filesystem::file_size(server_jar, ec))}};
  uint64_t report_block_count = 0;
  {
    Json report_blocks;
    std::string report_error;
    if (read_json_file(report_blocks_path, report_blocks, report_error) &&
        report_blocks.is_object()) {
      report_block_count = report_blocks.size();
    }
  }
  report["reports"] = Json{{"path", "reports/reports"},
                           {"registries_sha256", sha256_of_file(registries_path)},
                           {"blocks_sha256", sha256_of_file(report_blocks_path)},
                           {"block_count", report_block_count}};
  report["datapack"] = Json{{"path", "datapack"},
                            {"files", datapack_files},
                            {"json_files", datapack_json},
                            {"bytes", datapack_bytes},
                            {"digest", datapack_digest},
                            {"overlays", std::move(pack_reports)}};
  report["assets"] = assets_report;
  Json warnings = Json::array();
  for (const std::string& warning : asset_warnings) warnings.push_back(warning);
  report["warnings"] = std::move(warnings);
  if (!write_json_file(out_dir / "data_report.json", report, error)) {
    log_error(error);
    return 8;
  }

  log_info(std::format("data: reports OK ({} blocks), datapack {} files/{} json, "
                       "assets {} local/{} indexed, sounds {}",
                       report_block_count, datapack_files, datapack_json,
                       local_asset_files, objects.size(), sound_files.size()));
  log_info(std::format("data: wrote {}", file::to_utf8(out_dir)));
  return 0;
}

} // namespace reim::conv
