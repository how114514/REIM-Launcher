#pragma once

#include "anvil/region.h"
#include "world/chunk.h"
#include "world/level.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reim::world {

enum class WorldLayout {
  dimensions_26_3,  // dimensions/<namespace>/<dimension>/region/r.x.z.mca
  legacy            // region/r.x.z.mca (pre-26.3 single dimension)
};

struct DimensionInfo {
  std::string id;  // e.g. "minecraft:overworld"
  std::filesystem::path root;
  std::filesystem::path region_dir;
  std::vector<std::filesystem::path> region_files;  // sorted by (x, z)
};

enum class ChunkLoad {
  ok,
  not_present,
  error
};

/*
 * Read-only view of a user-owned world. Opens level.dat and enumerates the
 * region files of every dimension; chunk data is read on demand through a
 * small LRU-ish cache.
 *
 * Threading: the cache is mutable and unsynchronised. M1-C uses the world
 * from one thread; S3 will add the locking/streaming layer it needs.
 */
class World {
 public:
  World() = default;

  bool open(const std::filesystem::path& world_dir, std::string& error);

  const std::filesystem::path& path() const { return path_; }
  WorldLayout layout() const { return layout_; }
  const char* layout_name() const;
  const LevelInfo& level() const { return level_; }
  const std::vector<DimensionInfo>& dimensions() const { return dimensions_; }
  const DimensionInfo* dimension(std::string_view id) const;

  ChunkLoad load_chunk(const DimensionInfo& dimension, int chunk_x, int chunk_z,
                       Chunk& out, std::string& error) const;

  /* Loads the chunk containing (x, z) and returns its block state. Returns
   * false only for damaged data (missing chunk is `chunk_present == false`
   * and air, matching Minecraft's out-of-loaded-world semantics). */
  bool block_at(const DimensionInfo& dimension, int x, int y, int z,
                BlockState& out, bool& chunk_present, std::string& error) const;

 private:
  std::shared_ptr<anvil::Region> region_for(const DimensionInfo& dimension,
                                            int region_x, int region_z,
                                            std::string& error) const;
  void cache_chunk(const std::string& key, std::shared_ptr<Chunk> chunk) const;

  std::filesystem::path path_;
  WorldLayout layout_ = WorldLayout::legacy;
  LevelInfo level_;
  std::vector<DimensionInfo> dimensions_;

  mutable std::map<std::string, std::shared_ptr<anvil::Region>> region_cache_;
  mutable std::map<std::string, std::shared_ptr<Chunk>> chunk_cache_;
};

} // namespace reim::world
