#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reim::world {

struct SpawnPoint {
  bool present = false;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  std::string dimension = "minecraft:overworld";
};

struct PlayerState {
  bool present = false;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  std::string dimension = "minecraft:overworld";
};

/*
 * Data recovered from level.dat. 26.3 keeps only a small amount of world
 * state here (spawn, version, time); gameplay state moved to
 * data/minecraft/*.dat and player state to players/. Every field is optional
 * and flagged, because a server-generated world has no Player compound.
 */
struct LevelInfo {
  int data_version = 0;
  bool has_version = false;
  std::string version_name;
  int version_id = 0;
  int level_version = 0;  // Data.version (storage format version, e.g. 19133)
  std::vector<int> version_history;
  std::string level_name;
  SpawnPoint spawn;
  PlayerState player;
};

bool load_level(const std::filesystem::path& world_dir, LevelInfo& out,
                std::string& error);

} // namespace reim::world
