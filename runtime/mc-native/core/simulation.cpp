#include "core/simulation.h"

#include "reim_abi.h"
#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

#include "json.hpp"

namespace reim::core {
namespace {

constexpr uint32_t kBlocksPerSection = 4096;
constexpr uint32_t kSectionAxis = 16;

struct MeshQueryContext {
  const LoadedChunk* neighbours[9] = {};
  int cx = 0;
  int cz = 0;
};

uint32_t query_mesh_block(const void* raw, int x, int y, int z) {
  const auto* context = static_cast<const MeshQueryContext*>(raw);
  const int dcx = world::floor_div(x, kSectionAxis) - context->cx;
  const int dcz = world::floor_div(z, kSectionAxis) - context->cz;
  if (dcx < -1 || dcx > 1 || dcz < -1 || dcz > 1) return 0;
  const LoadedChunk* chunk = context->neighbours[(dcz + 1) * 3 + (dcx + 1)];
  if (chunk == nullptr) return 0;
  return chunk->block_id_at(x, y, z);
}

uint32_t query_world_block(const void* raw, int x, int y, int z) {
  return static_cast<const Simulation*>(raw)->block_id_at(x, y, z);
}

int distance_squared(int dx, int dz) { return dx * dx + dz * dz; }

const char* kDimensionIds[] = {"minecraft:overworld", "minecraft:the_nether",
                               "minecraft:the_end"};

int dimension_from_name(std::string_view name) {
  if (name == "nether" || name == "minecraft:the_nether") return 1;
  if (name == "end" || name == "minecraft:the_end") return 2;
  return 0;
}

uint64_t xorshift64(uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

} // namespace

Simulation::Simulation() = default;

Simulation::~Simulation() { stop_generation_worker(); }

void LoadedChunk::build(const data::BlockTable& blocks) {
  const int max_section = world::floor_div(chunk.max_y, kSectionAxis);
  min_section = world::floor_div(chunk.min_y, kSectionAxis);
  const int section_span = max_section >= min_section ? max_section - min_section + 1 : 0;
  section_lookup.assign(static_cast<size_t>(section_span), -1);
  section_block_ids.assign(chunk.sections.size(), {});
  slots.assign(chunk.sections.size(), SectionSlot{});
  non_air_blocks = 0;
  non_empty_sections = 0;

  for (size_t i = 0; i < chunk.sections.size(); ++i) {
    const world::Section& section = chunk.sections[i];
    const int local = section.y - min_section;
    if (local >= 0 && static_cast<size_t>(local) < section_lookup.size()) {
      section_lookup[static_cast<size_t>(local)] = static_cast<int32_t>(i);
    }
    std::vector<uint32_t>& ids = section_block_ids[i];
    ids.reserve(section.palette.size());
    for (const world::BlockState& state : section.palette) {
      ids.push_back(blocks.find_state(state.name, state.properties));
    }
    if (!section.non_empty()) continue;
    ++non_empty_sections;
    if (section.indices.empty()) {
      if (!ids.empty() && ids.front() != 0) non_air_blocks += kBlocksPerSection;
    } else {
      for (uint32_t palette_index : section.indices) {
        if (palette_index < ids.size() && ids[palette_index] != 0) ++non_air_blocks;
      }
    }
  }
}

uint32_t LoadedChunk::block_id_at(int world_x, int world_y, int world_z) const {
  const int section_y = world::floor_div(world_y, kSectionAxis);
  if (section_y < min_section) return 0;
  const size_t offset = static_cast<size_t>(section_y - min_section);
  if (offset >= section_lookup.size()) return 0;
  const int32_t section_index = section_lookup[offset];
  if (section_index < 0) return 0;
  const size_t index = static_cast<size_t>(section_index);
  if (index >= chunk.sections.size() || index >= section_block_ids.size()) return 0;
  const world::Section& section = chunk.sections[index];
  const std::vector<uint32_t>& ids = section_block_ids[index];
  if (ids.empty()) return 0;
  const int local_x = world_x - cx * 16;
  const int local_z = world_z - cz * 16;
  const int local_y = world_y - section_y * 16;
  if (local_x < 0 || local_x > 15 || local_z < 0 || local_z > 15 ||
      local_y < 0 || local_y > 15) {
    return 0;
  }
  if (section.indices.empty()) return ids.front();
  const size_t block_index = (static_cast<size_t>(local_y) << 8) |
                             (static_cast<size_t>(local_z) << 4) |
                             static_cast<size_t>(local_x);
  if (block_index >= section.indices.size()) return 0;
  const uint32_t palette_index = section.indices[block_index];
  return palette_index < ids.size() ? ids[palette_index] : 0;
}

void LoadedChunk::rebuild_lookup() {
  section_lookup.clear();
  if (chunk.sections.empty()) {
    min_section = world::floor_div(chunk.min_y, kSectionAxis);
    return;
  }
  min_section = chunk.sections.front().y;
  const int max_section = chunk.sections.back().y;
  const int span = max_section >= min_section ? max_section - min_section + 1 : 0;
  section_lookup.assign(static_cast<size_t>(span), -1);
  for (size_t i = 0; i < chunk.sections.size(); ++i) {
    const int local = chunk.sections[i].y - min_section;
    if (local >= 0 && static_cast<size_t>(local) < section_lookup.size()) {
      section_lookup[static_cast<size_t>(local)] = static_cast<int32_t>(i);
    }
  }
}

size_t LoadedChunk::ensure_section(int section_y, const data::BlockTable& blocks) {
  (void)blocks;
  auto it = std::lower_bound(
      chunk.sections.begin(), chunk.sections.end(), section_y,
      [](const world::Section& section, int value) { return section.y < value; });
  if (it != chunk.sections.end() && it->y == section_y) {
    return static_cast<size_t>(it - chunk.sections.begin());
  }
  world::Section section;
  section.y = section_y;
  section.palette.push_back(world::air_block());
  const size_t index = static_cast<size_t>(chunk.sections.insert(it, std::move(section)) -
                                           chunk.sections.begin());
  section_block_ids.insert(section_block_ids.begin() + static_cast<ptrdiff_t>(index),
                           std::vector<uint32_t>{0});
  slots.insert(slots.begin() + static_cast<ptrdiff_t>(index), SectionSlot{});
  const int block_y = section_y * kSectionAxis;
  if (chunk.sections.size() == 1 && chunk.max_y < chunk.min_y) {
    chunk.min_y = block_y;
    chunk.max_y = block_y + 15;
  } else {
    chunk.min_y = std::min(chunk.min_y, block_y);
    chunk.max_y = std::max(chunk.max_y, block_y + 15);
  }
  rebuild_lookup();
  return index;
}

bool Simulation::open(const SimulationConfig& config, std::string& error) {
  config_ = config;
  create_world_ = config_.create_world;
  seed_ = config_.seed;
  if (!blocks_.load(config_.generated_path, error)) {
    error = "generated block data: " + error;
    return false;
  }
  if (create_world_) {
    if (!gen_profile_.load(config_.generated_path, error)) {
      error = "worldgen profile: " + error;
      return false;
    }
    if (seed_ == 0) seed_ = 0x5EED5EED12345678ull;
    gen_profile_.seed = seed_;
    synthetic_dimension_.id = kDimensionIds[0];
    synthetic_dimension_.root = config_.world_path;
    synthetic_dimension_.region_dir = config_.world_path / "region";
    dimension_ = &synthetic_dimension_;
    dimension_id_ = 0;
    parse_scenario();
    dimension_id_ = std::clamp(dimension_id_, 0, 2);
    synthetic_dimension_.id = kDimensionIds[dimension_id_];
    build_spawn();
    start_generation_worker();
    reim::log::info("runtime",
                    "creating new world seed=" + std::to_string(seed_) +
                        " dimension=" + synthetic_dimension_.id +
                        " worldgen=" + gen_profile_.source);
    return true;
  }
  if (!world_.open(config_.world_path, error)) return false;
  dimension_ = world_.dimension("minecraft:overworld");
  if (dimension_ == nullptr && !world_.dimensions().empty()) {
    dimension_ = &world_.dimensions().front();
    reim::log::warn("runtime", "overworld not found; using " + dimension_->id);
  }
  if (dimension_ == nullptr) {
    error = "world has no dimensions";
    return false;
  }
  dimension_id_ = 0;
  parse_scenario();
  if (dimension_id_ != 0) {
    const world::DimensionInfo* target =
        dimension_info_for(dimension_id_);
    if (target == nullptr) {
      error = std::string("scenario dimension is not present: ") +
              kDimensionIds[dimension_id_];
      return false;
    }
    dimension_ = target;
  }
  build_spawn();
  reim::log::info("runtime",
                  "simulation opened " + file::to_utf8(config_.world_path) + " (" +
                      dimension_->id + ", " + std::to_string(blocks_.size()) +
                      " block definitions)");
  return true;
}

void Simulation::parse_scenario() {
  if (config_.scenario_json.empty()) return;
  const auto root = nlohmann::json::parse(
      config_.scenario_json.begin(), config_.scenario_json.end(), nullptr, false);
  if (root.is_discarded() || !root.is_object()) return;
  if (root.contains("time") && root["time"].is_number()) {
    world_time_ = static_cast<uint64_t>(
        std::max<int64_t>(0, root["time"].get<int64_t>()));
  }
  if (root.contains("weather")) {
    if (root["weather"].is_string()) {
      const std::string value = root["weather"].get<std::string>();
      weather_ = value == "rain" ? 1u : (value == "thunder" ? 2u : 0u);
    } else if (root["weather"].is_number_integer()) {
      weather_ = static_cast<uint32_t>(
          std::clamp(root["weather"].get<int>(), 0, 2));
    }
    weather_locked_ = true;
    rain_level_ = weather_ > 0 ? 1000u : 0u;
    thunder_level_ = weather_ == 2 ? 1000u : 0u;
  }
  if (root.contains("dimension") && root["dimension"].is_string()) {
    dimension_id_ = dimension_from_name(root["dimension"].get<std::string>());
  }
  if (root.contains("seed") && root["seed"].is_number_unsigned() &&
      config_.seed == 0) {
    seed_ = root["seed"].get<uint64_t>();
    gen_profile_.seed = seed_;
  }
}

void Simulation::build_spawn() {
  player_ = Player{};
  if (config_.spawn_override) {
    player_.x = config_.spawn_x;
    player_.y = config_.spawn_y;
    player_.z = config_.spawn_z;
    player_.yaw = config_.spawn_yaw;
    player_.pitch = std::clamp(config_.spawn_pitch, -90.0f, 90.0f);
    spawn_surface_ = static_cast<int>(std::floor(player_.y)) - 1;
    return;
  }
  if (create_world_) {
    world::WorldGenerator generator;
    std::string gen_error;
    if (generator.initialize(gen_profile_, gen_dimension_for(dimension_id_),
                             gen_error)) {
      int x = 0;
      int y = 64;
      int z = 0;
      generator.find_spawn(0, 0, x, y, z);
      player_.x = x + 0.5;
      player_.y = y;
      player_.z = z + 0.5;
      spawn_surface_ = y - 1;
      if (dimension_id_ == 1) player_.yaw = 270.0f;
      return;
    }
  }
  const world::LevelInfo& level = world_.level();
  const bool spawn_in_dimension =
      level.spawn.present &&
      (level.spawn.dimension.empty() || level.spawn.dimension == dimension_->id);
  if (spawn_in_dimension) {
    player_.x = level.spawn.x;
    player_.y = level.spawn.y;
    player_.z = level.spawn.z;
    player_.yaw = level.spawn.yaw;
    player_.pitch = level.spawn.pitch;
    spawn_surface_ = static_cast<int>(std::floor(player_.y)) - 1;
    return;
  }
  if (level.player.present) {
    player_.x = level.player.x;
    player_.y = level.player.y;
    player_.z = level.player.z;
    player_.yaw = level.player.yaw;
    player_.pitch = level.player.pitch;
    spawn_surface_ = static_cast<int>(std::floor(player_.y)) - 1;
    return;
  }
  player_.x = 0.5;
  player_.y = 96.0;
  player_.z = 0.5;
  spawn_surface_ = 95;
}

world::GenDimension Simulation::gen_dimension_for(int dimension) const {
  switch (dimension) {
    case 1: return world::GenDimension::nether;
    case 2: return world::GenDimension::end;
    default: return world::GenDimension::overworld;
  }
}

const world::DimensionInfo* Simulation::dimension_info_for(int dimension) {
  if (create_world_) {
    synthetic_dimension_.id = kDimensionIds[std::clamp(dimension, 0, 2)];
    return &synthetic_dimension_;
  }
  return world_.dimension(kDimensionIds[std::clamp(dimension, 0, 2)]);
}

game::EnvironmentState Simulation::environment() const {
  game::EnvironmentState environment;
  environment.time_of_day = world_time_;
  environment.day = static_cast<uint32_t>(world_time_ / 24000u);
  environment.weather = weather_;
  environment.dimension = static_cast<uint32_t>(dimension_id_);
  if (dimension_id_ != 0) {
    environment.daylight = false;
    environment.sky_darken = 1.0f;
    return environment;
  }
  const uint64_t phase = world_time_ % 24000u;
  environment.daylight = phase < 12500u || phase >= 23200u;
  float darken = 0.0f;
  if (phase >= 12000u && phase < 13000u) {
    darken = static_cast<float>(phase - 12000u) / 1000.0f * 0.8f;
  } else if (phase >= 13000u && phase < 23000u) {
    darken = 1.0f;
  } else if (phase >= 23000u) {
    darken = static_cast<float>(24000u - phase) / 1000.0f * 0.8f;
  }
  if (weather_ == 1) darken = std::min(1.0f, darken + 0.15f);
  if (weather_ == 2) darken = std::min(1.0f, darken + 0.25f);
  environment.sky_darken = darken;
  return environment;
}

std::string Simulation::biome_at(int x, int y, int z) const {
  const LoadedChunk* chunk =
      chunk_at(world::floor_div(x, 16), world::floor_div(z, 16));
  if (chunk != nullptr && chunk->present) {
    const std::string biome = chunk->chunk.biome(x, y, z);
    if (!biome.empty()) return biome;
  }
  if (create_world_) {
    world::WorldGenerator generator;
    std::string error;
    if (generator.initialize(gen_profile_, gen_dimension_for(dimension_id_),
                             error)) {
      return generator.biome_at(x, z);
    }
  }
  return {};
}

bool Simulation::set_debug_time(uint32_t ticks) {
  if (ticks > 24000u * 1000u) return false;
  world_time_ = ticks;
  return true;
}

bool Simulation::set_debug_weather(uint32_t weather) {
  if (weather > 2u) return false;
  weather_ = weather;
  weather_locked_ = true;
  rain_level_ = weather > 0 ? 1000u : 0u;
  thunder_level_ = weather == 2 ? 1000u : 0u;
  return true;
}

void Simulation::update_world_time() {
  ++world_time_;
  if (weather_locked_) return;
  if (weather_timer_ > 0) {
    --weather_timer_;
    return;
  }
  const uint64_t roll = xorshift64(weather_rng_) % 100u;
  weather_ = roll < 70u ? 0u : (roll < 90u ? 1u : 2u);
  weather_timer_ = 6000u + xorshift64(weather_rng_) % 12000u;
  const uint32_t rain_target = weather_ > 0 ? 1000u : 0u;
  const uint32_t thunder_target = weather_ == 2 ? 1000u : 0u;
  if (rain_level_ < rain_target) rain_level_ = std::min(rain_target, rain_level_ + 20u);
  else if (rain_level_ > rain_target) rain_level_ -= std::min(rain_level_ - rain_target, 20u);
  if (thunder_level_ < thunder_target) {
    thunder_level_ = std::min(thunder_target, thunder_level_ + 20u);
  } else if (thunder_level_ > thunder_target) {
    thunder_level_ -= std::min(thunder_level_ - thunder_target, 20u);
  }
}

void Simulation::start_generation_worker() {
  if (gen_worker_started_) return;
  gen_stop_.store(false);
  gen_worker_started_ = true;
  gen_thread_ = std::thread([this]() { generation_worker_main(); });
}

void Simulation::stop_generation_worker() {
  if (!gen_worker_started_) return;
  gen_stop_.store(true);
  gen_cv_.notify_all();
  if (gen_thread_.joinable()) gen_thread_.join();
  gen_worker_started_ = false;
}

void Simulation::generation_worker_main() {
  while (true) {
    GenRequest request;
    {
      std::unique_lock<std::mutex> lock(gen_mutex_);
      gen_cv_.wait(lock, [this]() {
        return gen_stop_.load() || !gen_queue_.empty();
      });
      if (gen_stop_.load() && gen_queue_.empty()) break;
      request = gen_queue_.front();
      gen_queue_.pop_front();
    }
    GenResult result;
    result.cx = request.cx;
    result.cz = request.cz;
    result.dimension = request.dimension;
    world::WorldGenerator generator;
    std::string error;
    if (generator.initialize(gen_profile_,
                             gen_dimension_for(request.dimension), error)) {
      generator.generate_chunk(request.cx, request.cz, result.chunk, result.stats);
      result.ok = true;
    } else {
      reim::log::warn("worldgen", "generation failed: " + error);
    }
    {
      std::lock_guard<std::mutex> lock(gen_mutex_);
      gen_done_.push_back(std::move(result));
    }
    gen_cv_.notify_all();
  }
}

void Simulation::request_generated(int cx, int cz) {
  const int64_t key = chunk_key(cx, cz);
  if (chunks_.find(key) != chunks_.end()) return;
  if (!pending_generation_.insert(key).second) return;
  GenRequest request;
  request.cx = cx;
  request.cz = cz;
  request.dimension = dimension_id_;
  {
    std::lock_guard<std::mutex> lock(gen_mutex_);
    gen_queue_.push_back(request);
  }
  gen_cv_.notify_all();
}

void Simulation::apply_generated(GenResult result) {
  const int64_t key = chunk_key(result.cx, result.cz);
  pending_generation_.erase(key);
  if (result.dimension != dimension_id_ || !result.ok) return;
  if (chunks_.find(key) != chunks_.end()) return;
  auto loaded = std::make_unique<LoadedChunk>();
  loaded->cx = result.cx;
  loaded->cz = result.cz;
  loaded->present = true;
  loaded->chunk = std::move(result.chunk);
  loaded->build(blocks_);
  loaded_sections_ += loaded->non_empty_sections;
  block_count_ += loaded->non_air_blocks;
  mark_neighbours_dirty(loaded->cx, loaded->cz);
  chunks_.emplace(key, std::move(loaded));
  generated_chunks_ += result.stats.chunks;
  ore_blocks_ += result.stats.ore_blocks;
  trees_ += result.stats.trees;
  plants_ += result.stats.plants;
  for (const auto& [name, count] : result.stats.ores) {
    ore_counts_[name] += count;
  }
}

void Simulation::drain_generated(uint32_t budget) {
  std::deque<GenResult> results;
  {
    std::lock_guard<std::mutex> lock(gen_mutex_);
    const size_t count = std::min<size_t>(budget, gen_done_.size());
    for (size_t i = 0; i < count; ++i) {
      results.push_back(std::move(gen_done_.front()));
      gen_done_.pop_front();
    }
  }
  std::sort(results.begin(), results.end(), [](const GenResult& a, const GenResult& b) {
    if (a.cx != b.cx) return a.cx < b.cx;
    return a.cz < b.cz;
  });
  for (GenResult& result : results) apply_generated(std::move(result));
}

bool Simulation::wait_for_generated(size_t expected, int timeout_ms) {
  (void)expected;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    drain_generated(64);
    bool pending = false;
    {
      std::lock_guard<std::mutex> lock(gen_mutex_);
      pending = !gen_queue_.empty() || !gen_done_.empty();
    }
    if (!pending && pending_generation_.empty()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  drain_generated(4096);
  return pending_generation_.empty();
}

bool Simulation::generate_sync(int cx, int cz) {
  world::WorldGenerator generator;
  std::string error;
  if (!generator.initialize(gen_profile_, gen_dimension_for(dimension_id_),
                            error)) {
    reim::log::warn("worldgen", "generation failed: " + error);
    return false;
  }
  GenResult result;
  result.cx = cx;
  result.cz = cz;
  result.dimension = dimension_id_;
  generator.generate_chunk(cx, cz, result.chunk, result.stats);
  result.ok = true;
  apply_generated(std::move(result));
  return true;
}

void Simulation::save_active_dimension(Player* position) {
  DimensionCache& cache = dimension_cache_[dimension_id_];
  for (const auto& [key, chunk] : chunks_) {
    if (chunk->present) cache.chunks[key] = chunk->chunk;
  }
  if (position != nullptr) {
    cache.position = *position;
    cache.has_position = true;
  }
}

void Simulation::clear_active_chunks(bool emit_removes) {
  for (auto& [key, chunk] : chunks_) {
    (void)key;
    if (emit_removes) {
      for (SectionSlot& slot : chunk->slots) {
        if (!slot.data || slot.data->empty()) continue;
        MeshEvent event;
        event.type = MeshEvent::Type::remove;
        event.chunk_x = chunk->cx;
        event.chunk_z = chunk->cz;
        event.section_y = slot.data->section_y;
        event.data = slot.data;
        events_.push_back(std::move(event));
      }
    }
  }
  chunks_.clear();
  dirty_.clear();
  known_absent_.clear();
  loaded_sections_ = 0;
  mesh_sections_ = 0;
  block_count_ = 0;
  initial_total_ = 0;
  initial_loaded_ = 0;
}

bool Simulation::switch_dimension(int dimension, Player& player,
                                  std::string& error) {
  if (dimension == dimension_id_) return true;
  if (dimension < 0 || dimension > 2) {
    error = "dimension id out of range";
    return false;
  }
  if (create_world_ && dimension == 2) {
    error = "end generation is not part of M3 (load-only)";
    return false;
  }
  const world::DimensionInfo* info = dimension_info_for(dimension);
  if (info == nullptr) {
    error = std::string("dimension is not present: ") +
            kDimensionIds[dimension];
    return false;
  }

  save_active_dimension(&player);
  clear_active_chunks(true);
  {
    std::lock_guard<std::mutex> lock(gen_mutex_);
    gen_queue_.clear();
    gen_done_.clear();
  }
  pending_generation_.clear();
  dimension_id_ = dimension;
  dimension_ = info;
  if (create_world_) synthetic_dimension_.id = kDimensionIds[dimension];

  DimensionCache& cache = dimension_cache_[dimension];
  for (const auto& [key, chunk] : cache.chunks) {
    auto loaded = std::make_unique<LoadedChunk>();
    loaded->cx = chunk.x;
    loaded->cz = chunk.z;
    loaded->present = true;
    loaded->chunk = chunk;
    loaded->build(blocks_);
    loaded_sections_ += loaded->non_empty_sections;
    block_count_ += loaded->non_air_blocks;
    chunks_.emplace(key, std::move(loaded));
  }

  Player target;
  if (cache.has_position) {
    target = cache.position;
  } else {
    target = player;
    target.vx = target.vy = target.vz = 0.0;
    if (dimension == 1 && create_world_) {
      world::WorldGenerator generator;
      std::string gen_error;
      if (!generator.initialize(gen_profile_, world::GenDimension::nether,
                                gen_error)) {
        error = gen_error;
        return false;
      }
      int x = static_cast<int>(std::floor(player.x));
      int y = 48;
      int z = static_cast<int>(std::floor(player.z));
      generator.find_spawn(x, z, x, y, z);
      target.x = x + 0.5;
      target.y = y;
      target.z = z + 0.5;
      target.yaw = 270.0f;
      target.pitch = 0.0f;
    } else if (dimension == 0 && create_world_) {
      world::WorldGenerator generator;
      std::string gen_error;
      if (!generator.initialize(gen_profile_, world::GenDimension::overworld,
                                gen_error)) {
        error = gen_error;
        return false;
      }
      int x = static_cast<int>(std::floor(player.x));
      int y = 64;
      int z = static_cast<int>(std::floor(player.z));
      generator.find_spawn(x, z, x, y, z);
      target.x = x + 0.5;
      target.y = y;
      target.z = z + 0.5;
    } else {
      target.y = 64.0;
    }
    cache.position = target;
    cache.has_position = true;
  }

  const uint32_t radius =
      std::clamp<uint32_t>(config_.render_distance, 1, 16);
  const int center_x =
      world::floor_div(static_cast<int>(std::floor(target.x)), 16);
  const int center_z =
      world::floor_div(static_cast<int>(std::floor(target.z)), 16);
  for (int dz = -static_cast<int>(radius); dz <= static_cast<int>(radius); ++dz) {
    for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius);
         ++dx) {
      const int cx = center_x + dx;
      const int cz = center_z + dz;
      if (chunks_.find(chunk_key(cx, cz)) != chunks_.end()) continue;
      if (create_world_) {
        generate_sync(cx, cz);
      } else {
        std::string load_error;
        load_chunk(cx, cz, load_error);
      }
    }
  }
  last_center_x_ = center_x;
  last_center_z_ = center_z;

