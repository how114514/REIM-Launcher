#pragma once

#include "core/player.h"
#include "data/block_table.h"
#include "mesh/section_mesh.h"
#include "world/world.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace reim::core {

struct SimInput {
  int type = 0;    // ReimInputType value
  int key = 0;     // ReimKey value
  int action = 0;  // ReimInputAction value
  float dx = 0.0f;
  float dy = 0.0f;
};

struct BlockSample {
  int x = 0;
  int y = 0;
  int z = 0;
  std::string name;
};

struct SimStatus {
  bool ready = false;
  uint32_t loaded_chunks = 0;
  uint32_t total_chunks = 0;
  uint32_t loading_percent = 0;
  uint64_t tick = 0;
  Player player;
  uint32_t loaded_sections = 0;
  uint32_t mesh_sections = 0;
  uint64_t block_count = 0;
  float measured_tps = 0.0f;
  /* Unobstructed pull-back distance for a third-person camera (0..4 blocks). */
  float third_person_distance = 4.0f;
  std::vector<BlockSample> samples;
};

struct MeshEvent {
  enum class Type { update, remove };
  Type type = Type::update;
  int chunk_x = 0;
  int chunk_z = 0;
  int section_y = 0;
  uint32_t revision = 0;
  uint32_t material = 0;
  std::shared_ptr<mesh::SectionMeshOutput> data;
};

class SimulationSink {
 public:
  virtual ~SimulationSink() = default;
  virtual void publish_status(const SimStatus& status) = 0;
  /* Moves the events out; the caller owns the queued mesh data. */
  virtual void publish_mesh_events(std::vector<MeshEvent>& events) = 0;
};

struct SimulationConfig {
  std::filesystem::path world_path;
  std::filesystem::path generated_path;
  uint32_t render_distance = 6;
  bool spawn_override = false;
  double spawn_x = 0.0;
  double spawn_y = 0.0;
  double spawn_z = 0.0;
  float spawn_yaw = 0.0f;
  float spawn_pitch = 0.0f;
  float mouse_sensitivity = 0.15f;
  bool invert_y = false;
};

struct SectionSlot {
  std::shared_ptr<mesh::SectionMeshOutput> data;
  uint32_t revision = 0;
  bool had_opaque = false;
  bool had_transparent = false;
};

/* One chunk column held by the runtime (not the offline World cache). */
struct LoadedChunk {
  int cx = 0;
  int cz = 0;
  bool present = false;
  reim::world::Chunk chunk;
  int min_section = 0;
  std::vector<int32_t> section_lookup;  // section_y - min_section -> chunk.sections index
  std::vector<std::vector<uint32_t>> section_block_ids;  // palette entry -> BlockTable id
  std::vector<SectionSlot> slots;  // parallel to chunk.sections
  uint64_t non_air_blocks = 0;
  uint32_t non_empty_sections = 0;

  void build(const data::BlockTable& blocks);
  uint32_t block_id_at(int world_x, int world_y, int world_z) const;
};

/*
 * Owns the loaded world, the player and the mesh cache. Everything here runs
 * on the Runtime simulation thread; the C ABI layer never touches it from the
 * render thread.
 */
class Simulation {
 public:
  bool open(const SimulationConfig& config, std::string& error);

  /* Loads the spawn-area chunks and meshes them. Called on the simulation
   * thread before the first tick; reports progress through `sink`. */
  void initial_load(SimulationSink& sink);

  void tick(SimulationSink& sink);
  void apply_input(const SimInput& input);
  SimStatus status() const;

  std::vector<MeshEvent> take_events();

  bool ready() const { return ready_; }
  uint64_t tick_count() const { return tick_; }
  const Player& player() const { return player_; }
  const SimulationConfig& config() const { return config_; }

  /* Block id at a world position across loaded chunks; 0 (air) outside. */
  uint32_t block_id_at(int world_x, int world_y, int world_z) const;

 private:
  static int64_t chunk_key(int cx, int cz);
  static bool collision_query(const void* context, int x, int y, int z);
  LoadedChunk* chunk_at(int cx, int cz);
  const LoadedChunk* chunk_at(int cx, int cz) const;

  bool load_chunk(int cx, int cz, std::string& error);
  void unload_chunk(LoadedChunk& chunk);
  void mark_neighbours_dirty(int cx, int cz);
  void remesh_chunk(LoadedChunk& chunk);
  void update_streaming();
  void remesh_dirty(uint32_t budget);
  void update_tps();
  void emit_status(SimulationSink& sink);
  void emit_events(SimulationSink& sink);
  void build_spawn();
  void resolve_spawn_overlap();

  SimulationConfig config_;
  reim::world::World world_;
  const reim::world::DimensionInfo* dimension_ = nullptr;
  data::BlockTable blocks_;

  Player player_;
  InputState input_;
  std::unordered_map<int64_t, std::unique_ptr<LoadedChunk>> chunks_;
  std::set<int64_t> dirty_;
  std::set<int64_t> known_absent_;
  std::vector<MeshEvent> events_;

  uint64_t tick_ = 0;
  bool ready_ = false;
  uint32_t initial_total_ = 0;
  uint32_t initial_loaded_ = 0;
  int last_center_x_ = 0;
  int last_center_z_ = 0;
  int stream_timer_ = 0;
  uint32_t revision_counter_ = 0;

  uint32_t loaded_sections_ = 0;
  uint32_t mesh_sections_ = 0;
  uint64_t block_count_ = 0;

  uint32_t tps_window_ticks_ = 0;
  std::chrono::steady_clock::time_point tps_window_start_;
  float measured_tps_ = 0.0f;
};

} // namespace reim::core
