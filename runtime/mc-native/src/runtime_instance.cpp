#include "src/runtime_instance.h"

#include "data/generated_manifest.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/log.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace reim::runtime {
namespace {

constexpr uint32_t kExpectedAbi = REIM_ABI_VERSION;
constexpr const char* kExpectedProfile = "26.3";
constexpr const char* kBuildId = "reim-runtime-26.3-abi2";
constexpr auto kTickPeriod = std::chrono::milliseconds(50);
constexpr size_t kBlobAlignment = 16;

size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t digest_prefix(const reim::hash::Digest32& digest) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value = (value << 8) | digest[static_cast<size_t>(i)];
  return value;
}

void fill_camera(const core::Player& player, float render_distance,
                 ReimCameraSnapshot& out) {
  out.position.x = static_cast<float>(player.x);
  out.position.y = static_cast<float>(player.y + core::kPlayerEyeHeight);
  out.position.z = static_cast<float>(player.z);
  out.yaw = player.yaw;
  out.pitch = player.pitch;
  out.fov_y_degrees = 70.0f;
  out.near_z = 0.05f;
  out.far_z = (render_distance + 1.0f) * 16.0f;
  out.dimension = 0;
}

void fill_player(const core::Player& player, ReimPlayerSnapshot& out) {
  out.position.x = static_cast<float>(player.x);
  out.position.y = static_cast<float>(player.y);
  out.position.z = static_cast<float>(player.z);
  out.velocity.x = static_cast<float>(player.vx);
  out.velocity.y = static_cast<float>(player.vy);
  out.velocity.z = static_cast<float>(player.vz);
  out.yaw = player.yaw;
  out.pitch = player.pitch;
  out.eye_height = static_cast<float>(core::kPlayerEyeHeight);
  out.half_width = static_cast<float>(core::kPlayerHalfWidth);
  out.height = static_cast<float>(core::kPlayerHeight);
  out.flags = player.flags;
}

// The log sink is process-wide (one per DLL, not per instance). Keep every live
// Host that supplied a log callback so instances do not overwrite each other.
std::mutex& host_list_mutex() {
  static std::mutex m;
  return m;
}

std::vector<const ReimHostApi*>& host_list() {
  static std::vector<const ReimHostApi*> hosts;
  return hosts;
}

void broadcast_log(reim::log::Level level, std::string_view category,
                   std::string_view message) {
  std::vector<const ReimHostApi*> hosts;
  {
    std::lock_guard<std::mutex> lock(host_list_mutex());
    hosts = host_list();
  }
  if (hosts.empty()) return;
  const std::string category_text(category);
  const std::string message_text(message);
  for (const ReimHostApi* host : hosts) {
    if (host != nullptr && host->log != nullptr) {
      host->log(static_cast<int32_t>(level), category_text.c_str(),
                message_text.c_str());
    }
  }
}

void register_host(const ReimHostApi* host) {
  if (host == nullptr || host->log == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(host_list_mutex());
    if (std::find(host_list().begin(), host_list().end(), host) ==
        host_list().end()) {
      host_list().push_back(host);
    }
  }
  reim::log::set_sink(&broadcast_log);
}

void unregister_host(const ReimHostApi* host) {
  if (host == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(host_list_mutex());
    host_list().erase(std::remove(host_list().begin(), host_list().end(), host),
                      host_list().end());
    if (!host_list().empty()) return;
  }
  reim::log::set_sink(reim::log::Sink{});
}

std::string escape_json(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void append_number(std::string& out, const char* key, double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), ",\"%s\":%.6f", key, value);
  out += buffer;
}

void append_uint(std::string& out, const char* key, uint64_t value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), ",\"%s\":%llu", key,
                static_cast<unsigned long long>(value));
  out += buffer;
}

void append_bool(std::string& out, const char* key, bool value) {
  out += ",\"";
  out += key;
  out += "\":";
  out += value ? "true" : "false";
}

} // namespace

