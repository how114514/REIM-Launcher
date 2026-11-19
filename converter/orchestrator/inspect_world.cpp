#include "inspect_world.h"

#include "anvil/region.h"
#include "nbt/nbt.h"
#include "util/file.h"
#include "world/chunk.h"
#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace reim::conv {
namespace {

struct SampleKey {
  std::string dimension;
  int x = 0;
  int y = 0;
  int z = 0;

  bool operator<(const SampleKey& other) const {
    if (dimension != other.dimension) return dimension < other.dimension;
    if (x != other.x) return x < other.x;
    if (y != other.y) return y < other.y;
    return z < other.z;
  }
};

Json properties_json(const world::BlockState& state) {
  Json properties = Json::object();
  for (const auto& [key, value] : state.properties) properties[key] = value;
  return properties;
}

Json block_json(const world::BlockState& state) {
  Json result{{"block", state.name}};
  if (!state.properties.empty()) result["properties"] = properties_json(state);
  return result;
}

Json level_json(const world::LevelInfo& level) {
  Json version;
  version["name"] = level.has_version && !level.version_name.empty()
                        ? Json(level.version_name)
                        : Json(nullptr);
  version["id"] = level.version_id;
  version["level_version"] = level.level_version;
  version["history"] = level.version_history;

  Json result;
  result["data_version"] = level.data_version;
  result["version"] = std::move(version);
  if (!level.level_name.empty()) result["level_name"] = level.level_name;

  if (level.spawn.present) {
    result["spawn"] = Json{{"x", level.spawn.x},
                           {"y", level.spawn.y},
                           {"z", level.spawn.z},
                           {"yaw", level.spawn.yaw},
                           {"pitch", level.spawn.pitch},
                           {"dimension", level.spawn.dimension}};
  } else {
    result["spawn"] = nullptr;
  }
  if (level.player.present) {
    result["player"] = Json{{"x", level.player.x},
                            {"y", level.player.y},
                            {"z", level.player.z},
                            {"yaw", level.player.yaw},
                            {"pitch", level.player.pitch},
                            {"dimension", level.player.dimension}};
  } else {
    result["player"] = nullptr;
  }
  return result;
}

struct DimensionStats {
  std::uint64_t chunks_present = 0;
  std::uint64_t chunks_loaded = 0;
  std::uint64_t chunks_skipped = 0;
  std::uint64_t non_empty_chunks = 0;
  std::uint64_t sections = 0;
  std::map<std::string, std::uint64_t> status_counts;
};

void add_sample(std::set<SampleKey>& samples, const std::string& dimension, int x, int y,
                int z, const world::Chunk& chunk) {
  if (!chunk.in_bounds(y)) return;
  samples.insert(SampleKey{dimension, x, y, z});
}

} // namespace

