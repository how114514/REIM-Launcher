#include "world/world.h"

#include "nbt/nbt.h"
#include "util/file.h"

#include <algorithm>
#include <system_error>

namespace reim::world {
namespace {

std::vector<std::filesystem::path> subdirectories(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  std::filesystem::directory_iterator it(root, ec);
  if (ec) return out;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code status_error;
    if (it->is_directory(status_error) && !status_error) out.push_back(it->path());
  }
  std::sort(out.begin(), out.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b) {
              return a.filename().u8string() < b.filename().u8string();
            });
  return out;
}

std::vector<std::filesystem::path> region_files(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  std::filesystem::directory_iterator it(dir, ec);
  if (ec) return out;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code status_error;
    if (!it->is_regular_file(status_error) || status_error) continue;
    if (it->path().extension() != ".mca") continue;
    int rx = 0;
    int rz = 0;
    if (!anvil::parse_region_coords(it->path(), rx, rz)) continue;
    out.push_back(it->path());
  }
  std::sort(out.begin(), out.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b) {
              int ax = 0;
              int az = 0;
              int bx = 0;
              int bz = 0;
              anvil::parse_region_coords(a, ax, az);
              anvil::parse_region_coords(b, bx, bz);
              return ax == bx ? az < bz : ax < bx;
            });
  return out;
}

std::string chunk_key(const std::string& dimension, int x, int z) {
  return dimension + "|" + std::to_string(x) + "|" + std::to_string(z);
}

int dimension_rank(std::string_view id) {
  if (id == "minecraft:overworld") return 0;
  if (id == "minecraft:the_nether") return 1;
  if (id == "minecraft:the_end") return 2;
  return 3;
}

} // namespace

const char* World::layout_name() const {
  return layout_ == WorldLayout::dimensions_26_3 ? "26.3" : "legacy";
}

bool World::open(const std::filesystem::path& world_dir, std::string& error) {
  *this = World{};
  path_ = world_dir;
  if (!file::is_directory(world_dir)) {
    error = "world directory not found: " + file::to_utf8(world_dir);
    return false;
  }
  if (!load_level(world_dir, level_, error)) return false;

  const std::filesystem::path dimensions_dir = world_dir / "dimensions";
  dimensions_.clear();
  if (file::is_directory(dimensions_dir)) {
    layout_ = WorldLayout::dimensions_26_3;
    for (const std::filesystem::path& namespace_dir : subdirectories(dimensions_dir)) {
      const std::string namespace_name = namespace_dir.filename().string();
      for (const std::filesystem::path& dimension_dir : subdirectories(namespace_dir)) {
        DimensionInfo info;
        info.id = namespace_name + ":" + dimension_dir.filename().string();
        info.root = dimension_dir;
        info.region_dir = dimension_dir / "region";
        info.region_files = region_files(info.region_dir);
        dimensions_.push_back(std::move(info));
      }
    }
    if (dimensions_.empty()) {
      error = "no dimensions found under " + file::to_utf8(dimensions_dir);
      return false;
    }
  } else {
    const std::filesystem::path legacy_region = world_dir / "region";
    if (!file::is_directory(legacy_region)) {
      error = "world has neither dimensions/ nor region/: " +
              file::to_utf8(world_dir) + " (expected Minecraft 26.3 layout)";
      return false;
    }
    layout_ = WorldLayout::legacy;
    DimensionInfo info;
    info.id = "minecraft:overworld";
    info.root = world_dir;
    info.region_dir = legacy_region;
    info.region_files = region_files(legacy_region);
    dimensions_.push_back(std::move(info));
  }

  std::sort(dimensions_.begin(), dimensions_.end(),
            [](const DimensionInfo& a, const DimensionInfo& b) {
              const int ra = dimension_rank(a.id);
              const int rb = dimension_rank(b.id);
              return ra == rb ? a.id < b.id : ra < rb;
            });
  return true;
}

const DimensionInfo* World::dimension(std::string_view id) const {
  for (const DimensionInfo& info : dimensions_) {
    if (info.id == id) return &info;
  }
  return nullptr;
}

std::shared_ptr<anvil::Region> World::region_for(const DimensionInfo& dimension,
                                                 int region_x, int region_z,
                                                 std::string& error) const {
  const std::string key =
      dimension.id + "|" + std::to_string(region_x) + "|" + std::to_string(region_z);
  const auto cached = region_cache_.find(key);
  if (cached != region_cache_.end()) return cached->second;

  const std::filesystem::path path =
      dimension.region_dir / ("r." + std::to_string(region_x) + "." +
                              std::to_string(region_z) + ".mca");
  if (!file::exists(path)) {
    error = "region file not found: " + file::to_utf8(path);
    return nullptr;
  }
  auto region = std::make_shared<anvil::Region>();
  if (!region->open(path, error)) return nullptr;
  region_cache_.emplace(key, region);
  return region;
}

void World::cache_chunk(const std::string& key, std::shared_ptr<Chunk> chunk) const {
  if (chunk_cache_.size() >= 256) chunk_cache_.clear();
  chunk_cache_.insert_or_assign(key, std::move(chunk));
}

ChunkLoad World::load_chunk(const DimensionInfo& dimension, int chunk_x, int chunk_z,
                            Chunk& out, std::string& error) const {
  const std::string key = chunk_key(dimension.id, chunk_x, chunk_z);
  const auto cached = chunk_cache_.find(key);
  if (cached != chunk_cache_.end()) {
    out = *cached->second;
    return ChunkLoad::ok;
  }
  const int region_x = floor_div(chunk_x, 32);
  const int region_z = floor_div(chunk_z, 32);
  std::string region_error;
  std::shared_ptr<anvil::Region> region =
      region_for(dimension, region_x, region_z, region_error);
  if (!region) {
    if (!file::exists(dimension.region_dir /
                      ("r." + std::to_string(region_x) + "." +
                       std::to_string(region_z) + ".mca"))) {
      error.clear();
      return ChunkLoad::not_present;
    }
    error = region_error;
    return ChunkLoad::error;
  }
  const int local_x = chunk_x - region_x * 32;
  const int local_z = chunk_z - region_z * 32;
  if (!region->has_chunk(local_x, local_z)) {
    error.clear();
    return ChunkLoad::not_present;
  }
  std::vector<uint8_t> payload;
  if (!region->read_chunk(local_x, local_z, payload, error)) return ChunkLoad::error;
  nbt::Document document;
  if (!nbt::parse_payload(payload, document, error)) {
    error = "chunk " + std::to_string(chunk_x) + "," + std::to_string(chunk_z) +
            ": " + error;
    return ChunkLoad::error;
  }
  auto chunk = std::make_shared<Chunk>();
  if (!parse_chunk(document, chunk_x, chunk_z, *chunk, error)) return ChunkLoad::error;
  cache_chunk(key, chunk);
  // Copy rather than move: the cached instance must stay intact for later
  // lookups in the same chunk.
  out = *chunk;
  return ChunkLoad::ok;
}

bool World::block_at(const DimensionInfo& dimension, int x, int y, int z,
                     BlockState& out, bool& chunk_present, std::string& error) const {
  const int chunk_x = floor_div(x, 16);
  const int chunk_z = floor_div(z, 16);
  Chunk chunk;
  const ChunkLoad status = load_chunk(dimension, chunk_x, chunk_z, chunk, error);
  if (status == ChunkLoad::not_present) {
    out = air_block();
    chunk_present = false;
    return true;
  }
  if (status == ChunkLoad::error) return false;
  out = chunk.block(x, y, z);
  chunk_present = true;
  return true;
}

} // namespace reim::world
