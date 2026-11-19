#include "src/runtime_instance.h"

#include "data/generated_manifest.h"
#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

namespace reim::runtime {
namespace {

constexpr uint32_t kExpectedAbi = REIM_ABI_VERSION;
constexpr const char* kExpectedProfile = "26.3";
constexpr auto kTickPeriod = std::chrono::milliseconds(50);

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
  sim_config.mouse_sensitivity =
      config->mouse_sensitivity > 0.0f ? config->mouse_sensitivity : 0.15f;
  sim_config.invert_y = config->invert_y != 0;

  // Route the core library's log lines to the Host callback as well. This is
  // registered for the lifetime of the instance and is safe with multiple
  // instances (each Host receives every line).
  register_host(host);

  auto* instance = new RuntimeInstance();
  instance->host_ = host;
  instance->render_distance_ = sim_config.render_distance;
  if (!instance->simulation_.open(sim_config, error)) {
    if (host && host->log) {
      const std::string message = "create: " + error;
      host->log(REIM_LOG_ERROR, "runtime", message.c_str());
    }
    *out_error = error.rfind("generated", 0) == 0 ? REIM_ERR_PARSE : REIM_ERR_NO_WORLD;
    unregister_host(host);
    delete instance;
    return nullptr;
  }
  instance->registered_host_ = host;
  *out_error = REIM_OK;
  return instance;
}

RuntimeInstance::~RuntimeInstance() {
  stop();
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

  const auto now = std::chrono::steady_clock::now();
  float frame_ms = 0.0f;
  if (last_fetch_.time_since_epoch().count() != 0) {
    frame_ms = std::chrono::duration<float, std::milli>(now - last_fetch_).count();
  }
  last_fetch_ = now;

  std::lock_guard<std::mutex> lock(frame_mutex_);
  std::swap(pending_, published_);
  pending_.keep.clear();
  pending_.updates.clear();
  pending_.removes.clear();
  pending_.status = published_.status;

  FrameState& frame = published_;
  frame.frame_id = ++frame_counter_;
  const core::SimStatus& status = frame.status;

  std::memset(out, 0, sizeof(*out));
  out->struct_size = sizeof(*out);
  out->abi_version = kExpectedAbi;
  out->frame_id = frame.frame_id;
  out->tick = status.tick;
  out->partial_tick = partial_tick_.load();
  out->ready = status.ready ? 1u : 0u;
  out->loaded_chunks = status.loaded_chunks;
  out->total_chunks = status.total_chunks;
  out->loading_percent = status.loading_percent;

  out->camera.position.x = static_cast<float>(status.player.x);
  out->camera.position.y =
      static_cast<float>(status.player.y + core::kPlayerEyeHeight);
  out->camera.position.z = static_cast<float>(status.player.z);
  out->camera.yaw = status.player.yaw;
  out->camera.pitch = status.player.pitch;
  out->camera.fov_y_degrees = 70.0f;
  out->camera.near_z = 0.05f;
  out->camera.far_z = static_cast<float>(render_distance_ + 1) * 16.0f;
  out->camera.dimension = 0;

  out->player.position.x = static_cast<float>(status.player.x);
  out->player.position.y = static_cast<float>(status.player.y);
  out->player.position.z = static_cast<float>(status.player.z);
  out->player.velocity.x = static_cast<float>(status.player.vx);
  out->player.velocity.y = static_cast<float>(status.player.vy);
  out->player.velocity.z = static_cast<float>(status.player.vz);
  out->player.yaw = status.player.yaw;
  out->player.pitch = status.player.pitch;
  out->player.eye_height = static_cast<float>(core::kPlayerEyeHeight);
  out->player.half_width = static_cast<float>(core::kPlayerHalfWidth);
  out->player.height = static_cast<float>(core::kPlayerHeight);
  out->player.flags = status.player.flags;

  out->environment.sky_color = 0x78A7FFu;
  out->environment.fog_color = 0xB0C8FFu;
  out->environment.fog_start = out->camera.far_z * 0.6f;
  out->environment.fog_end = out->camera.far_z;

  out->update_count = static_cast<uint32_t>(frame.updates.size());
  out->remove_count = static_cast<uint32_t>(frame.removes.size());
  out->updates = frame.updates.empty() ? nullptr : frame.updates.data();
  out->removes = frame.removes.empty() ? nullptr : frame.removes.data();
  out->loaded_sections = status.loaded_sections;
  out->mesh_sections = status.mesh_sections;
  out->block_count = status.block_count;
  out->measured_tps = status.measured_tps;
  out->measured_frame_ms = frame_ms;
  out->third_person_distance = status.third_person_distance;
  return REIM_OK;
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
  if (host_ && host_->log) {
    host_->log(level, category, message.c_str());
    return;
  }
  reim::log::write(static_cast<reim::log::Level>(level), category, message);
}

} // namespace reim::runtime