  if (dimension == 1 && create_world_) {
    /* Minimal portal arrival: a netherrack shelf and a return portal two blocks
     * east of the player, so USE_BLOCK without an explicit target can return. */
    const int fx = static_cast<int>(std::floor(target.x));
    const int fy = static_cast<int>(std::floor(target.y));
    const int fz = static_cast<int>(std::floor(target.z));
    world::BlockState netherrack;
    netherrack.name = "minecraft:netherrack";
    world::BlockState air;
    air.name = "minecraft:air";
    world::BlockState portal;
    portal.name = "minecraft:nether_portal";
    std::string set_error;
    for (int dx = -2; dx <= 3; ++dx) {
      for (int dz = -2; dz <= 2; ++dz) {
        set_block(fx + dx, fy - 1, fz + dz, netherrack, set_error);
        set_block(fx + dx, fy, fz + dz, air, set_error);
        set_block(fx + dx, fy + 1, fz + dz, air, set_error);
      }
    }
    set_block(fx + 2, fy, fz, portal, set_error);
    set_block(fx + 2, fy + 1, fz, portal, set_error);
  }

  player = target;
  for (auto& [key, chunk] : chunks_) {
    (void)key;
    if (chunk->present) remesh_chunk(*chunk);
  }
  dirty_.clear();
  initial_total_ = static_cast<uint32_t>(chunks_.size());
  initial_loaded_ = initial_total_;
  ready_ = true;
  return true;
}

void Simulation::resolve_spawn_overlap() {
  auto collides = [this]() {
    const int x0 = static_cast<int>(std::floor(player_.x - kPlayerHalfWidth));
    const int x1 = static_cast<int>(std::floor(player_.x + kPlayerHalfWidth));
    const int y0 = static_cast<int>(std::floor(player_.y));
    const int y1 = static_cast<int>(std::floor(player_.y + kPlayerHeight));
    const int z0 = static_cast<int>(std::floor(player_.z - kPlayerHalfWidth));
    const int z1 = static_cast<int>(std::floor(player_.z + kPlayerHalfWidth));
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) {
          if (blocks_.def(block_id_at(x, y, z)).collision) return true;
        }
      }
    }
    return false;
  };
  for (int step = 0; step < 128 && collides(); ++step) {
    player_.y += 1.0;
  }
}