RuntimeInstance* RuntimeInstance::create(const ReimInstanceConfig* config,
                                         const ReimHostApi* host,
                                         int32_t* out_error) {
  if (!config || !out_error) return nullptr;
  *out_error = REIM_ERR_INVALID_ARGUMENT;
  if (config->abi_version != kExpectedAbi) {
    *out_error = REIM_ERR_ABI_MISMATCH;
    return nullptr;
  }
  if (config->struct_size < REIM_INSTANCE_CONFIG_MIN_SIZE) return nullptr;
  if (!config->generated_path) return nullptr;
  if (config->render_level != 0) {
    *out_error = REIM_ERR_CAPABILITY;
    if (host && host->log) {
      const std::string message =
          "create: render_level " + std::to_string(config->render_level) +
          " is not supported (M2 is R0 only)";
      host->log(REIM_LOG_ERROR, "runtime", message.c_str());
    }
    return nullptr;
  }
  /* v2 frame ownership requires a Host allocator (reim_abi.h). */
  if (!host || !host->alloc || !host->free) {
    if (host && host->log) {
      host->log(REIM_LOG_ERROR, "runtime",
                "create: host alloc/free callbacks are required in ABI v2");
    }
    return nullptr;
  }

  const std::filesystem::path generated_dir(config->generated_path);
  reim::data::GeneratedManifest manifest;
  std::string error;
  if (!reim::data::validate_generated(generated_dir, kExpectedProfile, kExpectedAbi,
                                      manifest, error)) {
    if (host && host->log) {
      const std::string message = "create: generated data rejected: " + error;
      host->log(REIM_LOG_ERROR, "data", message.c_str());
    }
    *out_error = REIM_ERR_VERSION_MISMATCH;
    return nullptr;
  }
  if (!config->world_path) {
    *out_error = REIM_ERR_NO_WORLD;
    return nullptr;
  }

  const std::filesystem::path world_path(config->world_path);
  if (!reim::file::is_directory(world_path) ||
      !reim::file::exists(world_path / "level.dat")) {
    if (host && host->log) {
      const std::string message =
          "create: world not found (level.dat missing): " + reim::file::to_utf8(world_path);
      host->log(REIM_LOG_ERROR, "runtime", message.c_str());
    }
    *out_error = REIM_ERR_NO_WORLD;
    return nullptr;
  }

  core::SimulationConfig sim_config;
  sim_config.world_path = world_path;
  sim_config.generated_path = generated_dir;
  sim_config.render_distance =
      config->render_distance == 0 ? 6 : std::min<uint32_t>(config->render_distance, 16);
  sim_config.spawn_override = (config->flags & REIM_CONFIG_FLAG_SPAWN_OVERRIDE) != 0;
  sim_config.spawn_x = config->spawn_x;
  sim_config.spawn_y = config->spawn_y;
  sim_config.spawn_z = config->spawn_z;
  sim_config.spawn_yaw = config->spawn_yaw;
  sim_config.spawn_pitch = config->spawn_pitch;
  sim_config.mouse_sensitivity =
      config->mouse_sensitivity > 0.0f ? config->mouse_sensitivity : 0.15f;
  sim_config.invert_y = config->invert_y != 0;

  auto* instance = new RuntimeInstance();
  instance->host_ = *host;
  instance->render_distance_ = sim_config.render_distance;
  // Route the core library's log lines to the instance's Host copy for the
  // lifetime of the instance; the caller's struct may go out of scope.
  register_host(&instance->host_);
  instance->registered_host_ = &instance->host_;
  if (!instance->simulation_.open(sim_config, error)) {
    if (host->log) {
      const std::string message = "create: " + error;
      host->log(REIM_LOG_ERROR, "runtime", message.c_str());
    }
    *out_error = error.rfind("generated", 0) == 0 ? REIM_ERR_PARSE : REIM_ERR_NO_WORLD;
    unregister_host(instance->registered_host_);
    delete instance;
    return nullptr;
  }
  instance->build_id_ = kBuildId;
  std::string manifest_text;
  if (!reim::file::read_text(generated_dir / "manifest.json", manifest_text, error)) {
    if (host->log) host->log(REIM_LOG_ERROR, "runtime", error.c_str());
    unregister_host(instance->registered_host_);
    delete instance;
    *out_error = REIM_ERR_IO;
    return nullptr;
  }
  instance->manifest_hash_ = digest_prefix(reim::hash::sha256(manifest_text));
  if (!instance->load_assets(generated_dir, error)) {
    if (host->log) {
      const std::string message = "create: assets rejected: " + error;
      host->log(REIM_LOG_ERROR, "runtime", message.c_str());
    }
    unregister_host(instance->registered_host_);
    delete instance;
    *out_error = REIM_ERR_IO;
    return nullptr;
  }
  if (config->native_version_path && host->log) {
    const std::string message =
        "native_version=" + reim::file::to_utf8(std::filesystem::path(
                                config->native_version_path));
    host->log(REIM_LOG_INFO, "runtime", message.c_str());
  }
  *out_error = REIM_OK;
  return instance;
}

