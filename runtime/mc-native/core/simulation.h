#pragma once

#include "core/player.h"
#include "data/block_table.h"
#include "game/world_access.h"
#include "mesh/section_mesh.h"
#include "world/world.h"
#include "world/worldgen.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

  /* M3-G time/weather/dimension/worldgen status. */
  uint32_t dimension = 0;       /* ReimDimension */
  uint64_t time_of_day = 0;     /* absolute world ticks */
  uint32_t day = 0;
  uint32_t weather = 0;         /* ReimWeather */
  float sky_darken = 0.0f;
  float sun_angle = 0.0f;
  uint64_t seed = 0;
  uint64_t generated_chunks = 0;
  uint64_t ore_blocks = 0;
  uint64_t trees = 0;
  uint64_t plants = 0;
  uint32_t worldgen_source = 0; /* 0 = native, 1 = generated profile */
  int32_t spawn_surface = 0;
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
  /* M3-G: generate a new in-memory world instead of reading Anvil data. */
  bool create_world = false;
  uint64_t seed = 0;
  /* UTF-8 scenario JSON (time/weather/dimension/setup). Empty = interactive. */
  std::string scenario_json;
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
  /* Inserts an all-air section when the chunk data has no section at that Y
   * and rebuilds the lookup. Returns the section index in chunk.sections. */
  size_t ensure_section(int section_y, const data::BlockTable& blocks);
  void rebuild_lookup();
};

/*
 * Owns the loaded world, the player and the mesh cache. Everything here runs
 * on the Runtime simulation thread; the C ABI layer never touches it from the
 * render thread.
 */
class Simulation : public game::WorldAccess {
 public:
  Simulation();
  ~Simulation() override;
  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;

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
  /* Simulation-thread only: gameplay applies knockback/respawn here. */
  Player& player_mut() { return player_; }
  const SimulationConfig& config() const { return config_; }

  /* Block id at a world position across loaded chunks; 0 (air) outside. */
  uint32_t block_id_at(int world_x, int world_y, int world_z) const override;

  /* WorldAccess: gameplay reads/edits loaded chunks through these. */
  const data::BlockTable& blocks() const override { return blocks_; }
  bool block_state_at(int x, int y, int z,
                      world::BlockState& out) const override;
  bool set_block(int x, int y, int z, const world::BlockState& state,
                 std::string& error) override;
  bool is_loaded(int x, int z) const override;
  bool collides(int x, int y, int z) const override;
  game::EnvironmentState environment() const override;
  std::string biome_at(int x, int y, int z) const override;

  /* M3-G dimension/portal and development controls. */
  bool switch_dimension(int dimension, Player& player, std::string& error);
  bool set_debug_time(uint32_t ticks);
  bool set_debug_weather(uint32_t weather);
  int dimension_id() const { return dimension_id_; }
  bool create_world_mode() const { return create_world_; }
  uint64_t world_seed() const { return seed_; }
  int spawn_surface() const { return spawn_surface_; }
  const std::map<std::string, uint64_t>& ore_counts() const { return ore_counts_; }

  /* v3 Contract-spike asset: one box model definition referencing a real
   * block atlas UV from generated data. The Host caches it; the Runtime owns
   * the definition (M3-D06). */
  std::string contract_model_defs_json() const;

  /* Item id lookup for the item index asset (0 = unknown/empty). */
  uint32_t item_id_for_block(std::string_view name) const;

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

  /* M3-G generation/dimension plumbing. */
  struct GenRequest {
    int cx = 0;
    int cz = 0;
    int dimension = 0;
  };
  struct GenResult {
    int cx = 0;
    int cz = 0;
    int dimension = 0;
    bool ok = false;
    world::Chunk chunk;
    world::GenStats stats;
  };
  struct DimensionCache {
    std::map<int64_t, world::Chunk> chunks;
    bool has_position = false;
    Player position;
  };

  void parse_scenario();
  void start_generation_worker();
  void stop_generation_worker();
  void generation_worker_main();
  void request_generated(int cx, int cz);
  void drain_generated(uint32_t budget);
  bool wait_for_generated(size_t expected, int timeout_ms);
  bool generate_sync(int cx, int cz);
  void apply_generated(GenResult result);
  world::GenDimension gen_dimension_for(int dimension) const;
  const world::DimensionInfo* dimension_info_for(int dimension);
  void clear_active_chunks(bool emit_removes);
  void save_active_dimension(Player* position);
  void update_world_time();

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

  /* M3-G world generation, dimensions and time/weather. */
  bool create_world_ = false;
  uint64_t seed_ = 0;
  int dimension_id_ = 0;
  world::WorldGenProfile gen_profile_;
  world::DimensionInfo synthetic_dimension_;
  std::map<int, DimensionCache> dimension_cache_;
  std::unordered_set<int64_t> pending_generation_;

  std::thread gen_thread_;
  std::mutex gen_mutex_;
  std::condition_variable gen_cv_;
  std::deque<GenRequest> gen_queue_;
  std::deque<GenResult> gen_done_;
  std::atomic<bool> gen_stop_{false};
  bool gen_worker_started_ = false;

  uint64_t world_time_ = 6000;
  uint32_t weather_ = 0;
  uint32_t rain_level_ = 0;
  uint32_t thunder_level_ = 0;
  uint64_t weather_timer_ = 12000;
  bool weather_locked_ = false;
  uint64_t weather_rng_ = 0x123456789ABCDEFull;

  uint64_t generated_chunks_ = 0;
  uint64_t ore_blocks_ = 0;
  uint64_t trees_ = 0;
  uint64_t plants_ = 0;
  std::map<std::string, uint64_t> ore_counts_;
  int spawn_surface_ = 0;
};

} // namespace reim::core