int64_t Simulation::chunk_key(int cx, int cz) {
  return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
                              static_cast<uint32_t>(cz));
}

LoadedChunk* Simulation::chunk_at(int cx, int cz) {
  const auto it = chunks_.find(chunk_key(cx, cz));
  return it == chunks_.end() ? nullptr : it->second.get();
}

const LoadedChunk* Simulation::chunk_at(int cx, int cz) const {
  const auto it = chunks_.find(chunk_key(cx, cz));
  return it == chunks_.end() ? nullptr : it->second.get();
}

uint32_t Simulation::block_id_at(int world_x, int world_y, int world_z) const {
  const int cx = world::floor_div(world_x, 16);
  const int cz = world::floor_div(world_z, 16);
  const LoadedChunk* chunk = chunk_at(cx, cz);
  if (chunk == nullptr) return 0;
  return chunk->block_id_at(world_x, world_y, world_z);
}

bool Simulation::block_state_at(int x, int y, int z,
                                world::BlockState& out) const {
  const int cx = world::floor_div(x, 16);
  const int cz = world::floor_div(z, 16);
  const LoadedChunk* chunk = chunk_at(cx, cz);
  if (chunk == nullptr || !chunk->present) return false;
  out = chunk->chunk.block(x, y, z);
  return true;
}

