#include "core/simulation.h"

#include "reim_abi.h"
#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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

} // namespace

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
      ids.push_back(blocks.find(state.name));
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

bool Simulation::open(const SimulationConfig& config, std::string& error) {
  config_ = config;
  if (!blocks_.load(config_.generated_path, error)) {
    error = "generated block data: " + error;
    return false;
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
  build_spawn();
  reim::log::info("runtime",
                  "simulation opened " + file::to_utf8(config_.world_path) + " (" +
                      dimension_->id + ", " + std::to_string(blocks_.size()) +
                      " block definitions)");
  return true;
}

void Simulation::build_spawn() {
  player_ = Player{};
  if (config_.spawn_override) {
    player_.x = config_.spawn_x;
    player_.y = config_.spawn_y;
    player_.z = config_.spawn_z;
    return;
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
    return;
  }
  if (level.player.present) {
    player_.x = level.player.x;
    player_.y = level.player.y;
    player_.z = level.player.z;
    player_.yaw = level.player.yaw;
    player_.pitch = level.player.pitch;
    return;
  }
  player_.x = 0.5;
  player_.y = 96.0;
  player_.z = 0.5;
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
  for (const auto& [cx, cz] : coords) {
    std::string error;
    load_chunk(cx, cz, error);
    ++initial_loaded_;
    if ((initial_loaded_ % 8u) == 0u) emit_status(sink);
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

} // namespace reim::core