RuntimeInstance::~RuntimeInstance() {
  stop();
  free_outstanding_frames();
  unregister_host(registered_host_);
}

int32_t RuntimeInstance::start() {
  if (started_.load()) return REIM_ERR_ALREADY_STARTED;
  stop_.store(false);
  started_.store(true);
  thread_ = std::thread([this]() { run(); });
  return REIM_OK;
}

void RuntimeInstance::stop() {
  if (!started_.exchange(false)) return;
  stop_.store(true);
  if (thread_.joinable()) thread_.join();
}

void RuntimeInstance::run() {
  log_message(REIM_LOG_INFO, "runtime", "simulation thread started");
  simulation_.initial_load(*this);

  auto next_tick = std::chrono::steady_clock::now() + kTickPeriod;
  while (!stop_.load(std::memory_order_relaxed)) {
    if (paused_.load(std::memory_order_relaxed)) {
      partial_tick_.store(0.0f);
      next_tick = std::chrono::steady_clock::now() + kTickPeriod;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_tick) {
      std::vector<core::SimInput> inputs;
      {
        std::lock_guard<std::mutex> lock(input_mutex_);
        inputs.swap(queued_inputs_);
      }
      for (const core::SimInput& input : inputs) simulation_.apply_input(input);
      simulation_.tick(*this);
      next_tick += kTickPeriod;
      if (now - next_tick > 5 * kTickPeriod) {
        next_tick = now + kTickPeriod;  // catch-up cap: never spiral
      }
      partial_tick_.store(0.0f);
    } else {
      const float fraction =
          std::chrono::duration<float>(now - (next_tick - kTickPeriod)).count() /
          std::chrono::duration<float>(kTickPeriod).count();
      partial_tick_.store(std::clamp(fraction, 0.0f, 1.0f));
      auto sleep = std::chrono::duration_cast<std::chrono::milliseconds>(next_tick - now);
      if (sleep > std::chrono::milliseconds(5)) sleep = std::chrono::milliseconds(5);
      if (sleep < std::chrono::milliseconds(1)) sleep = std::chrono::milliseconds(1);
      std::this_thread::sleep_for(sleep);
    }
  }
  log_message(REIM_LOG_INFO, "runtime", "simulation thread stopped");
}

void RuntimeInstance::publish_status(const core::SimStatus& status) {
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (!has_prev_status_) {
    prev_status_ = status;
    has_prev_status_ = true;
  } else if (status.tick != latest_status_.tick) {
    prev_status_ = latest_status_;
  }
  pending_.status = status;
  latest_status_ = status;
}