bool Simulation::is_loaded(int x, int z) const {
  const LoadedChunk* chunk = chunk_at(world::floor_div(x, 16), world::floor_div(z, 16));
  return chunk != nullptr && chunk->present;
}

bool Simulation::collides(int x, int y, int z) const {
  return blocks_.def(block_id_at(x, y, z)).collision;
}

bool Simulation::set_block(int x, int y, int z, const world::BlockState& state,
                           std::string& error) {
  if (y < -512 || y > 1024) {
    error = "block y outside the supported world height";
    return false;
  }
  const int cx = world::floor_div(x, 16);
  const int cz = world::floor_div(z, 16);
  LoadedChunk* chunk = chunk_at(cx, cz);
  if (chunk == nullptr || !chunk->present) {
    error = "chunk is not loaded";
    return false;
  }
  const int section_y = world::floor_div(y, 16);
  const size_t index = chunk->ensure_section(section_y, blocks_);
  if (index >= chunk->chunk.sections.size() ||
      index >= chunk->section_block_ids.size() || index >= chunk->slots.size()) {
    error = "section index mismatch";
    return false;
  }
  world::Section& section = chunk->chunk.sections[index];
  std::vector<uint32_t>& ids = chunk->section_block_ids[index];
  const bool was_non_empty = section.non_empty();
  const uint32_t old_id = chunk->block_id_at(x, y, z);

  size_t palette_index = section.palette.size();
  for (size_t i = 0; i < section.palette.size(); ++i) {
    if (section.palette[i] == state) {
      palette_index = i;
      break;
    }
  }
  if (palette_index == section.palette.size()) {
    section.palette.push_back(state);
    ids.resize(section.palette.size(), 0);
    ids.back() = blocks_.find_state(state.name, state.properties);
  } else if (ids.size() < section.palette.size()) {
    ids.resize(section.palette.size(), 0);
  }
  if (section.indices.size() != kBlocksPerSection) {
    section.indices.assign(kBlocksPerSection, 0);
  }
  const int local_x = x - cx * 16;
  const int local_z = z - cz * 16;
  const int local_y = y - section_y * 16;
  const size_t block_index = (static_cast<size_t>(local_y) << 8) |
                             (static_cast<size_t>(local_z) << 4) |
                             static_cast<size_t>(local_x);
  section.indices[block_index] = static_cast<uint32_t>(palette_index);
  const uint32_t new_id = ids[palette_index];

  if (old_id == 0 && new_id != 0) {
    ++block_count_;
    ++chunk->non_air_blocks;
  } else if (old_id != 0 && new_id == 0) {
    if (block_count_ > 0) --block_count_;
    if (chunk->non_air_blocks > 0) --chunk->non_air_blocks;
  }
  const bool is_non_empty = section.non_empty();
  if (was_non_empty != is_non_empty) {
    if (is_non_empty) {
      ++chunk->non_empty_sections;
      ++loaded_sections_;
    } else {
      if (chunk->non_empty_sections > 0) --chunk->non_empty_sections;
      if (loaded_sections_ > 0) --loaded_sections_;
    }
  }
  dirty_.insert(chunk_key(cx, cz));
  mark_neighbours_dirty(cx, cz);
  return true;
}

