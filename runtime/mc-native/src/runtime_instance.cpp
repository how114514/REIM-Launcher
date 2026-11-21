#include "src/runtime_instance.h"

#include "asset/packs.h"

#include "data/generated_manifest.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/log.h"
#include "util/png.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace reim::runtime {
namespace {

constexpr uint32_t kExpectedAbi = REIM_ABI_VERSION;
constexpr const char* kExpectedProfile = "26.3";
constexpr const char* kBuildId = "reim-runtime-26.3-abi3";
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

void fill_player(const core::Player& player, uint32_t gamemode,
                 uint32_t difficulty, uint32_t dimension,
                 const game::GameView* view, ReimPlayerSnapshot& out) {
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

  /* v3 attributes are owned by Gameplay's survival state (M3-F1). */
  out.health = view != nullptr ? view->health : 20.0f;
  out.max_health = view != nullptr ? view->max_health : 20.0f;
  out.food = view != nullptr ? view->food : 20.0f;
  out.saturation = view != nullptr ? view->saturation : 5.0f;
  out.exhaustion = view != nullptr ? view->exhaustion : 0.0f;
  out.air = view != nullptr ? view->air : 300.0f;
  out.max_air = view != nullptr ? view->max_air : 300.0f;
  out.xp_progress = view != nullptr ? view->xp_progress : 0.0f;
  out.xp_level = view != nullptr ? view->xp_level : 0u;
  out.xp_total = view != nullptr ? view->xp_total : 0u;
  out.gamemode = gamemode;
  out.difficulty = difficulty;
  out.dimension = dimension;
  out.selected_slot =
      view != nullptr ? view->selected_slot : 0u;
  out.state_flags = 0;
  if (player.on_ground) out.flags |= REIM_PLAYER_ON_GROUND;
  if (player.in_water) {
    out.flags |= REIM_PLAYER_IN_WATER;
    out.state_flags |= REIM_PLAYER_STATE_IN_WATER;
  }
  if ((player.flags & 0x4u) != 0) out.flags |= REIM_PLAYER_SPRINTING;
  if (view != nullptr) {
    if (view->use_held) out.state_flags |= REIM_PLAYER_STATE_USING_ITEM;
    if (view->dead) out.state_flags |= REIM_PLAYER_STATE_DEAD;
    if (view->on_fire) out.state_flags |= REIM_PLAYER_STATE_ON_FIRE;
    if (view->health < view->max_health) {
      out.state_flags |= REIM_PLAYER_STATE_HURT;
    }
  }
  out.use_ticks = view != nullptr ? view->use_ticks : 0u;
  out.held_item.struct_size = sizeof(ReimItemStack);
  out.offhand_item.struct_size = sizeof(ReimItemStack);
  if (view != nullptr) game::to_contract(view->held, out.held_item);
}

void fill_interaction(const game::GameView* view, ReimInteractionSnapshot& out) {
  out.struct_size = sizeof(ReimInteractionSnapshot);
  out.reach_distance = 4.5f;
  out.attack_cooldown = 1.0f;
  if (view == nullptr) {
    out.target_type = REIM_INTERACTION_NONE;
    return;
  }
  const game::InteractionState& state = view->interaction;
  out.target_type = state.target_type;
  out.block_x = state.x;
  out.block_y = state.y;
  out.block_z = state.z;
  out.block_state_id = state.block_id;
  out.block_face = state.face;
  out.hit_x = state.hit_x;
  out.hit_y = state.hit_y;
  out.hit_z = state.hit_z;
  out.entity_id = state.entity_id;
  out.reach_distance = state.reach_distance;
  out.break_progress = state.break_progress;
  out.can_break = state.can_break ? 1u : 0u;
  out.can_place = state.can_place ? 1u : 0u;
  out.attack_cooldown = state.attack_cooldown;
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

ReimPartTransform part_transform(float pivot_x, float pivot_y, float pivot_z,
                                 float rx = 0.0f, float ry = 0.0f,
                                 float rz = 0.0f) {
  ReimPartTransform part{};
  part.pivot[0] = pivot_x;
  part.pivot[1] = pivot_y;
  part.pivot[2] = pivot_z;
  part.rotation[0] = rx;
  part.rotation[1] = ry;
  part.rotation[2] = rz;
  part.scale[0] = 1.0f;
  part.scale[1] = 1.0f;
  part.scale[2] = 1.0f;
  return part;
}

/* Part order matches game::entity_model_defs_json() (M3-F8). The Runtime bakes
 * the animation; the Host only interpolates and draws (M3-D06). */
std::vector<ReimPartTransform> entity_part_transforms(const game::EntityView& e) {
  const float swing = std::sin(e.limb_swing) * e.limb_amount;
  const float swing_deg = swing * 45.0f;
  std::vector<ReimPartTransform> parts;
  switch (e.type) {
    case game::EntityType::zombie:
    case game::EntityType::skeleton:
      parts = {
          part_transform(0.0f, 1.55f, 0.0f, e.head_pitch, e.head_yaw, 0.0f),
          part_transform(0.0f, 1.15f, 0.0f),
          part_transform(0.35f, 1.4f, 0.0f, -swing_deg, 0.0f, 0.0f),
          part_transform(-0.35f, 1.4f, 0.0f, swing_deg, 0.0f, 0.0f),
          part_transform(0.15f, 0.6f, 0.0f, swing_deg, 0.0f, 0.0f),
          part_transform(-0.15f, 0.6f, 0.0f, -swing_deg, 0.0f, 0.0f),
      };
      break;
    case game::EntityType::creeper:
      parts = {
          part_transform(0.0f, 1.45f, 0.0f, e.head_pitch, e.head_yaw, 0.0f),
          part_transform(0.0f, 0.95f, 0.0f),
          part_transform(0.15f, 0.3f, 0.15f, swing_deg, 0.0f, 0.0f),
          part_transform(-0.15f, 0.3f, 0.15f, -swing_deg, 0.0f, 0.0f),
          part_transform(0.15f, 0.3f, -0.15f, -swing_deg, 0.0f, 0.0f),
          part_transform(-0.15f, 0.3f, -0.15f, swing_deg, 0.0f, 0.0f),
      };
      break;
    case game::EntityType::spider: {
      parts.push_back(part_transform(0.0f, 0.6f, 0.7f, e.head_pitch, e.head_yaw, 0.0f));
      parts.push_back(part_transform(0.0f, 0.55f, 0.0f));
      for (int i = 0; i < 8; ++i) {
        const double side = (i % 2 == 0) ? 1.0 : -1.0;
        const double z = 0.35 - static_cast<double>(i / 2) * 0.25;
        const float phase = static_cast<float>((i / 2) % 2 == 0 ? swing_deg : -swing_deg);
        parts.push_back(part_transform(static_cast<float>(side * 0.45), 0.45f,
                                       static_cast<float>(z), 0.0f, 0.0f,
                                       phase * (i % 2 == 0 ? 1.0f : -1.0f)));
      }
      break;
    }
    case game::EntityType::cow:
    case game::EntityType::pig:
    case game::EntityType::sheep:
      parts = {
          part_transform(0.0f, 0.95f, 0.55f, e.head_pitch, e.head_yaw, 0.0f),
          part_transform(0.0f, 0.75f, 0.0f),
          part_transform(0.22f, 0.35f, 0.3f, swing_deg, 0.0f, 0.0f),
          part_transform(-0.22f, 0.35f, 0.3f, -swing_deg, 0.0f, 0.0f),
          part_transform(0.22f, 0.35f, -0.3f, -swing_deg, 0.0f, 0.0f),
          part_transform(-0.22f, 0.35f, -0.3f, swing_deg, 0.0f, 0.0f),
      };
      break;
    case game::EntityType::chicken:
      parts = {
          part_transform(0.0f, 0.6f, 0.25f, e.head_pitch, e.head_yaw, 0.0f),
          part_transform(0.0f, 0.35f, 0.0f),
          part_transform(0.22f, 0.38f, 0.0f, 0.0f, 0.0f, swing_deg),
          part_transform(-0.22f, 0.38f, 0.0f, 0.0f, 0.0f, -swing_deg),
          part_transform(0.1f, 0.15f, 0.0f, swing_deg, 0.0f, 0.0f),
          part_transform(-0.1f, 0.15f, 0.0f, -swing_deg, 0.0f, 0.0f),
      };
      break;
  }
  return parts;
}

uint32_t audio_category_for_sound(uint32_t id) {
  switch (id) {
    case game::kSoundPlayerHurt:
    case game::kSoundPlayerDeath:
    case game::kSoundPickup:
    case game::kSoundXpPickup:
      return REIM_AUDIO_CATEGORY_PLAYERS;
    case game::kSoundExplosion:
    case game::kSoundBlockBreak:
    case game::kSoundBlockPlace:
    case game::kSoundFootstep:
    case game::kSoundBlockHit:
      return REIM_AUDIO_CATEGORY_BLOCKS;
    case game::kSoundWeatherRain:
    case game::kSoundThunder:
      return REIM_AUDIO_CATEGORY_WEATHER;
    case game::kSoundAmbientCave:
      return REIM_AUDIO_CATEGORY_AMBIENT;
    default:
      return REIM_AUDIO_CATEGORY_CREATURES;
  }
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
  if (config->render_level > 2) {
    *out_error = REIM_ERR_CAPABILITY;
    if (host && host->log) {
      const std::string message =
          "create: render_level " + std::to_string(config->render_level) +
          " is not supported (M3 negotiates R0/R1/R2)";
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
  const uint32_t generated_schema = manifest.format;
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

  const bool create_world = config->create_world != 0;
  const std::filesystem::path world_path(config->world_path);
  if (!create_world &&
      (!reim::file::is_directory(world_path) ||
       !reim::file::exists(world_path / "level.dat"))) {
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
  sim_config.create_world = create_world;
  sim_config.seed = config->seed;
  if (config->scenario != nullptr) sim_config.scenario_json = config->scenario;
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
  instance->render_level_ = config->render_level;
  if (!reim::asset::parse_pack_list(config->resource_packs,
                                    instance->resource_pack_paths_, error) ||
      !reim::asset::parse_pack_list(config->data_packs, instance->data_pack_paths_,
                                    error)) {
    if (host->log) {
      const std::string message = "create: pack list rejected: " + error;
      host->log(REIM_LOG_ERROR, "pack", message.c_str());
    }
    *out_error = REIM_ERR_INVALID_ARGUMENT;
    delete instance;
    return nullptr;
  }
  instance->render_distance_ = sim_config.render_distance;
  instance->generated_schema_version_ = generated_schema;
  instance->channel_spike_ = (config->flags & REIM_CONFIG_FLAG_CHANNEL_SPIKE) != 0;
  instance->ui_spike_enabled_ = (config->flags & REIM_CONFIG_FLAG_UI_SPIKE) != 0;
  instance->gamemode_ = config->gamemode <= REIM_GAMEMODE_SPECTATOR
                            ? config->gamemode
                            : REIM_GAMEMODE_SURVIVAL;
  instance->difficulty_ = config->difficulty <= REIM_DIFFICULTY_HARD
                              ? config->difficulty
                              : REIM_DIFFICULTY_NORMAL;
  instance->debug_controls_ = config->scenario != nullptr;
  {
    const uint32_t scale = config->ui_scale == 0
                               ? 2u
                               : std::clamp<uint32_t>(config->ui_scale, 1, 8);
    instance->ui_scale_.store(static_cast<float>(scale));
  }
  instance->save_status_.struct_size = sizeof(ReimSaveStatus);
  instance->save_status_.state = REIM_SAVE_IDLE;
  instance->save_status_.last_result = REIM_OK;
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
  if (!instance->gameplay_.initialize(instance->simulation_,
                                      reim::file::to_utf8(generated_dir), error)) {
    if (host->log) {
      const std::string message = "create: gameplay init failed: " + error;
      host->log(REIM_LOG_ERROR, "gameplay", message.c_str());
    }
    unregister_host(instance->registered_host_);
    delete instance;
    *out_error = REIM_ERR_PARSE;
    return nullptr;
  }
  instance->gameplay_.set_rules(instance->gamemode_, instance->difficulty_,
                                config->seed);
  if (config->scenario != nullptr) {
    if (!instance->gameplay_.configure_scenario(config->scenario, error)) {
      if (host->log) {
        const std::string message = "create: scenario config rejected: " + error;
        host->log(REIM_LOG_ERROR, "scenario", message.c_str());
      }
      unregister_host(instance->registered_host_);
      delete instance;
      *out_error = REIM_ERR_INVALID_ARGUMENT;
      return nullptr;
    }
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
  if (!instance->build_contract_assets(error)) {
    if (host->log) {
      const std::string message = "create: contract assets rejected: " + error;
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
  gameplay_.apply_setup(simulation_.player());
  update_contract_channels();

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
      for (const core::SimInput& input : inputs) {
        simulation_.apply_input(input);
        if (input.type == REIM_INPUT_KEY) {
          gameplay_.on_key(input.key, input.action != REIM_ACTION_RELEASE,
                           simulation_.player());
        }
      }
      process_actions();
      simulation_.tick(*this);
      gameplay_.tick(simulation_.player_mut());
      const int dimension_request = gameplay_.take_dimension_request();
      if (dimension_request >= 0) {
        std::string dimension_error;
        if (!simulation_.switch_dimension(dimension_request,
                                          simulation_.player_mut(),
                                          dimension_error)) {
          log_message(REIM_LOG_WARN, "dimension",
                      "portal transition rejected: " + dimension_error);
        } else {
          gameplay_.on_dimension_changed();
          log_message(REIM_LOG_INFO, "dimension",
                      "entered " + std::string(
                          dimension_request == 1 ? "minecraft:the_nether"
                                                 : "minecraft:overworld"));
        }
      }
      update_contract_channels();
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

  /* v3 list limits. Gameplay-critical lists fail the frame; the audio list is
   * allowed to drop the oldest non-critical ambient events (M3_PLAN 7.8). */
  const size_t entity_count = frame.entities.size();
  size_t part_total = 0;
  for (const std::vector<ReimPartTransform>& parts : frame.entity_parts) {
    part_total += parts.size();
  }
  const size_t particle_count = frame.particles.size();
  const size_t ui_draw_count = frame.ui.draws.size();
  const size_t ui_text_bytes = frame.ui.text.size();
  const size_t ui_slot_count = frame.ui.slots.size();
  size_t audio_count = frame.audio.size();
  if (entity_count > REIM_MAX_FRAME_ENTITIES) {
    return reject(REIM_ERR_LIMIT, "entity count exceeds REIM_MAX_FRAME_ENTITIES");
  }
  if (part_total > REIM_MAX_FRAME_PART_TRANSFORMS) {
    return reject(REIM_ERR_LIMIT,
                  "entity part count exceeds REIM_MAX_FRAME_PART_TRANSFORMS");
  }
  if (particle_count > REIM_MAX_FRAME_PARTICLES) {
    return reject(REIM_ERR_LIMIT, "particle count exceeds REIM_MAX_FRAME_PARTICLES");
  }
  if (ui_draw_count > REIM_MAX_UI_DRAWS) {
    return reject(REIM_ERR_LIMIT, "UI draw count exceeds REIM_MAX_UI_DRAWS");
  }
  if (ui_text_bytes > REIM_MAX_UI_TEXT_BYTES) {
    return reject(REIM_ERR_LIMIT, "UI text exceeds REIM_MAX_UI_TEXT_BYTES");
  }
  if (audio_count > REIM_MAX_FRAME_AUDIO_EVENTS) {
    audio_count = REIM_MAX_FRAME_AUDIO_EVENTS;
    log_message(REIM_LOG_WARN, "frame",
                "fetch_frame: audio event limit reached; oldest events dropped");
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

  const size_t entities_off = align_up(offset, kBlobAlignment);
  offset = entities_off + entity_count * sizeof(ReimEntityRender);
  std::vector<size_t> part_off(entity_count, 0);
  for (size_t i = 0; i < entity_count; ++i) {
    part_off[i] = align_up(offset, kBlobAlignment);
    offset = part_off[i] +
             frame.entity_parts[i].size() * sizeof(ReimPartTransform);
  }
  const size_t particles_off = align_up(offset, kBlobAlignment);
  offset = particles_off + particle_count * sizeof(ReimParticle);
  const size_t ui_draws_off = align_up(offset, REIM_UI_DRAW_ALIGN);
  offset = ui_draws_off + ui_draw_count * sizeof(ReimUiDraw);
  const size_t ui_text_off = align_up(offset, alignof(uint8_t));
  offset = ui_text_off + ui_text_bytes;
  const size_t ui_slots_off = align_up(offset, kBlobAlignment);
  offset = ui_slots_off + ui_slot_count * sizeof(ReimInventorySlot);
  const size_t audio_off = align_up(offset, kBlobAlignment);
  offset = audio_off + audio_count * sizeof(ReimAudioEvent);

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

  auto* out_entities =
      reinterpret_cast<ReimEntityRender*>(base + entities_off);
  for (size_t i = 0; i < entity_count; ++i) {
    out_entities[i] = frame.entities[i];
    out_entities[i].parts = nullptr;
    const std::vector<ReimPartTransform>& parts = frame.entity_parts[i];
    if (!parts.empty()) {
      auto* dst = reinterpret_cast<ReimPartTransform*>(base + part_off[i]);
      std::memcpy(dst, parts.data(), parts.size() * sizeof(ReimPartTransform));
      out_entities[i].parts = dst;
    }
  }
  if (particle_count != 0) {
    std::memcpy(base + particles_off, frame.particles.data(),
                particle_count * sizeof(ReimParticle));
  }
  auto* out_ui_draws = reinterpret_cast<ReimUiDraw*>(base + ui_draws_off);
  if (ui_draw_count != 0) {
    std::memcpy(out_ui_draws, frame.ui.draws.data(),
                ui_draw_count * sizeof(ReimUiDraw));
  }
  if (ui_text_bytes != 0) {
    std::memcpy(base + ui_text_off, frame.ui.text.data(), ui_text_bytes);
  }
  auto* out_ui_slots =
      reinterpret_cast<ReimInventorySlot*>(base + ui_slots_off);
  if (ui_slot_count != 0) {
    std::memcpy(out_ui_slots, frame.ui.slots.data(),
                ui_slot_count * sizeof(ReimInventorySlot));
  }
  auto* out_audio = reinterpret_cast<ReimAudioEvent*>(base + audio_off);
  if (audio_count != 0) {
    std::memcpy(out_audio, frame.audio.data(),
                audio_count * sizeof(ReimAudioEvent));
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

  const game::GameView* view = frame.has_view ? &frame.view : nullptr;
  fill_camera(status.player, static_cast<float>(render_distance_), out->camera);
  fill_player(status.player, gamemode_, difficulty_, status.dimension, view,
              out->player);
  out->camera.dimension = status.dimension;

  /* Interpolation snapshot: prev_* is the previous published tick. When the
   * simulation has not advanced (pause/loading) alpha stays 0 and prev equals
   * the current snapshot (reim_render.h). */
  bool advanced = has_prev_status_ && status.tick > prev_status_.tick;
  const core::Player& prev_player = advanced ? prev_status_.player : status.player;
  fill_camera(prev_player, static_cast<float>(render_distance_), out->prev_camera);
  out->prev_camera.dimension = status.dimension;
  fill_player(prev_player, gamemode_, difficulty_, status.dimension, view,
              out->prev_player);
  out->tick_alpha =
      (advanced && !paused_.load()) ? std::clamp(partial_tick_.load(), 0.0f, 1.0f)
                                    : 0.0f;

  out->environment.fog_start = out->camera.far_z * 0.6f;
  out->environment.fog_end = out->camera.far_z;
  const float darken = std::clamp(status.sky_darken, 0.0f, 1.0f);
  if (status.dimension == REIM_DIMENSION_NETHER) {
    out->environment.sky_color = 0x5A1A0Au;
    out->environment.fog_color = 0x3A1008u;
  } else if (status.dimension == REIM_DIMENSION_END) {
    out->environment.sky_color = 0x0A0810u;
    out->environment.fog_color = 0x140A20u;
  } else {
    const float factor = 1.0f - darken * 0.75f;
    const uint32_t r = static_cast<uint32_t>(0x78 * factor);
    const uint32_t g = static_cast<uint32_t>(0xA7 * factor);
    const uint32_t b = static_cast<uint32_t>(0xFF * factor);
    out->environment.sky_color = (r << 16) | (g << 8) | b;
    const uint32_t fr = static_cast<uint32_t>(0xB0 * factor);
    const uint32_t fg = static_cast<uint32_t>(0xC8 * factor);
    const uint32_t fb = static_cast<uint32_t>(0xFF * factor);
    out->environment.fog_color = (fr << 16) | (fg << 8) | fb;
  }
  out->environment.time_of_day = status.time_of_day;
  out->environment.day = status.day;
  out->environment.moon_phase = status.day % 8u;
  out->environment.weather = status.weather;
  out->environment.rain_level = status.weather > 0 ? 1000u : 0u;
  out->environment.thunder_level = status.weather == 2 ? 1000u : 0u;
  out->environment.dimension = status.dimension;
  out->environment.sky_darken = darken;
  out->environment.sun_angle = status.sun_angle;
  out->environment.ambient_light = status.dimension == 0
                                       ? 1.0f - darken * 0.85f
                                       : 0.15f;

  /* v3 interaction snapshot: the Runtime raycast result for this tick. */
  fill_interaction(view, out->interaction);

  out->entity_count = static_cast<uint32_t>(entity_count);
  out->entities = entity_count == 0 ? nullptr : out_entities;
  out->particle_count = static_cast<uint32_t>(particle_count);
  out->particles = particle_count == 0 ? nullptr
                                       : reinterpret_cast<const ReimParticle*>(
                                             base + particles_off);

  ReimUiFrame& ui = out->ui;
  ui.struct_size = sizeof(ReimUiFrame);
  ui.screen_id = frame.ui.screen_id;
  ui.screen_revision = frame.ui.revision;
  ui.ui_scale = frame.ui.ui_scale;
  ui.canvas_width = frame.ui.canvas_width;
  ui.canvas_height = frame.ui.canvas_height;
  ui.cursor_mode = frame.ui.cursor_mode;
  ui.cursor_x = frame.ui.cursor_x;
  ui.cursor_y = frame.ui.cursor_y;
  ui.draw_count = static_cast<uint32_t>(ui_draw_count);
  ui.draws = ui_draw_count == 0 ? nullptr : out_ui_draws;
  ui.text_bytes = static_cast<uint32_t>(ui_text_bytes);
  ui.text = ui_text_bytes == 0
                ? nullptr
                : reinterpret_cast<const char*>(base + ui_text_off);
  ui.inventory.struct_size = sizeof(ReimInventorySnapshot);
  ui.inventory.screen_id = frame.ui.screen_id;
  ui.inventory.slot_count = static_cast<uint32_t>(ui_slot_count);
  ui.inventory.selected_slot = frame.ui.selected_slot;
  ui.inventory.cursor_item_present = frame.ui.cursor_item_present;
  ui.inventory.cursor_item.struct_size = sizeof(ReimItemStack);
  if (view != nullptr && !view->cursor.empty()) {
    game::to_contract(view->cursor, ui.inventory.cursor_item);
    ui.inventory.cursor_item_present = 1;
  }
  ui.inventory.slots = ui_slot_count == 0 ? nullptr : out_ui_slots;
  ui.hovered_element = frame.ui.hovered_element;
  ui.hovered_slot = frame.ui.hovered_slot;
  ui.focused_element = frame.ui.focused_element;

  out->audio_count = static_cast<uint32_t>(audio_count);
  out->audio_events = audio_count == 0 ? nullptr : out_audio;

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
  /* Mesh updates are incremental; UI/entity/particle lists are level-triggered
   * and must be visible to every fetch until the next tick replaces them.
   * Audio keeps its sequence numbers so a Host can play each event once. */
  frame.keep.clear();
  frame.updates.clear();
  frame.removes.clear();
  pending_.keep.clear();
  pending_.updates.clear();
  pending_.removes.clear();
  pending_.ui = frame.ui;
  pending_.view = frame.view;
  pending_.has_view = frame.has_view;
  pending_.entities = frame.entities;
  pending_.entity_parts = frame.entity_parts;
  pending_.particles = frame.particles;
  pending_.audio = frame.audio;
  frame.entities.clear();
  frame.entity_parts.clear();
  frame.particles.clear();
  frame.audio.clear();
  frame.ui = ui::UiSpike::DrawList{};
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
    textures_json_ = textures_text;
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

  auto parse_generated_json = [&](const char* name, nlohmann::json& out) -> bool {
    std::string text;
    if (!reim::file::read_text(generated_dir / name, text, error)) return false;
    out = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
      error = std::string(name) + " is not a JSON object";
      return false;
    }
    return true;
  };

  /* K5: the Runtime loads the v3 worldgen definitions and checks the header.
   * Generation remains the native profile until a data profile is wired; the
   * data is still validated so a mismatched/empty file fails startup. */
  nlohmann::json worldgen;
  if (!parse_generated_json("worldgen.json", worldgen)) return false;
  const uint64_t worldgen_count = worldgen.value("definition_count", 0ull);
  if (worldgen_count == 0) {
    error = "worldgen.json contains no definitions";
    return false;
  }
  worldgen_summary_json_ = nlohmann::json{
      {"schema", worldgen.value("schema", "")},
      {"format", worldgen.value("format", 0)},
      {"definition_count", worldgen_count},
      {"runtime_loader", worldgen.value("runtime_loader", "")},
  }.dump();

  /* I1/I3: use the generated sound index. Sound ids map to OGG files that the
   * Converter copied under generated/assets/. Preload the M3 set so get_asset
   * never touches the disk from the render thread. */
  nlohmann::json sounds_json;
  if (parse_generated_json("sounds.json", sounds_json)) {
    sound_index_json_ = sounds_json.dump();
    const auto m3 = sounds_json.find("m3");
    if (m3 != sounds_json.end() && m3->is_array()) {
      for (const nlohmann::json& entry : *m3) {
        const uint32_t id = entry.value("id", 0u);
        if (id == 0) continue;
        const auto files = entry.find("files");
        if (files == entry.end() || !files->is_array()) continue;
        for (const nlohmann::json& item : *files) {
          if (!item.value("local", false)) continue;
          const std::string path = item.value("path", "");
          if (path.empty()) continue;
          std::vector<uint8_t> bytes;
          std::string read_error;
          if (!reim::file::read_all(generated_dir / "assets" / reim::file::from_utf8(path),
                                    bytes, read_error)) {
            continue;
          }
          sound_ogg_cache_[id] = std::move(bytes);
          break;
        }
      }
    }
  } else {
    error.clear();
  }

  nlohmann::json lang_values;
  if (parse_generated_json("lang/en_us.json", lang_values)) {
    nlohmann::json lang;
    lang["format"] = 3;
    lang["language"] = "en_us";
    lang["fallback"] = "en_us";
    lang["values"] = std::move(lang_values);
    lang_json_ = lang.dump();
  } else {
    error.clear();
  }

  if (reim::file::read_all(generated_dir / "item_atlas.png", item_atlas_png_, error)) {
    std::string item_meta;
    if (reim::file::read_text(generated_dir / "item_atlas.json", item_meta, error)) {
      item_atlas_json_ = std::move(item_meta);
    } else {
      error.clear();
      item_atlas_png_.clear();
    }
  } else {
    error.clear();
  }

  // ---- I4: resource packs (startup only, never hot-reloaded) --------------
  if (!resource_pack_paths_.empty()) {
    reim::asset::PackView view;
    if (!reim::asset::apply_resource_packs(resource_pack_paths_, lang_values,
                                           sounds_json, view, error)) {
      return false;
    }
    nlohmann::json lang;
    lang["format"] = 3;
    lang["language"] = "en_us";
    lang["fallback"] = "en_us";
    lang["values"] = view.lang_values;
    lang_json_ = lang.dump();
    sounds_json = view.sounds;
    sound_index_json_ = sounds_json.dump();
    sound_ogg_cache_.clear();
    const auto m3 = sounds_json.find("m3");
    if (m3 != sounds_json.end() && m3->is_array()) {
      for (const nlohmann::json& entry : *m3) {
        const uint32_t id = entry.value("id", 0u);
        if (id == 0) continue;
        const auto files = entry.find("files");
        if (files == entry.end() || !files->is_array()) continue;
        for (const nlohmann::json& item : *files) {
          const std::string path = item.value("path", "");
          if (path.empty()) continue;
          const auto override = view.overrides.find(path);
          if (override != view.overrides.end()) {
            sound_ogg_cache_[id] = override->second;
            break;
          }
          if (!item.value("local", false)) continue;
          std::vector<uint8_t> bytes;
          std::string read_error;
          if (!reim::file::read_all(generated_dir / "assets" / reim::file::from_utf8(path),
                                    bytes, read_error)) {
            continue;
          }
          sound_ogg_cache_[id] = std::move(bytes);
          break;
        }
      }
    }
    if (!apply_atlas_overrides(view.overrides, error)) return false;
    resource_pack_report_json_ = view.report.dump();
    for (const std::string& warning : view.warnings) {
      log_message(REIM_LOG_WARN, "pack", warning);
    }
  }

  // ---- I5: data packs (validated; gameplay data remains native) -----------
  if (!data_pack_paths_.empty()) {
    nlohmann::json report;
    if (!reim::asset::validate_data_packs(data_pack_paths_, report, error)) {
      return false;
    }
    data_pack_report_json_ = report.dump();
    log_message(REIM_LOG_WARN, "pack",
                "data packs validated; Runtime recipes/loot remain the native "
                "subset (M3-E3/E6)");
  }
  return true;
}

bool RuntimeInstance::apply_atlas_overrides(
    const std::map<std::string, std::vector<uint8_t>>& overrides,
    std::string& error) {
  if (overrides.empty()) return true;
  auto blit = [](png::Image& atlas, const png::Image& tile, int x0, int y0) {
    for (int y = 0; y < tile.height; ++y) {
      for (int x = 0; x < tile.width; ++x) {
        const uint8_t* source = tile.at(x, y);
        uint8_t* target = atlas.pixels.data() +
            (static_cast<size_t>(y0 + y) * atlas.width + x0 + x) * atlas.channels;
        for (int c = 0; c < 4; ++c) target[c] = source[c];
      }
    }
  };
  auto sprite_lookup = [](const nlohmann::json& root,
                          const std::string& name) -> const nlohmann::json* {
    const auto sprites = root.find("sprites");
    if (sprites == root.end() || !sprites->is_object()) return nullptr;
    const auto found = sprites->find(name);
    return found == sprites->end() || !found->is_object() ? nullptr : &(*found);
  };
  auto apply_region = [&](const std::string& prefix, std::vector<uint8_t>& png_bytes,
                          const std::string& sprite_table_text, const char* label) -> bool {
    nlohmann::json sprite_table = nlohmann::json::parse(
        sprite_table_text.begin(), sprite_table_text.end(), nullptr, false);
    if (sprite_table.is_discarded() || !sprite_table.is_object()) return true;
    png::Image atlas;
    std::string png_error;
    if (!png::load_memory(png_bytes.data(), png_bytes.size(), 4, atlas, png_error)) {
      error = std::string(label) + ": " + png_error;
      return false;
    }
    bool changed = false;
    for (const auto& [key, bytes] : overrides) {
      if (key.rfind(prefix, 0) != 0) continue;
      std::string sprite = key.substr(std::string("minecraft/textures/").size());
      if (sprite.size() < 4 || sprite.substr(sprite.size() - 4) != ".png") continue;
      sprite.resize(sprite.size() - 4);
      const nlohmann::json* rect = sprite_lookup(sprite_table, sprite);
      if (rect == nullptr) {
        log_message(REIM_LOG_WARN, "pack",
                    std::string("override texture not in ") + label + ": " + key);
        continue;
      }
      png::Image tile;
      std::string tile_error;
      if (!png::load_memory(bytes.data(), bytes.size(), 4, tile, tile_error)) {
        log_message(REIM_LOG_WARN, "pack",
                    std::string("override texture rejected: ") + key + ": " + tile_error);
        continue;
      }
      const double u0 = rect->value("u0", 0.0);
      const double v0 = rect->value("v0", 0.0);
      const double u1 = rect->value("u1", 0.0);
      const double v1 = rect->value("v1", 0.0);
      const int cell_w = static_cast<int>(std::llround((u1 - u0) * atlas.width));
      const int cell_h = static_cast<int>(std::llround((v1 - v0) * atlas.height));
      if (tile.width != cell_w || tile.height != cell_h) {
        log_message(REIM_LOG_WARN, "pack",
                    std::string("override texture size mismatch in ") + label + ": " +
                        key + " (" + std::to_string(tile.width) + "x" +
                        std::to_string(tile.height) + " vs " +
                        std::to_string(cell_w) + "x" + std::to_string(cell_h) + ")");
        continue;
      }
      const int x0 = static_cast<int>(std::llround(u0 * atlas.width));
      const int y0 = static_cast<int>(std::llround(v0 * atlas.height));
      blit(atlas, tile, x0, y0);
      changed = true;
    }
    if (!changed) return true;
    std::vector<uint8_t> encoded;
    if (!png::encode_memory(atlas.width, atlas.height, 4, atlas.pixels.data(), encoded,
                            error)) {
      return false;
    }
    png_bytes = std::move(encoded);
    return true;
  };

  if (!apply_region("minecraft/textures/block/", atlas_png_, textures_json_,
                    "block atlas")) {
    return false;
  }
  // The block atlas revision is part of the asset contract.
  {
    const auto digest = reim::hash::sha256(atlas_png_.data(), atlas_png_.size());
    atlas_revision_ = digest_prefix(digest);
    nlohmann::json meta = nlohmann::json::parse(atlas_meta_json_.begin(),
                                                atlas_meta_json_.end(), nullptr, false);
    if (!meta.is_discarded() && meta.is_object()) {
      meta["hash"] = reim::hash::hex(digest);
      meta["source"] = "atlas.png + resource pack overrides";
      atlas_meta_json_ = meta.dump();
    }
  }
  if (!item_atlas_png_.empty() && !item_atlas_json_.empty()) {
    if (!apply_region("minecraft/textures/item/", item_atlas_png_, item_atlas_json_,
                      "item atlas")) {
      return false;
    }
    nlohmann::json meta = nlohmann::json::parse(item_atlas_json_.begin(),
                                                item_atlas_json_.end(), nullptr, false);
    if (!meta.is_discarded() && meta.is_object()) {
      meta["sha256"] = reim::hash::hex(
          reim::hash::sha256(item_atlas_png_.data(), item_atlas_png_.size()));
      item_atlas_json_ = meta.dump();
    }
  }
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
                      REIM_CAP_ATLAS_ASSET | REIM_CAP_R0 | REIM_CAP_R1 |
                      REIM_CAP_R2 | REIM_CAP_UI | REIM_CAP_AUDIO |
                      REIM_CAP_ENTITIES | REIM_CAP_ACTIONS;
  out->render_level = render_level_;
  out->dll_abi_version = kExpectedAbi;
  out->schema_version = generated_schema_version_;
  out->max_ui_scale = 8;
  return REIM_OK;
}

int32_t RuntimeInstance::get_asset(uint32_t kind, uint32_t asset_id,
                                   const void** out_data, uint64_t* out_size,
                                   uint64_t* out_revision) {
  if (!out_data || !out_size || !out_revision) return REIM_ERR_INVALID_ARGUMENT;
  *out_data = nullptr;
  *out_size = 0;
  *out_revision = 0;

  auto text_asset = [&](const std::string& text) -> int32_t {
    if (text.empty()) return REIM_ERR_NOT_FOUND;
    *out_data = text.data();
    *out_size = text.size();
    *out_revision = 1;
    return REIM_OK;
  };
  auto bytes_asset = [&](const std::vector<uint8_t>& bytes,
                         uint64_t revision) -> int32_t {
    if (bytes.empty()) return REIM_ERR_NOT_FOUND;
    *out_data = bytes.data();
    *out_size = bytes.size();
    *out_revision = revision;
    return REIM_OK;
  };

  switch (kind) {
    case REIM_ASSET_ATLAS_PNG:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return bytes_asset(atlas_png_, atlas_revision_);
    case REIM_ASSET_ATLAS_META:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(atlas_meta_json_);
    case REIM_ASSET_FONT_ATLAS:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return bytes_asset(font_png_, 1);
    case REIM_ASSET_FONT_META:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(font_meta_json_);
    case REIM_ASSET_MODEL_DEFS_JSON:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(model_defs_json_);
    case REIM_ASSET_SOUND_INDEX_JSON:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(sound_index_json_);
    case REIM_ASSET_ITEM_INDEX_JSON:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(item_index_json_);
    case REIM_ASSET_UI_INDEX_JSON:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(ui_index_json_);
    case REIM_ASSET_LANG_JSON:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(lang_json_);
    case REIM_ASSET_SOUND_OGG: {
      const auto found = sound_ogg_cache_.find(asset_id);
      if (found == sound_ogg_cache_.end() || found->second.empty()) {
        return REIM_ERR_NOT_FOUND;
      }
      *out_data = found->second.data();
      *out_size = found->second.size();
      *out_revision = 1;
      return REIM_OK;
    }
    case REIM_ASSET_ITEM_ATLAS_PNG:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return bytes_asset(item_atlas_png_, 1);
    case REIM_ASSET_ITEM_ATLAS_META:
      if (asset_id != 0) return REIM_ERR_NOT_FOUND;
      return text_asset(item_atlas_json_);
    case REIM_ASSET_UI_ATLAS_PNG:
    case REIM_ASSET_UI_ATLAS_META:
      return REIM_ERR_NOT_FOUND;
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

bool RuntimeInstance::build_contract_assets(std::string& error) {
  font_png_ = ui_spike_.font_png();
  font_meta_json_ = ui_spike_.font_meta();
  if (font_png_.empty()) {
    error = "font atlas synthesis failed";
    return false;
  }
  {
    nlohmann::json models =
        nlohmann::json::parse(simulation_.contract_model_defs_json());
    const nlohmann::json entity_models =
        nlohmann::json::parse(game::entity_model_defs_json());
    /* Entity models are native and carry no atlas UVs. Reuse the opaque block
     * face UV of the debug model so a Host never samples an unknown sprite. */
    nlohmann::json fallback_uv = nlohmann::json::array({0.0, 0.0, 0.0078125, 0.125});
    if (models["models"].is_array() && !models["models"].empty()) {
      const nlohmann::json& debug = models["models"][0];
      if (debug.contains("parts") && debug["parts"].is_array() &&
          !debug["parts"].empty() && debug["parts"][0].contains("uv")) {
        fallback_uv = debug["parts"][0]["uv"];
      }
    }
    for (nlohmann::json model : entity_models["models"]) {
      if (model.contains("parts") && model["parts"].is_array()) {
        for (nlohmann::json& part : model["parts"]) {
          if (!part.contains("uv")) part["uv"] = fallback_uv;
        }
      }
      models["models"].push_back(std::move(model));
    }
    models["entity_source"] = entity_models["source"];
    model_defs_json_ = models.dump();
  }
  if (sound_index_json_.empty()) {
    /* Fallback for a generated directory that predates the v3 sound index. */
    sound_index_json_ =
        R"({"format":1,"sounds":[)"
        R"({"id":1,"name":"reim:ui/spike_click","category":6,"synthesized":true},)"
        R"({"id":10,"name":"reim:entity/mob_hurt","category":3,"synthesized":true},)"
        R"({"id":11,"name":"reim:entity/mob_death","category":3,"synthesized":true},)"
        R"({"id":12,"name":"reim:entity/explosion","category":2,"synthesized":true},)"
        R"({"id":13,"name":"reim:entity/player_hurt","category":7,"synthesized":true},)"
        R"({"id":14,"name":"reim:entity/attack","category":7,"synthesized":true},)"
        R"({"id":15,"name":"reim:entity/creeper_prime","category":3,"synthesized":true},)"
        R"({"id":16,"name":"reim:entity/player_death","category":7,"synthesized":true}]})";
  }
  nlohmann::json items_json;
  items_json["format"] = 1;
  std::vector<uint32_t> item_ids;
  item_ids.reserve(gameplay_.items().defs().size());
  for (const auto& [id, def] : gameplay_.items().defs()) {
    (void)def;
    item_ids.push_back(id);
  }
  std::sort(item_ids.begin(), item_ids.end());
  items_json["items"] = nlohmann::json::array();
  for (uint32_t id : item_ids) {
    const game::ItemDef& def = gameplay_.items().def(id);
    nlohmann::json entry;
    entry["id"] = id;
    entry["name"] = def.name;
    entry["display"] = def.display;
    entry["max_stack"] = def.max_stack;
    entry["max_damage"] = def.max_damage;
    entry["block_id"] = def.block_id;
    entry["flags"] = def.flags;
    items_json["items"].push_back(std::move(entry));
  }
  item_index_json_ = items_json.dump();
  if (ui_index_json_.empty()) ui_index_json_ = R"({"format":1,"elements":[]})";
  if (lang_json_.empty()) {
    lang_json_ = R"({"format":1,"language":"en_us","values":{}})";
  }
  return true;
}

void RuntimeInstance::set_ui_canvas(uint32_t width, uint32_t height, float scale) {
  if (width != 0 && height != 0) {
    ui_canvas_width_.store(std::clamp<uint32_t>(width, 64, 16384));
    ui_canvas_height_.store(std::clamp<uint32_t>(height, 64, 16384));
  }
  if (scale > 0.0f) {
    ui_scale_.store(std::clamp(scale, 1.0f, 8.0f));
  }
}

void RuntimeInstance::submit_action(const ReimActionEvent* event) {
  if (event == nullptr || event->struct_size < REIM_ACTION_EVENT_MIN_SIZE) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action",
                "submit_action: event shorter than REIM_ACTION_EVENT_MIN_SIZE");
    return;
  }
  const uint32_t type = event->type;
  const bool known =
      (type >= REIM_ACTIONTYPE_BREAK_BLOCK && type <= REIM_ACTIONTYPE_WHEEL) ||
      type == REIM_ACTIONTYPE_DEBUG_SET_TIME ||
      type == REIM_ACTIONTYPE_DEBUG_SET_WEATHER;
  if (!known) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action",
                "submit_action: unknown action type " + std::to_string(type));
    return;
  }
  if (!std::isfinite(event->x) || !std::isfinite(event->y) ||
      !std::isfinite(event->z) || !std::isfinite(event->screen_x) ||
      !std::isfinite(event->screen_y) || !std::isfinite(event->wheel)) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action", "submit_action: non-finite coordinate");
    return;
  }
  actions_submitted_.fetch_add(1);

  std::lock_guard<std::mutex> lock(action_mutex_);
  if (type == REIM_ACTIONTYPE_POINTER) {
    /* Pointer state is level-triggered: keep only the newest. */
    if (has_pending_pointer_) actions_coalesced_.fetch_add(1);
    pending_pointer_ = *event;
    has_pending_pointer_ = true;
    return;
  }
  if (queued_actions_.size() >= REIM_MAX_ACTION_QUEUE) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action",
                "submit_action: queue full, event rejected (type " +
                    std::to_string(type) + ")");
    return;
  }
  queued_actions_.push_back(*event);
}

void RuntimeInstance::process_actions() {
  const uint32_t width = ui_canvas_width_.load();
  const uint32_t height = ui_canvas_height_.load();
  const uint32_t scale = static_cast<uint32_t>(
      std::clamp(std::lround(ui_scale_.load()), 1L, 8L));
  if (width != applied_canvas_width_ || height != applied_canvas_height_ ||
      scale != applied_scale_) {
    ui_spike_.set_canvas(width, height, scale);
    applied_canvas_width_ = width;
    applied_canvas_height_ = height;
    applied_scale_ = scale;
  }
  gameplay_.set_canvas(width, height, scale);

  std::deque<ReimActionEvent> actions;
  {
    std::lock_guard<std::mutex> lock(action_mutex_);
    actions.swap(queued_actions_);
    if (has_pending_pointer_) {
      actions.push_back(pending_pointer_);
      has_pending_pointer_ = false;
    }
  }
  for (const ReimActionEvent& action : actions) apply_action(action);
}

bool RuntimeInstance::apply_action(const ReimActionEvent& event) {
  const uint64_t last = last_action_sequence_.load();
  if (event.sequence == 0 || event.sequence <= last) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action",
                "action rejected: non-monotonic sequence " +
                    std::to_string(event.sequence));
    return false;
  }
  last_action_sequence_.store(event.sequence);

  const bool screen_specific =
      event.type == REIM_ACTIONTYPE_SLOT_CLICK ||
      event.type == REIM_ACTIONTYPE_DRAG_START ||
      event.type == REIM_ACTIONTYPE_DRAG_TO ||
      event.type == REIM_ACTIONTYPE_DRAG_END ||
      event.type == REIM_ACTIONTYPE_SELECT_SLOT ||
      event.type == REIM_ACTIONTYPE_WHEEL;
  const uint32_t current_screen =
      ui_spike_enabled_ ? ui_spike_.screen_id() : gameplay_.screen();
  if (screen_specific && event.screen_id != 0 &&
      event.screen_id != current_screen) {
    actions_rejected_.fetch_add(1);
    log_message(REIM_LOG_WARN, "action",
                "action rejected: screen_id " +
                    std::to_string(event.screen_id) + " is not current");
    return false;
  }

  auto apply_gameplay = [&](const char* name) {
    std::string game_error;
    if (!gameplay_.handle_action(event, simulation_.player_mut(), game_error)) {
      actions_rejected_.fetch_add(1);
      log_message(REIM_LOG_WARN, "action",
                  std::string(name) + " rejected: " + game_error);
      return false;
    }
    return true;
  };

  switch (event.type) {
    case REIM_ACTIONTYPE_POINTER: {
      if (!ui_spike_enabled_) {
        if (!apply_gameplay("pointer")) return false;
        break;
      }
      const uint32_t previous = static_cast<uint32_t>(pointer_buttons_.load());
      const uint32_t buttons = event.c & 0x00FFu;
      const bool consumed = ui_spike_.pointer(event.screen_x, event.screen_y,
                                              buttons, previous);
      pointer_buttons_.store(buttons);
      if (consumed) queue_audio(1, REIM_AUDIO_CATEGORY_UI, true, 0.8f);
      break;
    }
    case REIM_ACTIONTYPE_SLOT_CLICK:
      if (!ui_spike_enabled_) {
        if (!apply_gameplay("slot_click")) return false;
        break;
      }
      if (!ui_spike_.slot_click(event.a, event.b)) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "slot click rejected: slot " + std::to_string(event.a) +
                        " button " + std::to_string(event.b));
        return false;
      }
      break;
    case REIM_ACTIONTYPE_SELECT_SLOT:
      if (event.a < 0 || event.a > 8) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "select_slot rejected: slot " + std::to_string(event.a));
        return false;
      }
      if (ui_spike_enabled_) {
        ui_spike_.slot_click(event.a, 0);
      } else if (!apply_gameplay("select_slot")) {
        return false;
      }
      break;
    case REIM_ACTIONTYPE_WHEEL: {
      const int direction = event.wheel > 0.0f ? 1 : (event.wheel < 0.0f ? -1 : 0);
      if (direction == 0) break;
      if (ui_spike_enabled_) {
        const int slot = (static_cast<int>(ui_spike_.selected_slot()) + direction +
                          9) % 9;
        ui_spike_.slot_click(slot, 0);
      } else if (!apply_gameplay("wheel")) {
        return false;
      }
      action_wheel_.fetch_add(1);
      break;
    }
    case REIM_ACTIONTYPE_BREAK_START:
      if (break_held_) {
        actions_coalesced_.fetch_add(1);
        return true;
      }
      break_held_ = true;
      if (!apply_gameplay("break_start")) return false;
      break;
    case REIM_ACTIONTYPE_BREAK_STOP:
      if (!break_held_) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "break_stop without break_start rejected");
        return false;
      }
      break_held_ = false;
      if (!apply_gameplay("break_stop")) return false;
      break;
    case REIM_ACTIONTYPE_USE_START:
      if (use_held_) {
        actions_coalesced_.fetch_add(1);
        return true;
      }
      use_held_ = true;
      if (!apply_gameplay("use_start")) return false;
      break;
    case REIM_ACTIONTYPE_USE_STOP:
      if (!use_held_) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "use_stop without use_start rejected");
        return false;
      }
      use_held_ = false;
      if (!apply_gameplay("use_stop")) return false;
      break;
    case REIM_ACTIONTYPE_CLOSE_SCREEN:
      if (ui_spike_enabled_) {
        ui_spike_.close_screen();
      } else if (!apply_gameplay("close_screen")) {
        return false;
      }
      break;
    case REIM_ACTIONTYPE_DEBUG_SET_TIME: {
      if (!debug_controls_) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "debug action rejected: debug controls are not enabled");
        return false;
      }
      if (event.a < 0 || !simulation_.set_debug_time(static_cast<uint32_t>(event.a))) {
        actions_rejected_.fetch_add(1);
        return false;
      }
      break;
    }
    case REIM_ACTIONTYPE_DEBUG_SET_WEATHER: {
      if (!debug_controls_) {
        actions_rejected_.fetch_add(1);
        log_message(REIM_LOG_WARN, "action",
                    "debug action rejected: debug controls are not enabled");
        return false;
      }
      if (event.a < 0 || !simulation_.set_debug_weather(static_cast<uint32_t>(event.a))) {
        actions_rejected_.fetch_add(1);
        return false;
      }
      break;
    }
    default:
      /* Break/place/use/attack/item/pick/drop/respawn/drag intents all go to
       * the Runtime gameplay authority; the Host never mutates the world. */
      if (!apply_gameplay("action")) return false;
      break;
  }
  actions_applied_.fetch_add(1);
  return true;
}