int run_inspect_world(const Args& args, const std::filesystem::path& exe_dir) {
  (void)exe_dir;
  const auto world_arg = args.get("world");
  if (!world_arg) {
    log_error("inspect-world requires --world <save> [--out <report.json>]");
    return 2;
  }
  const std::filesystem::path world_path = file::from_utf8(*world_arg);
  const std::filesystem::path out_path =
      args.get("out") ? file::from_utf8(*args.get("out"))
                      : std::filesystem::path("inspect-world.json");

  world::World world;
  std::string error;
  if (!world.open(world_path, error)) {
    log_error("inspect-world: " + error);
    return 3;
  }
  log_info("inspect-world: opened " + file::to_utf8(world_path) + " (" +
           world.layout_name() + " layout, " +
           std::to_string(world.dimensions().size()) + " dimension(s))");

  Json report;
  report["format"] = 1;
  report["tool"] = "reim-convert";
  report["tool_version"] = "1.0.0-m1";
  report["world"] = Json{{"path", file::to_utf8(world.path())},
                         {"name", world.path().filename().string()},
                         {"layout", world.layout_name()}};
  report["level"] = level_json(world.level());

  Json dimensions_json = Json::array();
  Json chunks_json = Json::array();
  Json errors_json = Json::array();

  std::uint64_t total_regions = 0;
  std::uint64_t total_chunks_present = 0;
  std::uint64_t total_chunks_loaded = 0;
  std::uint64_t total_chunks_skipped = 0;
  std::uint64_t total_damaged_regions = 0;
  int observed_min_y = std::numeric_limits<int>::max();
  int observed_max_y = std::numeric_limits<int>::min();

  std::set<SampleKey> samples;
  std::uint64_t sampled_chunks = 0;

  for (const world::DimensionInfo& dimension : world.dimensions()) {
    DimensionStats stats;
    Json regions_json = Json::array();
    log_info("inspect-world: dimension " + dimension.id + " (" +
             std::to_string(dimension.region_files.size()) + " region file(s))");

    for (const std::filesystem::path& region_path : dimension.region_files) {
      ++total_regions;
      anvil::Region region;
      std::string region_error;
      if (!region.open(region_path, region_error)) {
        ++total_damaged_regions;
        errors_json.push_back(Json{{"dimension", dimension.id},
                                   {"file", region_path.filename().string()},
                                   {"kind", "region"},
                                   {"error", region_error}});
        log_warn("inspect-world: " + region_error);
        continue;
      }
      const std::vector<anvil::ChunkLocation> locations = region.present();
      regions_json.push_back(Json{{"file", region_path.filename().string()},
                                  {"chunks", locations.size()}});
      stats.chunks_present += locations.size();
      total_chunks_present += locations.size();

      for (const anvil::ChunkLocation& location : locations) {
        const int chunk_x = region.region_x() * 32 + location.local_x;
        const int chunk_z = region.region_z() * 32 + location.local_z;
        std::vector<uint8_t> payload;
        std::string chunk_error;
        if (!region.read_chunk(location.local_x, location.local_z, payload,
                               chunk_error)) {
          ++stats.chunks_skipped;
          ++total_chunks_skipped;
          errors_json.push_back(Json{{"dimension", dimension.id},
                                     {"file", region_path.filename().string()},
                                     {"kind", "chunk"},
                                     {"chunk", Json::array({chunk_x, chunk_z})},
                                     {"error", chunk_error}});
          log_warn("inspect-world: " + chunk_error);
          continue;
        }
        nbt::Document document;
        if (!nbt::parse_payload(payload, document, chunk_error)) {
          ++stats.chunks_skipped;
          ++total_chunks_skipped;
          errors_json.push_back(Json{{"dimension", dimension.id},
                                     {"file", region_path.filename().string()},
                                     {"kind", "chunk"},
                                     {"chunk", Json::array({chunk_x, chunk_z})},
                                     {"error", chunk_error}});
          log_warn("inspect-world: chunk " + std::to_string(chunk_x) + "," +
                   std::to_string(chunk_z) + ": " + chunk_error);
          continue;
        }
        world::Chunk chunk;
        if (!world::parse_chunk(document, chunk_x, chunk_z, chunk, chunk_error)) {
          ++stats.chunks_skipped;
          ++total_chunks_skipped;
          errors_json.push_back(Json{{"dimension", dimension.id},
                                     {"file", region_path.filename().string()},
                                     {"kind", "chunk"},
                                     {"chunk", Json::array({chunk_x, chunk_z})},
                                     {"error", chunk_error}});
          log_warn("inspect-world: chunk " + std::to_string(chunk_x) + "," +
                   std::to_string(chunk_z) + ": " + chunk_error);
          continue;
        }
        ++stats.chunks_loaded;
        ++total_chunks_loaded;
        stats.sections += chunk.sections.size();
        ++stats.status_counts[chunk.status];

        const std::size_t non_empty = chunk.non_empty_sections();
        if (non_empty > 0) {
          ++stats.non_empty_chunks;
          observed_min_y = std::min(observed_min_y, chunk.min_y);
          observed_max_y = std::max(observed_max_y, chunk.max_y);
        }
        chunks_json.push_back(Json{{"dimension", dimension.id},
                                   {"x", chunk_x},
                                   {"z", chunk_z},
                                   {"status", chunk.status},
                                   {"data_version", chunk.data_version},
                                   {"sections", chunk.sections.size()},
                                   {"non_empty_sections", non_empty},
                                   {"palette_entries", chunk.palette_entries()},
                                   {"light_sections", chunk.light_sections()},
                                   {"min_y", chunk.min_y},
                                   {"max_y", chunk.has_y_pos || !chunk.sections.empty()
                                                 ? Json(chunk.max_y)
                                                 : Json(nullptr)}});

        if (non_empty > 0 && sampled_chunks < 64) {
          ++sampled_chunks;
          const int center_x = chunk_x * 16 + 8;
          const int center_z = chunk_z * 16 + 8;
          std::vector<int> heights = {chunk.min_y + 7, 0, 63, 127, chunk.max_y - 1};
          for (int y : heights) add_sample(samples, dimension.id, center_x, y, center_z,
                                           chunk);
        }
      }
    }

    Json statuses = Json::object();
    for (const auto& [status, count] : stats.status_counts) statuses[status] = count;

    dimensions_json.push_back(Json{
        {"id", dimension.id},
        {"region_dir", file::to_utf8(dimension.region_dir)},
        {"regions", std::move(regions_json)},
        {"chunks_present", stats.chunks_present},
        {"chunks_loaded", stats.chunks_loaded},
        {"chunks_skipped", stats.chunks_skipped},
        {"non_empty_chunks", stats.non_empty_chunks},
        {"sections", stats.sections},
        {"status_counts", std::move(statuses)}});
  }

  // Spawn column grid: 3x3 positions around the spawn, sampled every 8 blocks
  // vertically. Missing chunks read as air on both C++ and oracle sides.
  const world::LevelInfo& level = world.level();
  int min_y = observed_min_y;
  int max_y = observed_max_y;
  if (min_y == std::numeric_limits<int>::max() || min_y > max_y) {
    min_y = -64;
    max_y = 319;
  }
  if (level.spawn.present) {
    const std::string dimension_id = level.spawn.dimension.empty()
                                         ? std::string("minecraft:overworld")
                                         : level.spawn.dimension;
    const int spawn_x = static_cast<int>(std::floor(level.spawn.x));
    const int spawn_z = static_cast<int>(std::floor(level.spawn.z));
    const int offsets[] = {0, -8, 8};
    for (int dx : offsets) {
      for (int dz : offsets) {
        for (int y = min_y; y <= max_y; y += 8) {
          samples.insert(SampleKey{dimension_id, spawn_x + dx, y, spawn_z + dz});
        }
      }
    }
  }

  Json samples_json = Json::array();
  for (const SampleKey& key : samples) {
    const world::DimensionInfo* dimension = world.dimension(key.dimension);
    if (dimension == nullptr) {
      errors_json.push_back(Json{{"dimension", key.dimension},
                                 {"kind", "sample"},
                                 {"chunk", Json::array({world::floor_div(key.x, 16),
                                                        world::floor_div(key.z, 16)})},
                                 {"error", "sample dimension not present in world"}});
      continue;
    }
    world::BlockState state;
    bool chunk_present = false;
    std::string sample_error;
    if (!world.block_at(*dimension, key.x, key.y, key.z, state, chunk_present,
                        sample_error)) {
      errors_json.push_back(Json{{"dimension", key.dimension},
                                 {"kind", "sample"},
                                 {"chunk", Json::array({world::floor_div(key.x, 16),
                                                        world::floor_div(key.z, 16)})},
                                 {"error", sample_error}});
      continue;
    }
    Json sample{{"dimension", key.dimension},
                {"x", key.x},
                {"y", key.y},
                {"z", key.z},
                {"chunk_present", chunk_present},
                {"block", state.name}};
    if (!state.properties.empty()) sample["properties"] = properties_json(state);
    samples_json.push_back(std::move(sample));
  }

  report["dimensions"] = std::move(dimensions_json);
  report["chunks"] = std::move(chunks_json);
  report["samples"] = std::move(samples_json);
  report["errors"] = std::move(errors_json);
  report["summary"] = Json{
      {"dimensions", world.dimensions().size()},
      {"regions", total_regions},
      {"damaged_regions", total_damaged_regions},
      {"chunks_present", total_chunks_present},
      {"chunks_loaded", total_chunks_loaded},
      {"chunks_skipped", total_chunks_skipped},
      {"sample_count", report["samples"].size()},
      {"min_y", observed_min_y != std::numeric_limits<int>::max() ? Json(observed_min_y)
                                                                  : Json(nullptr)},
      {"max_y", observed_max_y != std::numeric_limits<int>::min() ? Json(observed_max_y)
                                                                  : Json(nullptr)}};

  if (!write_json_file(out_path, report, error)) {
    log_error("inspect-world: " + error);
    return 4;
  }
  log_info("inspect-world: wrote " + file::to_utf8(out_path) + " (" +
           std::to_string(total_chunks_loaded) + " chunks loaded, " +
           std::to_string(total_chunks_skipped) + " skipped, " +
           std::to_string(report["samples"].size()) + " samples)");
  return 0;
}

} // namespace reim::conv