bool Simulation::collision_query(const void* context, int x, int y, int z) {
  const auto* simulation = static_cast<const Simulation*>(context);
  return simulation->blocks_.def(simulation->block_id_at(x, y, z)).collision;
}

bool Simulation::load_chunk(int cx, int cz, std::string& error) {
  const int64_t key = chunk_key(cx, cz);
  if (chunks_.find(key) != chunks_.end()) return true;

  world::Chunk chunk;
  const world::ChunkLoad status = world_.load_chunk(*dimension_, cx, cz, chunk, error);
  auto loaded = std::make_unique<LoadedChunk>();
  loaded->cx = cx;
  loaded->cz = cz;
  if (status == world::ChunkLoad::ok) {
    loaded->present = true;
    loaded->chunk = std::move(chunk);
  } else if (status == world::ChunkLoad::not_present) {
    error.clear();
    known_absent_.insert(key);
    loaded->chunk.x = cx;
    loaded->chunk.z = cz;
  } else {
    reim::log::warn("runtime", "chunk " + std::to_string(cx) + "," +
                                   std::to_string(cz) + " skipped: " + error);
    known_absent_.insert(key);
    loaded->chunk.x = cx;
    loaded->chunk.z = cz;
  }
  loaded->build(blocks_);
  loaded_sections_ += loaded->non_empty_sections;
  block_count_ += loaded->non_air_blocks;
  chunks_.emplace(key, std::move(loaded));
  mark_neighbours_dirty(cx, cz);
  return true;
}