void RuntimeInstance::publish_mesh_events(std::vector<core::MeshEvent>& events) {
  std::lock_guard<std::mutex> lock(frame_mutex_);
  for (const core::MeshEvent& event : events) {
    if (event.type == core::MeshEvent::Type::remove) {
      ReimSectionRemove remove{};
      remove.chunk_x = event.chunk_x;
      remove.chunk_z = event.chunk_z;
      remove.section_y = event.section_y;
      pending_.removes.push_back(remove);
      continue;
    }
    if (!event.data) {
      // Clear event: the simulation dropped this material (for example the
      // section became empty after an edit). Zero counts remove the mesh.
      ReimSectionMeshUpdate clear{};
      clear.chunk_x = event.chunk_x;
      clear.chunk_z = event.chunk_z;
      clear.section_y = event.section_y;
      clear.revision = event.revision;
      clear.material = event.material;
      clear.bounds.min.x = static_cast<float>(event.chunk_x * 16);
      clear.bounds.min.y = static_cast<float>(event.section_y * 16);
      clear.bounds.min.z = static_cast<float>(event.chunk_z * 16);
      clear.bounds.max.x = clear.bounds.min.x + 16.0f;
      clear.bounds.max.y = clear.bounds.min.y + 16.0f;
      clear.bounds.max.z = clear.bounds.min.z + 16.0f;
      pending_.updates.push_back(clear);
      continue;
    }
    const bool opaque = event.material == REIM_MATERIAL_OPAQUE;
    const std::vector<ReimVertex>& vertices =
        opaque ? event.data->opaque_vertices : event.data->transparent_vertices;
    const std::vector<uint32_t>& indices =
        opaque ? event.data->opaque_indices : event.data->transparent_indices;

    pending_.keep.push_back(event.data);
    ReimSectionMeshUpdate update{};
    update.chunk_x = event.chunk_x;
    update.chunk_z = event.chunk_z;
    update.section_y = event.section_y;
    update.revision = event.revision;
    update.material = event.material;
    update.bounds.min.x = static_cast<float>(event.chunk_x * 16);
    update.bounds.min.y = static_cast<float>(event.section_y * 16);
    update.bounds.min.z = static_cast<float>(event.chunk_z * 16);
    update.bounds.max.x = update.bounds.min.x + 16.0f;
    update.bounds.max.y = update.bounds.min.y + 16.0f;
    update.bounds.max.z = update.bounds.min.z + 16.0f;
    update.vertex_count = static_cast<uint32_t>(vertices.size());
    update.vertex_stride = REIM_VERTEX_STRIDE;
    update.index_count = static_cast<uint32_t>(indices.size());
    update.vertices = vertices.empty() ? nullptr : vertices.data();
    update.indices = indices.empty() ? nullptr : indices.data();
    pending_.updates.push_back(update);
  }
  events.clear();
}