void RuntimeInstance::queue_audio(uint32_t sound_id, uint32_t category,
                                  bool is_2d, float volume) {
  ReimAudioEvent event{};
  event.struct_size = sizeof(ReimAudioEvent);
  event.sound_id = sound_id;
  event.volume = std::clamp(volume, 0.0f, 1.0f);
  event.pitch = 1.0f;
  event.category = category;
  event.flags = is_2d ? REIM_AUDIO_FLAG_2D : 0u;
  event.sequence = ++audio_sequence_;
  tick_audio_.push_back(event);
}

void RuntimeInstance::update_contract_channels() {
  const game::GameView view = gameplay_.view(simulation_.player());
  ui::UiSpike::DrawList list;
  if (ui_spike_enabled_) {
    ui_spike_.build(list);
  } else {
    game_ui_.build(view, gameplay_.items(), simulation_.blocks(), list);
  }

  std::vector<ReimEntityRender> entities;
  std::vector<std::vector<ReimPartTransform>> entity_parts;
  for (const game::EntityView& view_entity : view.entities) {
    ReimEntityRender entity{};
    entity.entity_id = view_entity.id;
    entity.type_id = static_cast<uint32_t>(view_entity.type);
    entity.model_id = static_cast<uint32_t>(view_entity.type);
    entity.position.x = static_cast<float>(view_entity.x);
    entity.position.y = static_cast<float>(view_entity.y);
    entity.position.z = static_cast<float>(view_entity.z);
    entity.prev_position.x = static_cast<float>(view_entity.prev_x);
    entity.prev_position.y = static_cast<float>(view_entity.prev_y);
    entity.prev_position.z = static_cast<float>(view_entity.prev_z);
    entity.yaw = view_entity.yaw;
    entity.pitch = view_entity.pitch;
    entity.prev_yaw = view_entity.prev_yaw;
    entity.prev_pitch = view_entity.prev_pitch;
    entity.scale = 1.0f;
    entity.limb_swing = view_entity.limb_swing;
    entity.limb_swing_amount = view_entity.limb_amount;
    entity.head_yaw = view_entity.head_yaw;
    entity.head_pitch = view_entity.head_pitch;
    entity.hurt_time = static_cast<float>(view_entity.hurt_time);
    entity.death_time = static_cast<float>(std::max(0, view_entity.death_time));
    entity.flags = 0;
    if (view_entity.on_fire) entity.flags |= REIM_ENTITY_FLAG_ON_FIRE;
    if (view_entity.hurt_time > 0) entity.flags |= REIM_ENTITY_FLAG_HURT;
    if (view_entity.dying) entity.flags |= REIM_ENTITY_FLAG_DEAD;
    entity.held_item.struct_size = sizeof(ReimItemStack);
    std::vector<ReimPartTransform> parts =
        entity_part_transforms(view_entity);
    entity.part_count = static_cast<uint32_t>(parts.size());
    entity_parts.push_back(std::move(parts));
    entities.push_back(entity);
  }
  if (channel_spike_) {
    const core::Player& player = simulation_.player();
    const float yaw = std::fmod(static_cast<float>(channel_tick_ * 3) + 15.0f, 360.0f);
    const float radians = yaw * 3.14159265358979323846f / 180.0f;
    ReimEntityRender entity{};
    entity.entity_id = ~0ull;
    entity.type_id = 0;
    entity.model_id = 0;
    entity.position.x = static_cast<float>(player.x) + std::sin(radians) * 3.0f;
    entity.position.y = static_cast<float>(player.y) + 0.5f;
    entity.position.z = static_cast<float>(player.z) + std::cos(radians) * 3.0f;
    entity.prev_position = entity.position;
    entity.yaw = yaw;
    entity.prev_yaw = last_entity_yaw_;
    entity.pitch = 0.0f;
    entity.prev_pitch = 0.0f;
    entity.scale = 1.0f;
    entity.held_item.struct_size = sizeof(ReimItemStack);
    entity.part_count = 1;
    std::vector<ReimPartTransform> parts(1);
    parts[0].pivot[1] = 0.5f;
    parts[0].rotation[1] = yaw;
    parts[0].scale[0] = 1.0f;
    parts[0].scale[1] = 1.0f;
    parts[0].scale[2] = 1.0f;
    last_entity_yaw_ = yaw;
    entity_parts.push_back(std::move(parts));
    entities.push_back(entity);

    if (!channel_sound_emitted_) {
      queue_audio(1, REIM_AUDIO_CATEGORY_UI, true, 0.9f);
      channel_sound_emitted_ = true;
    }
  }

  for (const game::EntitySoundEvent& sound : view.entity_sounds) {
    ReimAudioEvent event{};
    event.struct_size = sizeof(ReimAudioEvent);
    event.sound_id = sound.id;
    event.position.x = static_cast<float>(sound.x);
    event.position.y = static_cast<float>(sound.y);
    event.position.z = static_cast<float>(sound.z);
    event.volume = std::clamp(sound.volume, 0.0f, 1.0f);
    event.pitch = sound.pitch;
    event.category = audio_category_for_sound(sound.id);
    event.flags = 0;
    event.sequence = ++audio_sequence_;
    tick_audio_.push_back(event);
  }

  /* I3: weather/ambient events are periodic and category-limited; they never
   * affect gameplay. */
  {
    const core::SimStatus status = simulation_.status();
    const uint64_t tick = status.tick;
    if ((status.weather == 1 || status.weather == 2) && tick % 200 == 0) {
      queue_audio(game::kSoundWeatherRain, REIM_AUDIO_CATEGORY_WEATHER, true, 0.25f);
    }
    if (status.weather == 2 && tick % 600 == 0) {
      queue_audio(game::kSoundThunder, REIM_AUDIO_CATEGORY_WEATHER, true, 0.4f);
    }
    if (status.tick % 1200 == 0 && status.player.y < 50.0) {
      queue_audio(game::kSoundAmbientCave, REIM_AUDIO_CATEGORY_AMBIENT, true, 0.2f);
    }
  }

  std::vector<ReimAudioEvent> audio;
  audio.swap(tick_audio_);

  ui_screen_status_.store(list.screen_id);
  ui_revision_status_.store(list.revision);
  ui_hovered_element_status_.store(list.hovered_element);
  ui_hovered_slot_status_.store(list.hovered_slot);
  ui_selected_slot_status_.store(list.selected_slot);
  ui_button_status_.store(ui_spike_.button_on() ? 1u : 0u);

  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    pending_.ui = std::move(list);
    pending_.view = view;
    pending_.has_view = true;
    latest_view_ = view;
    has_latest_view_ = true;
    pending_.entities = std::move(entities);
    pending_.entity_parts = std::move(entity_parts);
    pending_.particles.clear();
    pending_.particles.reserve(gameplay_.particles().size());
    for (const game::Particle& source : gameplay_.particles()) {
      ReimParticle particle{};
      particle.type = source.type;
      particle.position.x = static_cast<float>(source.x);
      particle.position.y = static_cast<float>(source.y);
      particle.position.z = static_cast<float>(source.z);
      particle.prev_position.x = static_cast<float>(source.prev_x);
      particle.prev_position.y = static_cast<float>(source.prev_y);
      particle.prev_position.z = static_cast<float>(source.prev_z);
      particle.velocity.x = static_cast<float>(source.vx);
      particle.velocity.y = static_cast<float>(source.vy);
      particle.velocity.z = static_cast<float>(source.vz);
      particle.size = source.size;
      particle.age = source.age;
      particle.lifetime = source.lifetime;
      particle.color = source.color;
      particle.texture = source.texture;
      particle.flags = source.flags;
      pending_.particles.push_back(particle);
    }
    pending_.audio = std::move(audio);
  }
  channel_tick_ = simulation_.tick_count();
}