void Simulation::unload_chunk(LoadedChunk& chunk) {
  for (SectionSlot& slot : chunk.slots) {
    if (!slot.data || slot.data->empty()) continue;
    MeshEvent event;
    event.type = MeshEvent::Type::remove;
    event.chunk_x = chunk.cx;
    event.chunk_z = chunk.cz;
    event.section_y = slot.data->section_y;
    event.data = slot.data;
    events_.push_back(std::move(event));
    --mesh_sections_;
  }
  loaded_sections_ -= chunk.non_empty_sections;
  block_count_ -= chunk.non_air_blocks;
  mark_neighbours_dirty(chunk.cx, chunk.cz);
}

void Simulation::mark_neighbours_dirty(int cx, int cz) {
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int64_t key = chunk_key(cx + dx, cz + dz);
      if (chunks_.find(key) != chunks_.end()) dirty_.insert(key);
    }
  }
}

void Simulation::remesh_chunk(LoadedChunk& chunk) {
  MeshQueryContext context;
  context.cx = chunk.cx;
  context.cz = chunk.cz;
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dx = -1; dx <= 1; ++dx) {
      context.neighbours[(dz + 1) * 3 + (dx + 1)] = chunk_at(chunk.cx + dx, chunk.cz + dz);
    }
  }

  std::vector<mesh::SectionMeshOutput> outputs;
  mesh::mesh_chunk(chunk.cx, chunk.cz, chunk.chunk, chunk.section_block_ids, blocks_,
                   &query_mesh_block, &context, outputs);

  size_t output_index = 0;
  for (size_t i = 0; i < chunk.chunk.sections.size(); ++i) {
    const world::Section& section = chunk.chunk.sections[i];
    SectionSlot& slot = chunk.slots[i];

    const bool section_has_mesh =
        section.non_empty() && i < chunk.section_block_ids.size() &&
        !chunk.section_block_ids[i].empty();
    if (!section_has_mesh) {
      // A section that used to produce geometry is now empty (or was never
      // meshed). Clear whatever the Host still holds for it; without this a
      // future block-edit feature would leave stale meshes behind.
      if (slot.data) {
        if (!slot.data->empty()) --mesh_sections_;
        slot.data.reset();
      }
      auto push_clear = [&](uint32_t material) {
        MeshEvent event;
        event.type = MeshEvent::Type::update;
        event.chunk_x = chunk.cx;
        event.chunk_z = chunk.cz;
        event.section_y = section.y;
        event.revision = ++revision_counter_;
        event.material = material;
        event.data = nullptr;  // zero-vertex update clears the material
        events_.push_back(std::move(event));
      };
      if (slot.had_opaque) push_clear(REIM_MATERIAL_OPAQUE);
      if (slot.had_transparent) push_clear(REIM_MATERIAL_TRANSPARENT);
      slot.had_opaque = false;
      slot.had_transparent = false;
      continue;
    }

    if (output_index >= outputs.size()) break;

    auto data = std::make_shared<mesh::SectionMeshOutput>(std::move(outputs[output_index]));
    ++output_index;
    slot.data = data;
    slot.revision = ++revision_counter_;

    auto push_update = [&](uint32_t material) {
      MeshEvent event;
      event.type = MeshEvent::Type::update;
      event.chunk_x = chunk.cx;
      event.chunk_z = chunk.cz;
      event.section_y = data->section_y;
      event.revision = slot.revision;
      event.material = material;
      event.data = data;
      events_.push_back(std::move(event));
    };
    const bool has_opaque = !data->opaque_indices.empty();
    const bool has_transparent = !data->transparent_indices.empty();
    // An empty update clears a material the host still has; a full one
    // replaces it. Both carry the same revision so stale work is discarded.
    if (has_opaque || slot.had_opaque) push_update(REIM_MATERIAL_OPAQUE);
    if (has_transparent || slot.had_transparent) {
      push_update(REIM_MATERIAL_TRANSPARENT);
    }
    slot.had_opaque = has_opaque;
    slot.had_transparent = has_transparent;
    if (!data->empty()) ++mesh_sections_;
  }
}