int32_t RuntimeInstance::fetch_frame(ReimRenderFrame* out) {
  if (!out) return REIM_ERR_INVALID_ARGUMENT;
  if (out->struct_size < REIM_RENDER_FRAME_MIN_SIZE) return REIM_ERR_INVALID_ARGUMENT;
  if (!started_.load()) return REIM_ERR_NOT_STARTED;
  if (!host_.alloc || !host_.free) return REIM_ERR_INTERNAL;

  /* Backpressure: never block the simulation on Host consumption. */
  if (outstanding_count() >= REIM_MAX_PENDING_FRAMES) {
    log_message(REIM_LOG_WARN, "frame",
                "fetch_frame: Host has 4 unreleased frame blobs; returning BUSY");
    return REIM_ERR_BUSY;
  }

  const auto now = std::chrono::steady_clock::now();
  float frame_ms = 0.0f;
  if (last_fetch_.time_since_epoch().count() != 0) {
    frame_ms = std::chrono::duration<float, std::milli>(now - last_fetch_).count();
  }
  last_fetch_ = now;

  std::lock_guard<std::mutex> lock(frame_mutex_);
  std::swap(pending_, published_);

  FrameState& frame = published_;
  const size_t update_count = frame.updates.size();
  const size_t remove_count = frame.removes.size();

  auto reject = [&](int32_t code, const char* reason) {
    // Put the new events back so the Host can retry without losing sections.
    std::swap(pending_, published_);
    log_message(REIM_LOG_WARN, "frame", std::string("fetch_frame: ") + reason);
    return code;
  };
  if (update_count > REIM_MAX_FRAME_UPDATES) {
    return reject(REIM_ERR_LIMIT, "update count exceeds REIM_MAX_FRAME_UPDATES");
  }
  if (remove_count > REIM_MAX_FRAME_REMOVES) {
    return reject(REIM_ERR_LIMIT, "remove count exceeds REIM_MAX_FRAME_REMOVES");
  }

  const size_t updates_off = 0;
  const size_t removes_off = align_up(update_count * sizeof(ReimSectionMeshUpdate),
                                      kBlobAlignment);
  size_t offset = align_up(removes_off + remove_count * sizeof(ReimSectionRemove),
                           kBlobAlignment);
  std::vector<size_t> vertex_off(update_count, 0);
  std::vector<size_t> index_off(update_count, 0);
  for (size_t i = 0; i < update_count; ++i) {
    const ReimSectionMeshUpdate& update = frame.updates[i];
    vertex_off[i] = align_up(offset, kBlobAlignment);
    offset = vertex_off[i] +
             static_cast<size_t>(update.vertex_count) * sizeof(ReimVertex);
    index_off[i] = align_up(offset, alignof(uint32_t));
    offset = index_off[i] +
             static_cast<size_t>(update.index_count) * sizeof(uint32_t);
  }
  /* Every successful frame owns a blob so frame/release accounting is
   * deterministic, even when the frame carries no mesh data. */
  const size_t total_bytes =
      std::max(align_up(offset, kBlobAlignment), static_cast<size_t>(64));
  if (total_bytes > REIM_MAX_FRAME_BYTES) {
    return reject(REIM_ERR_LIMIT, "frame blob exceeds REIM_MAX_FRAME_BYTES");
  }

  void* block = host_.alloc(total_bytes, kBlobAlignment);
  if (!block) {
    return reject(REIM_ERR_INTERNAL, "host alloc failed");
  }
  auto* base = static_cast<uint8_t*>(block);
  auto* out_updates = reinterpret_cast<ReimSectionMeshUpdate*>(base + updates_off);
  if (update_count != 0) {
    std::memcpy(out_updates, frame.updates.data(),
                update_count * sizeof(ReimSectionMeshUpdate));
  }
  if (remove_count != 0) {
    std::memcpy(base + removes_off, frame.removes.data(),
                remove_count * sizeof(ReimSectionRemove));
  }
  for (size_t i = 0; i < update_count; ++i) {
    const ReimSectionMeshUpdate& source = frame.updates[i];
    ReimSectionMeshUpdate& target = out_updates[i];
    target.vertices = nullptr;
    target.indices = nullptr;
    if (source.vertex_count != 0 && source.vertices != nullptr) {
      auto* dst = reinterpret_cast<ReimVertex*>(base + vertex_off[i]);
      std::memcpy(dst, source.vertices,
                  static_cast<size_t>(source.vertex_count) * sizeof(ReimVertex));
      target.vertices = dst;
    }
    if (source.index_count != 0 && source.indices != nullptr) {
      auto* dst = reinterpret_cast<uint32_t*>(base + index_off[i]);
      std::memcpy(dst, source.indices,
                  static_cast<size_t>(source.index_count) * sizeof(uint32_t));
      target.indices = dst;
    }
  }

  const uint64_t frame_id = ++frame_counter_;
  const core::SimStatus& status = frame.status;

  const size_t host_struct_size = out->struct_size;
  std::memset(out, 0, std::min(host_struct_size, sizeof(*out)));
  out->struct_size = sizeof(*out);
  out->abi_version = kExpectedAbi;
  out->frame_id = frame_id;
  out->tick = status.tick;
  out->partial_tick = partial_tick_.load();
  out->ready = status.ready ? 1u : 0u;
  out->loaded_chunks = status.loaded_chunks;
  out->total_chunks = status.total_chunks;
  out->loading_percent = status.loading_percent;

  fill_camera(status.player, static_cast<float>(render_distance_), out->camera);
  fill_player(status.player, out->player);

  /* Interpolation snapshot: prev_* is the previous published tick. When the
   * simulation has not advanced (pause/loading) alpha stays 0 and prev equals
   * the current snapshot (reim_render.h). */
  bool advanced = has_prev_status_ && status.tick > prev_status_.tick;
  const core::Player& prev_player = advanced ? prev_status_.player : status.player;
  fill_camera(prev_player, static_cast<float>(render_distance_), out->prev_camera);
  fill_player(prev_player, out->prev_player);
  out->tick_alpha =
      (advanced && !paused_.load()) ? std::clamp(partial_tick_.load(), 0.0f, 1.0f)
                                    : 0.0f;

  out->environment.sky_color = 0x78A7FFu;
  out->environment.fog_color = 0xB0C8FFu;
  out->environment.fog_start = out->camera.far_z * 0.6f;
  out->environment.fog_end = out->camera.far_z;

  out->update_count = static_cast<uint32_t>(update_count);
  out->remove_count = static_cast<uint32_t>(remove_count);
  out->updates =
      update_count == 0 ? nullptr : reinterpret_cast<const ReimSectionMeshUpdate*>(
                                         static_cast<uint8_t*>(block) + updates_off);
  out->removes =
      remove_count == 0 ? nullptr : reinterpret_cast<const ReimSectionRemove*>(
                                        static_cast<uint8_t*>(block) + removes_off);
  out->loaded_sections = status.loaded_sections;
  out->mesh_sections = status.mesh_sections;
  out->block_count = status.block_count;
  out->measured_tps = status.measured_tps;
  out->measured_frame_ms = frame_ms;
  out->third_person_distance = status.third_person_distance;
  out->owned_block = block;
  out->owned_bytes = block ? total_bytes : 0;

  /* The blob now owns the mesh bytes; the Runtime no longer needs the source
   * buffers or the previous frame's event lists. */
  frame.keep.clear();
  frame.updates.clear();
  frame.removes.clear();
  pending_.keep.clear();
  pending_.updates.clear();
  pending_.removes.clear();
  pending_.status = status;

  if (block != nullptr) {
    std::lock_guard<std::mutex> out_lock(frames_out_mutex_);
    outstanding_.push_back(OutstandingFrame{frame_id, block});
  }
  return REIM_OK;
}