int32_t RuntimeInstance::request_save(uint32_t flags) {
  (void)flags;
  std::lock_guard<std::mutex> lock(save_mutex_);
  ++save_request_count_;
  if (save_status_.state == REIM_SAVE_SAVING) return REIM_ERR_BUSY;
  /* The WorldWriter lands in M3-B1. Until then the instance has a read-only
   * world and says so instead of pretending to save. */
  save_status_.state = REIM_SAVE_FAILED;
  save_status_.last_result = REIM_ERR_NO_WORLD;
  std::snprintf(save_status_.last_error, sizeof(save_status_.last_error),
                "save writer is not available in this build (M3-B1)");
  return REIM_ERR_NO_WORLD;
}

int32_t RuntimeInstance::get_save_status(ReimSaveStatus* out) {
  if (out == nullptr) return REIM_ERR_INVALID_ARGUMENT;
  if (out->struct_size < REIM_SAVE_STATUS_MIN_SIZE) return REIM_ERR_INVALID_ARGUMENT;
  std::lock_guard<std::mutex> lock(save_mutex_);
  const size_t host_size = out->struct_size;
  std::memset(out, 0, std::min(host_size, sizeof(*out)));
  *out = save_status_;
  out->struct_size = sizeof(ReimSaveStatus);
  return REIM_OK;
}