void Simulation::initial_load(SimulationSink& sink) {
  const uint32_t radius = std::clamp<uint32_t>(config_.render_distance, 1, 16);
  const int center_x = world::floor_div(static_cast<int>(std::floor(player_.x)), 16);
  const int center_z = world::floor_div(static_cast<int>(std::floor(player_.z)), 16);
  last_center_x_ = center_x;
  last_center_z_ = center_z;

  std::vector<std::pair<int, int>> coords;
  for (int dz = -static_cast<int>(radius); dz <= static_cast<int>(radius); ++dz) {
    for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); ++dx) {
      coords.emplace_back(center_x + dx, center_z + dz);
    }
  }
  std::sort(coords.begin(), coords.end(), [&](const auto& a, const auto& b) {
    return distance_squared(a.first - center_x, a.second - center_z) <
           distance_squared(b.first - center_x, b.second - center_z);
  });

  initial_total_ = static_cast<uint32_t>(coords.size());
  initial_loaded_ = 0;
  if (create_world_) {
    for (const auto& [cx, cz] : coords) request_generated(cx, cz);
    wait_for_generated(coords.size(), 30000);
    initial_loaded_ = static_cast<uint32_t>(chunks_.size());
    emit_status(sink);
  } else {
    for (const auto& [cx, cz] : coords) {
      std::string error;
      load_chunk(cx, cz, error);
      ++initial_loaded_;
      if ((initial_loaded_ % 8u) == 0u) emit_status(sink);
    }
  }
  resolve_spawn_overlap();

  uint32_t meshed = 0;
  for (auto& [key, chunk] : chunks_) {
    (void)key;
    if (!chunk->present) continue;
    remesh_chunk(*chunk);
    if ((++meshed % 8u) == 0u) {
      emit_events(sink);
      emit_status(sink);
    }
  }
  dirty_.clear();
  ready_ = true;
  tps_window_start_ = std::chrono::steady_clock::now();
  tps_window_ticks_ = 0;
  emit_events(sink);
  emit_status(sink);
  reim::log::info("runtime",
                  "initial load complete: " + std::to_string(chunks_.size()) +
                      " chunks, " + std::to_string(mesh_sections_) + " meshed sections");
}

void Simulation::apply_input(const SimInput& input) {
  if (input.type == REIM_INPUT_MOUSE_MOTION) {
    apply_mouse(player_, input.dx, input.dy, config_.mouse_sensitivity,
                config_.invert_y);
    return;
  }
  if (input.type != REIM_INPUT_KEY) return;
  const bool pressed = input.action != REIM_ACTION_RELEASE;
  switch (input.key) {
    case REIM_KEY_W:
      input_.forward = pressed;
      break;
    case REIM_KEY_S:
      input_.back = pressed;
      break;
    case REIM_KEY_A:
      input_.left = pressed;
      break;
    case REIM_KEY_D:
      input_.right = pressed;
      break;
    case REIM_KEY_SPACE:
      input_.jump_held = pressed;
      if (pressed) input_.jump_requested = true;
      break;
    case REIM_KEY_LEFT_SHIFT:
      input_.sneak = pressed;
      break;
    case REIM_KEY_LEFT_CONTROL:
      input_.sprint = pressed;
      break;
    case REIM_KEY_UP:
      input_.look_up = pressed;
      break;
    case REIM_KEY_DOWN:
      input_.look_down = pressed;
      break;
    case REIM_KEY_LEFT:
      input_.look_left = pressed;
      break;
    case REIM_KEY_RIGHT:
      input_.look_right = pressed;
      break;
    default:
      break;
  }
}

void Simulation::tick(SimulationSink& sink) {
  drain_generated(4);
  update_world_time();
  player_step(player_, input_, &query_world_block, this, blocks_);
  ++tick_;
  update_tps();

  const int center_x = world::floor_div(static_cast<int>(std::floor(player_.x)), 16);
  const int center_z = world::floor_div(static_cast<int>(std::floor(player_.z)), 16);
  const bool center_changed = center_x != last_center_x_ || center_z != last_center_z_;
  ++stream_timer_;
  if (center_changed || stream_timer_ >= 20) {
    stream_timer_ = 0;
    last_center_x_ = center_x;
    last_center_z_ = center_z;
    update_streaming();
  }
  remesh_dirty(4);
  emit_events(sink);
  emit_status(sink);
}

void Simulation::update_streaming() {
  const int radius = static_cast<int>(std::clamp<uint32_t>(config_.render_distance, 1, 16));
  const int center_x = last_center_x_;
  const int center_z = last_center_z_;

  for (auto it = chunks_.begin(); it != chunks_.end();) {
    LoadedChunk& chunk = *it->second;
    const int dx = chunk.cx - center_x;
    const int dz = chunk.cz - center_z;
    if (std::max(std::abs(dx), std::abs(dz)) > radius + 1) {
      unload_chunk(chunk);
      dirty_.erase(it->first);
      it = chunks_.erase(it);
    } else {
      ++it;
    }
  }

  std::vector<std::pair<int, int>> coords;
  coords.reserve(static_cast<size_t>((2 * radius + 1) * (2 * radius + 1)));
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      coords.emplace_back(center_x + dx, center_z + dz);
    }
  }
  std::sort(coords.begin(), coords.end(), [&](const auto& a, const auto& b) {
    return distance_squared(a.first - center_x, a.second - center_z) <
           distance_squared(b.first - center_x, b.second - center_z);
  });
  int budget = 4;
  for (const auto& [cx, cz] : coords) {
    if (budget <= 0) break;
    const int64_t key = chunk_key(cx, cz);
    if (chunks_.find(key) != chunks_.end()) continue;
    if (create_world_) {
      request_generated(cx, cz);
      --budget;
      continue;
    }
    if (known_absent_.contains(key)) continue;
    std::string error;
    load_chunk(cx, cz, error);
    --budget;
  }
}