void RuntimeInstance::release_frame(uint64_t frame_id) {
  std::lock_guard<std::mutex> lock(frames_out_mutex_);
  const auto it = std::find_if(outstanding_.begin(), outstanding_.end(),
                               [&](const OutstandingFrame& entry) {
                                 return entry.frame_id == frame_id;
                               });
  if (it == outstanding_.end()) {
    // Duplicate/unknown release: no-op beyond a diagnostic.
    return;
  }
  void* block = it->block;
  outstanding_.erase(it);
  if (block != nullptr && host_.free) host_.free(block);
}

size_t RuntimeInstance::outstanding_count() {
  std::lock_guard<std::mutex> lock(frames_out_mutex_);
  return outstanding_.size();
}

void RuntimeInstance::free_outstanding_frames() {
  std::vector<OutstandingFrame> blocks;
  {
    std::lock_guard<std::mutex> lock(frames_out_mutex_);
    blocks.swap(outstanding_);
  }
  if (!host_.free) return;
  for (const OutstandingFrame& entry : blocks) {
    if (entry.block != nullptr) host_.free(entry.block);
  }
}

bool RuntimeInstance::load_assets(const std::filesystem::path& generated_dir,
                                  std::string& error) {
  if (!reim::file::read_all(generated_dir / "atlas.png", atlas_png_, error)) {
    return false;
  }
  if (atlas_png_.size() < 24) {
    error = "atlas.png is too small to be a PNG";
    return false;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t tile_size = 16;
  std::string textures_text;
  if (reim::file::read_text(generated_dir / "textures.json", textures_text, error)) {
    const auto root = nlohmann::json::parse(textures_text.begin(), textures_text.end(),
                                            nullptr, false);
    if (!root.is_discarded() && root.is_object()) {
      const auto atlas = root.find("atlas");
      if (atlas != root.end() && atlas->is_object()) {
        width = atlas->value("width", 0u);
        height = atlas->value("height", 0u);
        tile_size = atlas->value("tile_size", tile_size);
      }
    }
  }
  if (width == 0 || height == 0) {
    // PNG IHDR: 8-byte signature, 4-byte length, "IHDR", width, height (BE).
    const uint8_t* header = atlas_png_.data();
    if (header[12] != 'I' || header[13] != 'H' || header[14] != 'D' ||
        header[15] != 'R') {
      error = "atlas.png has no IHDR chunk";
      return false;
    }
    width = (static_cast<uint32_t>(header[16]) << 24) |
            (static_cast<uint32_t>(header[17]) << 16) |
            (static_cast<uint32_t>(header[18]) << 8) |
            static_cast<uint32_t>(header[19]);
    height = (static_cast<uint32_t>(header[20]) << 24) |
             (static_cast<uint32_t>(header[21]) << 16) |
             (static_cast<uint32_t>(header[22]) << 8) |
             static_cast<uint32_t>(header[23]);
  }
  if (width == 0 || height == 0 || tile_size == 0) {
    error = "atlas dimensions are invalid";
    return false;
  }

  const auto digest = reim::hash::sha256(atlas_png_.data(), atlas_png_.size());
  atlas_revision_ = digest_prefix(digest);
  if (atlas_revision_ == 0) atlas_revision_ = 1;

  nlohmann::json meta;
  meta["width"] = width;
  meta["height"] = height;
  meta["tile_size"] = tile_size;
  meta["padding"] = 0;
  meta["sampling"] = "nearest";
  meta["format"] = "rgba8";
  meta["channels"] = 4;
  meta["hash"] = reim::hash::hex(digest);
  meta["source"] = "atlas.png";
  atlas_meta_json_ = meta.dump();
  return true;
}

int32_t RuntimeInstance::get_runtime_info(ReimRuntimeInfo* out) {
  if (!out) return REIM_ERR_INVALID_ARGUMENT;
  if (out->struct_size < REIM_RUNTIME_INFO_MIN_SIZE) return REIM_ERR_INVALID_ARGUMENT;
  const size_t host_struct_size = out->struct_size;
  std::memset(out, 0, std::min(host_struct_size, sizeof(*out)));
  out->struct_size = sizeof(*out);
  out->abi_version = kExpectedAbi;
  std::snprintf(out->profile, sizeof(out->profile), "%s", kExpectedProfile);
  std::snprintf(out->build_id, sizeof(out->build_id), "%s", build_id_.c_str());
  out->manifest_hash = manifest_hash_;
  out->capabilities = REIM_CAP_FRAME_BLOB | REIM_CAP_INTERPOLATION |
                      REIM_CAP_ATLAS_ASSET | REIM_CAP_R0;
  out->render_level = 0;
  out->dll_abi_version = kExpectedAbi;
  return REIM_OK;
}

int32_t RuntimeInstance::get_asset(uint32_t kind, const void** out_data,
                                   uint64_t* out_size, uint64_t* out_revision) {
  if (!out_data || !out_size || !out_revision) return REIM_ERR_INVALID_ARGUMENT;
  *out_data = nullptr;
  *out_size = 0;
  *out_revision = 0;
  switch (kind) {
    case REIM_ASSET_ATLAS_PNG:
      if (atlas_png_.empty()) return REIM_ERR_NOT_FOUND;
      *out_data = atlas_png_.data();
      *out_size = atlas_png_.size();
      *out_revision = atlas_revision_;
      return REIM_OK;
    case REIM_ASSET_ATLAS_META:
      if (atlas_meta_json_.empty()) return REIM_ERR_NOT_FOUND;
      *out_data = atlas_meta_json_.data();
      *out_size = atlas_meta_json_.size();
      *out_revision = atlas_revision_;
      return REIM_OK;
    default:
      return REIM_ERR_INVALID_ARGUMENT;
  }
}

void RuntimeInstance::submit_input(const ReimInputEvent* event) {
  if (!event || event->struct_size < REIM_INPUT_EVENT_MIN_SIZE) return;
  core::SimInput input;
  input.type = event->type;
  input.key = event->key;
  input.action = event->action;
  input.dx = event->dx;
  input.dy = event->dy;
  std::lock_guard<std::mutex> lock(input_mutex_);
  queued_inputs_.push_back(input);
}

void RuntimeInstance::set_paused(bool paused) {
  paused_.store(paused);
}

std::string RuntimeInstance::build_status_json() {
  core::SimStatus status;
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    status = latest_status_;
  }
  std::string out = "{\"stage\":\"S3\",\"abi\":";
  out += std::to_string(kExpectedAbi);
  append_bool(out, "ready", status.ready);
  append_uint(out, "tick", status.tick);
  append_uint(out, "loaded_chunks", status.loaded_chunks);
  append_uint(out, "total_chunks", status.total_chunks);
  append_uint(out, "loading_percent", status.loading_percent);
  append_uint(out, "loaded_sections", status.loaded_sections);
  append_uint(out, "mesh_sections", status.mesh_sections);
  append_uint(out, "block_count", status.block_count);
  append_number(out, "measured_tps", status.measured_tps);
  char first_number[64];
  std::snprintf(first_number, sizeof(first_number), "%.6f", status.player.x);
  out += ",\"player\":{\"x\":";
  out += first_number;
  append_number(out, "y", status.player.y);
  append_number(out, "z", status.player.z);
  append_number(out, "vx", status.player.vx);
  append_number(out, "vy", status.player.vy);
  append_number(out, "vz", status.player.vz);
  append_number(out, "yaw", status.player.yaw);
  append_number(out, "pitch", status.player.pitch);
  append_bool(out, "on_ground", status.player.on_ground);
  append_bool(out, "in_water", status.player.in_water);
  append_uint(out, "flags", status.player.flags);
  out += "},\"samples\":[";
  for (size_t i = 0; i < status.samples.size(); ++i) {
    if (i != 0) out += ',';
    out += '{';
    out += "\"x\":" + std::to_string(status.samples[i].x);
    out += ",\"y\":" + std::to_string(status.samples[i].y);
    out += ",\"z\":" + std::to_string(status.samples[i].z);
    out += ",\"block\":\"" + escape_json(status.samples[i].name) + "\"";
    out += '}';
  }
  out += "]}";
  return out;
}

int32_t RuntimeInstance::get_status_json(char* buffer, uint32_t buffer_size) {
  const std::string text = build_status_json();
  const size_t length = text.size();
  if (!buffer || buffer_size == 0) return static_cast<int32_t>(length + 1);
  if (buffer_size <= length) return REIM_ERR_INVALID_ARGUMENT;
  std::memcpy(buffer, text.c_str(), length + 1);
  return static_cast<int32_t>(length);
}

void RuntimeInstance::log_message(int32_t level, const char* category,
                                  const std::string& message) {
  if (host_.log) {
    host_.log(level, category, message.c_str());
    return;
  }
  reim::log::write(static_cast<reim::log::Level>(level), category, message);
}

} // namespace reim::runtime