std::string RuntimeInstance::build_status_json() {
  core::SimStatus status;
  game::GameView view;
  bool has_view = false;
  {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    status = latest_status_;
    view = latest_view_;
    has_view = has_latest_view_;
  }
  ReimSaveStatus save_status;
  {
    std::lock_guard<std::mutex> lock(save_mutex_);
    save_status = save_status_;
  }
  std::string out = "{\"stage\":\"M3-S2\",\"abi\":";
  out += std::to_string(kExpectedAbi);
  out += ",\"generated_schema\":" + std::to_string(generated_schema_version_);
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
  append_number(out, "health", has_view ? view.health : 20.0);
  append_number(out, "max_health", has_view ? view.max_health : 20.0);
  append_number(out, "food", has_view ? view.food : 20.0);
  append_number(out, "saturation", has_view ? view.saturation : 5.0);
  append_number(out, "air", has_view ? view.air : 300.0);
  append_uint(out, "xp_level", has_view ? view.xp_level : 0u);
  append_number(out, "xp_progress", has_view ? view.xp_progress : 0.0);
  append_bool(out, "dead", has_view && view.dead);
  append_uint(out, "selected_slot", ui_selected_slot_status_.load());
  out += "}";

  {
    nlohmann::json gp;
    gp["available"] = has_view;
    gp["screen"] = view.screen;
    gp["screen_revision"] = view.screen_revision;
    gp["selected_slot"] = view.selected_slot;
    gp["health"] = view.health;
    gp["food"] = view.food;
    gp["hovered_slot"] = view.hovered_slot;
    gp["pointer"] = {{"x", view.pointer_x},
                     {"y", view.pointer_y},
                     {"visible", view.pointer_visible}};
    auto stack_json = [&](const game::ItemStack& stack) {
      nlohmann::json value;
      value["item"] =
          stack.empty() ? std::string() : gameplay_.items().item_name(stack.item_id);
      value["count"] = stack.count;
      value["damage"] = stack.damage;
      value["flags"] = stack.flags;
      return value;
    };
    gp["held"] = stack_json(view.held);
    gp["cursor"] = stack_json(view.cursor);
    gp["inventory"] = nlohmann::json::object();
    for (const auto& [name, count] : view.item_counts) {
      gp["inventory"][name] = count;
    }
    gp["slots"] = nlohmann::json::array();
    for (const game::UiSlot& slot : view.slots) {
      gp["slots"].push_back(
          {{"slot", slot.slot}, {"item", stack_json(slot.item)}});
    }
    gp["drops"] = nlohmann::json::array();
    for (const game::DroppedItem& drop : view.drops) {
      gp["drops"].push_back({{"id", drop.id},
                             {"item", stack_json(drop.stack)},
                             {"x", drop.x},
                             {"y", drop.y},
                             {"z", drop.z},
                             {"age", drop.age}});
    }
    gp["drops_count"] = view.drops.size();
    gp["target"] = {{"type", view.interaction.target_type},
                    {"x", view.interaction.x},
                    {"y", view.interaction.y},
                    {"z", view.interaction.z},
                    {"block", view.interaction.block_name},
                    {"face", view.interaction.face},
                    {"can_break", view.interaction.can_break},
                    {"can_place", view.interaction.can_place},
                    {"progress", view.interaction.break_progress},
                    {"entity_id", view.interaction.entity_id},
                    {"entity_type",
                     view.interaction.entity_type != 0
                         ? std::string(game::entity_type_name(
                               static_cast<game::EntityType>(
                                   view.interaction.entity_type)))
                         : std::string()}};
    gp["furnace"] = {{"valid", view.furnace.valid},
                     {"x", view.furnace.x},
                     {"y", view.furnace.y},
                     {"z", view.furnace.z},
                     {"burn_time", view.furnace.burn_time},
                     {"burn_total", view.furnace.burn_total},
                     {"cook_time", view.furnace.cook_time},
                     {"input", stack_json(view.furnace.input)},
                     {"fuel", stack_json(view.furnace.fuel)},
                     {"output", stack_json(view.furnace.output)}};
    gp["chest"] = {{"valid", view.chest.valid},
                   {"x", view.chest.x},
                   {"y", view.chest.y},
                   {"z", view.chest.z}};
    gp["chest"]["slots"] = nlohmann::json::array();
    for (size_t i = 0; i < view.chest.slots.size(); ++i) {
      if (view.chest.slots[i].empty()) continue;
      gp["chest"]["slots"].push_back(
          {{"slot", i}, {"item", stack_json(view.chest.slots[i])}});
    }
    gp["probes"] = nlohmann::json::array();
    for (const game::Probe& probe : view.probes) {
      gp["probes"].push_back({{"x", probe.x},
                              {"y", probe.y},
                              {"z", probe.z},
                              {"block", probe.block},
                              {"biome", probe.biome}});
    }
    gp["recipes"] = {{"crafting", gameplay_.crafting_recipe_count()},
                     {"smelting", gameplay_.smelting_recipe_count()}};
    gp["counters"] = {{"blocks_broken", view.blocks_broken},
                      {"blocks_placed", view.blocks_placed},
                      {"items_dropped", view.items_dropped},
                      {"items_picked", view.items_picked},
                      {"crafts_taken", view.crafts_taken},
                      {"smelts_completed", view.smelts_completed},
                      {"mobs_killed", view.mobs_killed},
                      {"mobs_spawned", view.mobs_spawned}};
    gp["survival"] = {{"health", view.health},
                      {"max_health", view.max_health},
                      {"food", view.food},
                      {"saturation", view.saturation},
                      {"exhaustion", view.exhaustion},
                      {"air", view.air},
                      {"max_air", view.max_air},
                      {"xp_level", view.xp_level},
                      {"xp_progress", view.xp_progress},
                      {"xp_total", view.xp_total},
                      {"dead", view.dead},
                      {"on_fire", view.on_fire},
                      {"deaths", view.deaths},
                      {"damage_taken", view.damage_taken},
                      {"damage_cause", view.damage_cause},
                      {"mobs_killed", view.mobs_killed},
                      {"mobs_spawned", view.mobs_spawned}};
    gp["entities"] = nlohmann::json::array();
    for (const game::EntityView& entity : view.entities) {
      gp["entities"].push_back(
          {{"id", entity.id},
           {"type", game::entity_type_name(entity.type)},
           {"x", entity.x},
           {"y", entity.y},
           {"z", entity.z},
           {"health", entity.health},
           {"max_health", entity.max_health},
           {"hurt_time", entity.hurt_time},
           {"dying", entity.dying},
           {"on_fire", entity.on_fire}});
    }
    gp["entity_count"] = view.entities.size();
    std::map<std::string, int> mob_counts;
    for (const game::EntityView& entity : view.entities) {
      if (!entity.dying) ++mob_counts[game::entity_type_name(entity.type)];
    }
    gp["mob_counts"] = mob_counts.empty() ? nlohmann::json::object()
                                          : nlohmann::json(mob_counts);
    out += ",\"gameplay\":" + gp.dump();
  }

  {
    nlohmann::json world;
    world["dimension"] = status.dimension;
    world["time_of_day"] = status.time_of_day;
    world["day"] = status.day;
    world["weather"] = status.weather;
    world["sky_darken"] = status.sky_darken;
    world["sun_angle"] = status.sun_angle;
    world["seed"] = status.seed;
    world["generated_chunks"] = status.generated_chunks;
    world["ore_blocks"] = status.ore_blocks;
    world["trees"] = status.trees;
    world["plants"] = status.plants;
    world["spawn_surface"] = status.spawn_surface;
    world["worldgen_source"] = status.worldgen_source == 1 ? "data" : "native";
    nlohmann::json ores = nlohmann::json::object();
    for (const auto& [name, count] : simulation_.ore_counts()) {
      ores[name] = count;
    }
    world["ore_counts"] = std::move(ores);
    world["created_new_world"] = simulation_.create_world_mode();
    out += ",\"world\":" + world.dump();
  }

  char scalars[512];
  std::snprintf(
      scalars, sizeof(scalars),
      ",\"actions\":{\"submitted\":%llu,\"applied\":%llu,\"rejected\":%llu,"
      "\"coalesced\":%llu,\"last_sequence\":%llu,\"wheel\":%llu,"
      "\"pointer_buttons\":%llu},"
      "\"ui\":{\"screen\":%u,\"revision\":%u,\"hovered_element\":%u,"
      "\"hovered_slot\":%u,\"selected_slot\":%u,\"button_on\":%u,"
      "\"canvas_width\":%u,\"canvas_height\":%u,\"scale\":%.3f,"
      "\"spike\":%s},"
      "\"channels\":{\"entity_spike\":%s,\"audio_events\":%llu},"
      "\"save\":{\"state\":%u,\"last_result\":%d,\"requests\":%llu}",
      static_cast<unsigned long long>(actions_submitted_.load()),
      static_cast<unsigned long long>(actions_applied_.load()),
      static_cast<unsigned long long>(actions_rejected_.load()),
      static_cast<unsigned long long>(actions_coalesced_.load()),
      static_cast<unsigned long long>(last_action_sequence_.load()),
      static_cast<unsigned long long>(action_wheel_.load()),
      static_cast<unsigned long long>(pointer_buttons_.load()),
      ui_screen_status_.load(), ui_revision_status_.load(),
      ui_hovered_element_status_.load(), ui_hovered_slot_status_.load(),
      ui_selected_slot_status_.load(), ui_button_status_.load(),
      ui_canvas_width_.load(), ui_canvas_height_.load(),
      static_cast<double>(ui_scale_.load()),
      ui_spike_enabled_ ? "true" : "false",
      channel_spike_ ? "true" : "false",
      static_cast<unsigned long long>(audio_sequence_.load()),
      save_status.state, save_status.last_result,
      static_cast<unsigned long long>(save_request_count_.load()));
  out += scalars;
  out += ",\"resource_packs\":";
  out += resource_pack_report_json_.empty() ? "{\"pack_count\":0}"
                                            : resource_pack_report_json_;
  out += ",\"data_packs\":";
  out += data_pack_report_json_.empty() ? "{\"pack_count\":0}" : data_pack_report_json_;
  out += ",\"worldgen\":";
  out += worldgen_summary_json_.empty() ? "{}" : worldgen_summary_json_;
  out += ",\"render_level\":";
  out += std::to_string(render_level_);
  out += ",\"samples\":[";
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