void Simulation::remesh_dirty(uint32_t budget) {
  uint32_t count = 0;
  auto it = dirty_.begin();
  while (it != dirty_.end() && count < budget) {
    const int64_t key = *it;
    it = dirty_.erase(it);
    const auto found = chunks_.find(key);
    if (found == chunks_.end() || !found->second->present) continue;
    remesh_chunk(*found->second);
    ++count;
  }
}

void Simulation::update_tps() {
  ++tps_window_ticks_;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed =
      std::chrono::duration<double>(now - tps_window_start_).count();
  if (elapsed >= 1.0) {
    measured_tps_ = static_cast<float>(tps_window_ticks_ / elapsed);
    tps_window_ticks_ = 0;
    tps_window_start_ = now;
  }
}

void Simulation::emit_status(SimulationSink& sink) { sink.publish_status(status()); }

void Simulation::emit_events(SimulationSink& sink) {
  if (events_.empty()) return;
  sink.publish_mesh_events(events_);
  events_.clear();
}

SimStatus Simulation::status() const {
  SimStatus status;
  status.ready = ready_;
  status.loaded_chunks = static_cast<uint32_t>(chunks_.size());
  status.total_chunks = initial_total_;
  status.loading_percent =
      initial_total_ == 0
          ? (ready_ ? 100u : 0u)
          : std::min(100u, initial_loaded_ * 100u / initial_total_);
  status.tick = tick_;
  status.player = player_;
  status.loaded_sections = loaded_sections_;
  status.mesh_sections = mesh_sections_;
  status.block_count = block_count_;
  status.measured_tps = measured_tps_;
  status.third_person_distance =
      camera_backoff_distance(player_.x, player_.y + kPlayerEyeHeight, player_.z,
                              player_.yaw, player_.pitch, 4.0f,
                              &Simulation::collision_query, this);
  status.dimension = static_cast<uint32_t>(dimension_id_);
  status.time_of_day = world_time_;
  status.day = static_cast<uint32_t>(world_time_ / 24000u);
  status.weather = weather_;
  const game::EnvironmentState environment_state = environment();
  status.sky_darken = environment_state.sky_darken;
  status.sun_angle = static_cast<float>((world_time_ % 24000u) * 360u / 24000u);
  status.seed = seed_;
  status.generated_chunks = generated_chunks_;
  status.ore_blocks = ore_blocks_;
  status.trees = trees_;
  status.plants = plants_;
  status.worldgen_source = gen_profile_.source == "data" ? 1u : 0u;
  status.spawn_surface = spawn_surface_;

  const int foot_x = static_cast<int>(std::floor(player_.x));
  const int foot_y = static_cast<int>(std::floor(player_.y));
  const int foot_z = static_cast<int>(std::floor(player_.z));
  const int eye_y = static_cast<int>(std::floor(player_.y + kPlayerEyeHeight));
  const std::array<std::array<int, 3>, 6> positions{{
      {{foot_x, foot_y, foot_z}},
      {{foot_x, foot_y - 1, foot_z}},
      {{foot_x, eye_y, foot_z}},
      {{foot_x, eye_y + 1, foot_z}},
      {{foot_x + 1, foot_y, foot_z}},
      {{foot_x, foot_y, foot_z + 1}},
  }};
  status.samples.reserve(positions.size());
  for (const auto& position : positions) {
    BlockSample sample;
    sample.x = position[0];
    sample.y = position[1];
    sample.z = position[2];
    sample.name = blocks_.def(block_id_at(sample.x, sample.y, sample.z)).name;
    status.samples.push_back(std::move(sample));
  }
  return status;
}

std::vector<MeshEvent> Simulation::take_events() {
  std::vector<MeshEvent> events;
  events.swap(events_);
  return events;
}

std::string Simulation::contract_model_defs_json() const {
  static const char* const kCandidates[] = {"minecraft:stone", "minecraft:dirt",
                                           "minecraft:grass_block",
                                           "minecraft:oak_planks"};
  uint32_t block_id = 0;
  for (const char* name : kCandidates) {
    const uint32_t candidate = blocks_.find(name);
    if (candidate == 0) continue;
    const data::BlockDef& def = blocks_.def(candidate);
    if (def.faces[1].u1 > def.faces[1].u0 && def.faces[1].v1 > def.faces[1].v0) {
      block_id = candidate;
      break;
    }
  }
  if (block_id == 0) {
    for (uint32_t id = 1; id < blocks_.size(); ++id) {
      const data::BlockDef& def = blocks_.def(id);
      if (!def.full_block) continue;
      if (def.faces[1].u1 > def.faces[1].u0) {
        block_id = id;
        break;
      }
    }
  }
  nlohmann::json model;
  model["id"] = 0;
  model["name"] = "reim:debug_box";
  model["texture"] = REIM_UI_TEXTURE_BLOCK_ATLAS;
  nlohmann::json part;
  part["name"] = "body";
  part["pivot"] = {0.0, 0.5, 0.0};
  part["size"] = {0.5, 0.5, 0.5};
  if (block_id != 0) {
    const data::FaceTexture& face = blocks_.def(block_id).faces[1];
    part["uv"] = {face.u0, face.v0, face.u1, face.v1};
  } else {
    part["uv"] = {0.0, 0.0, 1.0 / 128.0, 1.0 / 8.0};
  }
  part["color"] = 0xFFFFFFFFu;
  model["parts"] = nlohmann::json::array({part});
  nlohmann::json root;
  root["format"] = 1;
  root["models"] = nlohmann::json::array({model});
  root["source_block"] =
      block_id != 0 ? blocks_.def(block_id).name : std::string("none");
  return root.dump();
}

uint32_t Simulation::item_id_for_block(std::string_view name) const {
  return blocks_.find(name);
}

} // namespace reim::core
