#include "game/gameplay.h"

#include "game/slot_layout.h"
#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <utility>

#include "json.hpp"

namespace reim::game {
namespace {

constexpr float kReach = 4.5f;
constexpr double kPickupRadius = 2.6;
constexpr double kPickupCenterHeight = 0.9;
constexpr int kPickupDelayTicks = 10;
constexpr int kDespawnTicks = 6000;
constexpr int kCookTicks = 200;
constexpr float kPlayerHalf = static_cast<float>(core::kPlayerHalfWidth);
constexpr float kPlayerHeight = static_cast<float>(core::kPlayerHeight);

std::string_view bare_name(std::string_view name) {
  const size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

bool is_air(std::string_view name) {
  return name == "minecraft:air" || name == "air" || name.empty();
}

bool is_replaceable(std::string_view name) {
  const std::string_view bare = bare_name(name);
  return is_air(name) || bare == "water" || bare == "lava" ||
         bare == "short_grass" || bare == "tall_grass" || bare == "fern" ||
         bare == "large_fern" || bare == "dead_bush" || bare == "snow" ||
         bare == "vine" || bare == "fire" || bare == "soul_fire";
}

int max_stack_of(const ItemTable& items, uint32_t item_id) {
  return std::max(1, static_cast<int>(items.def(item_id).max_stack));
}

ItemStack make_stack(uint32_t item_id, int count) {
  ItemStack stack;
  stack.item_id = item_id;
  stack.count = count;
  return stack;
}

void face_normal(uint32_t face, int& dx, int& dy, int& dz) {
  dx = dy = dz = 0;
  switch (face) {
    case REIM_FACE_DOWN: dy = -1; break;
    case REIM_FACE_UP: dy = 1; break;
    case REIM_FACE_NORTH: dz = -1; break;
    case REIM_FACE_SOUTH: dz = 1; break;
    case REIM_FACE_WEST: dx = -1; break;
    case REIM_FACE_EAST: dx = 1; break;
    default: dy = 1; break;
  }
}

void player_bounds(const core::Player& player, double& x0, double& y0, double& z0,
                   double& x1, double& y1, double& z1) {
  x0 = player.x - kPlayerHalf;
  x1 = player.x + kPlayerHalf;
  y0 = player.y;
  y1 = player.y + kPlayerHeight;
  z0 = player.z - kPlayerHalf;
  z1 = player.z + kPlayerHalf;
}

bool box_overlaps_block(double x0, double y0, double z0, double x1, double y1,
                        double z1, int bx, int by, int bz) {
  return x1 > bx && x0 < bx + 1.0 && y1 > by && y0 < by + 1.0 && z1 > bz &&
         z0 < bz + 1.0;
}

void forward_vector(const core::Player& player, double& fx, double& fy,
                    double& fz) {
  constexpr double kPi = 3.14159265358979323846;
  const double yaw = static_cast<double>(player.yaw) * kPi / 180.0;
  const double pitch = static_cast<double>(player.pitch) * kPi / 180.0;
  fx = -std::sin(yaw) * std::cos(pitch);
  fy = -std::sin(pitch);
  fz = std::cos(yaw) * std::cos(pitch);
}

uint32_t face_from_eye(const core::Player& player, int bx, int by, int bz) {
  const double ex = player.x - (bx + 0.5);
  const double ey = (player.y + core::kPlayerEyeHeight) - (by + 0.5);
  const double ez = player.z - (bz + 0.5);
  const double ax = std::fabs(ex);
  const double ay = std::fabs(ey);
  const double az = std::fabs(ez);
  if (ax >= ay && ax >= az) return ex >= 0.0 ? REIM_FACE_EAST : REIM_FACE_WEST;
  if (ay >= az) return ey >= 0.0 ? REIM_FACE_UP : REIM_FACE_DOWN;
  return ez >= 0.0 ? REIM_FACE_SOUTH : REIM_FACE_NORTH;
}

void put_property(world::BlockState& state, std::string key, std::string value) {
  state.properties.emplace(std::move(key), std::move(value));
}

/* Placement state: logs pick an axis, stairs pick facing/half, slabs pick
 * top/bottom. Other blocks use their representative state (the Converter
 * variant table owns connection/shape states until C7/M3-K3). */
world::BlockState placement_state(const ItemTable& items, uint32_t item_id,
                                  const Hit& hit, const core::Player& player) {
  world::BlockState state;
  state.name = items.def(item_id).name;
  const std::string_view bare = bare_name(state.name);
  if (ends_with(bare, "_log") || ends_with(bare, "_wood") ||
      ends_with(bare, "_stem") || ends_with(bare, "_hyphae")) {
    if (hit.face == REIM_FACE_UP || hit.face == REIM_FACE_DOWN) {
      put_property(state, "axis", "y");
    } else if (hit.face == REIM_FACE_EAST || hit.face == REIM_FACE_WEST) {
      put_property(state, "axis", "x");
    } else {
      put_property(state, "axis", "z");
    }
    return state;
  }
  if (ends_with(bare, "_stairs")) {
    const float yaw = std::fmod(std::fmod(player.yaw, 360.0f) + 360.0f, 360.0f);
    const char* facing = "south";
    if (yaw >= 45.0f && yaw < 135.0f) facing = "west";
    else if (yaw >= 135.0f && yaw < 225.0f) facing = "north";
    else if (yaw >= 225.0f && yaw < 315.0f) facing = "east";
    put_property(state, "facing", facing);
    put_property(state, "half", hit.face == REIM_FACE_DOWN ? "top" : "bottom");
    put_property(state, "shape", "straight");
    put_property(state, "waterlogged", "false");
    return state;
  }
  if (ends_with(bare, "_slab")) {
    put_property(state, "type", hit.face == REIM_FACE_DOWN ? "top" : "bottom");
    put_property(state, "waterlogged", "false");
    return state;
  }
  if (ends_with(bare, "_trapdoor")) {
    put_property(state, "half", hit.face == REIM_FACE_DOWN ? "top" : "bottom");
    put_property(state, "open", "false");
    put_property(state, "powered", "false");
    put_property(state, "waterlogged", "false");
    return state;
  }
  return state;
}

bool is_item_slot_number(uint32_t screen, int slot) {
  switch (screen) {
    case REIM_SCREEN_INVENTORY:
      return (slot >= 9 && slot <= 44);
    case REIM_SCREEN_CRAFTING_TABLE:
      return (slot >= 10 && slot <= 45);
    case REIM_SCREEN_FURNACE:
      return (slot >= 3 && slot <= 38);
    case REIM_SCREEN_CHEST:
      return (slot >= 27 && slot <= 62);
    default:
      return false;
  }
}

} // namespace

bool Gameplay::initialize(WorldAccess& world, const std::string& generated_dir,
                          std::string& error) {
  world_ = &world;
  if (!items_.load(world.blocks(), std::filesystem::path(generated_dir), error)) {
    error = "item table: " + error;
    return false;
  }
  recipes_.load(items_);
  inventory_.clear();
  grid_.clear();
  craft_result_.clear();
  cursor_.clear();
  screen_ = REIM_SCREEN_HUD;
  screen_revision_ = 1;
  target_ = Hit{};
  break_target_ = Hit{};
  survival_reset(survival_, true);
  entities_.initialize(world_, &items_, entity_seed_);
  has_spawn_point_ = false;
  want_respawn_ = false;
  dimension_request_ = -1;
  reim::log::info("gameplay",
                  "item table loaded: " + std::to_string(items_.size()) +
                      " items, " + std::to_string(recipes_.crafting_count()) +
                      " crafting recipes, " +
                      std::to_string(recipes_.smelting_count()) + " smelting");
  return true;
}

void Gameplay::set_rules(uint32_t gamemode, uint32_t difficulty, uint64_t seed) {
  gamemode_ = gamemode;
  difficulty_ = difficulty;
  entity_seed_ = seed;
  entities_.initialize(world_, &items_, seed);
}

int Gameplay::take_dimension_request() {
  const int request = dimension_request_;
  dimension_request_ = -1;
  return request;
}

void Gameplay::on_dimension_changed() {
  entities_.clear();
  target_ = Hit{};
  target_entity_ = EntityHit{};
  break_target_ = Hit{};
  break_progress_ = 0.0f;
  break_held_ = false;
  hovered_slot_ = -1;
  if (has_active_container_) {
    has_active_container_ = false;
    active_container_ = BlockPos{};
    screen_ = REIM_SCREEN_HUD;
    ++screen_revision_;
  }
}

bool Gameplay::configure_scenario(const std::string& scenario_json,
                                  std::string& error) {
  if (scenario_json.empty()) return true;
  const auto root = nlohmann::json::parse(scenario_json.begin(),
                                          scenario_json.end(), nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    error = "scenario config is not a JSON object";
    return false;
  }
  scenario_active_ = true;
  /* Automated scenes are deterministic: natural spawning stays off unless the
   * scenario asks for it. Normal interactive runs keep it on. */
  entities_.set_natural_spawning(false);
  if (root.contains("setup") && root["setup"].is_array()) {
    for (const auto& entry : root["setup"]) {
      if (!entry.is_object()) continue;
      const std::string action = entry.value("action", "");
      if (action == "give") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::give;
        op.name = entry.value("item", "");
        op.count = entry.value("count", 1);
        if (op.name.empty() || op.count <= 0) continue;
        setup_ops_.push_back(std::move(op));
      } else if (action == "set_block") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::set_block;
        op.name = entry.value("block", "");
        op.x = entry.value("x", 0);
        op.y = entry.value("y", 0);
        op.z = entry.value("z", 0);
        if (op.name.empty()) continue;
        setup_ops_.push_back(std::move(op));
      } else if (action == "platform") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::platform;
        op.name = entry.value("block", "minecraft:stone");
        op.x = entry.value("x", 0);
        op.y = entry.value("y", 0);
        op.z = entry.value("z", 0);
        op.count = std::clamp(entry.value("radius", 2), 1, 8);
        setup_ops_.push_back(std::move(op));
      } else if (action == "clear_inventory") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::clear;
        setup_ops_.push_back(op);
      } else if (action == "set_health" || action == "set_food" ||
                 action == "set_air" || action == "set_xp" ||
                 action == "damage_player") {
        ScenarioSetupOp op;
        op.kind = action == "set_health" ? ScenarioSetupOp::Kind::set_health
                : action == "set_food"   ? ScenarioSetupOp::Kind::set_food
                : action == "set_air"    ? ScenarioSetupOp::Kind::set_air
                : action == "set_xp"     ? ScenarioSetupOp::Kind::set_xp
                                         : ScenarioSetupOp::Kind::damage_player;
        op.value = entry.value("value", entry.value("amount", 1.0f));
        op.name = entry.value("cause", "none");
        setup_ops_.push_back(op);
      } else if (action == "spawn_entity") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::spawn_entity;
        op.name = entry.value("entity", entry.value("type", "zombie"));
        op.count = std::max(1, entry.value("count", 1));
        op.x = entry.value("x", 0);
        op.y = entry.value("y", 0);
        op.z = entry.value("z", 0);
        setup_ops_.push_back(std::move(op));
      } else if (action == "clear_entities") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::clear_entities;
        setup_ops_.push_back(op);
      } else if (action == "natural_spawn") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::natural_spawn;
        op.count = entry.value("enabled", true) ? 1 : 0;
        setup_ops_.push_back(op);
      } else if (action == "keep_inventory") {
        ScenarioSetupOp op;
        op.kind = ScenarioSetupOp::Kind::keep_inventory;
        op.count = entry.value("enabled", true) ? 1 : 0;
        setup_ops_.push_back(op);
      }
    }
  }
  if (root.contains("probes") && root["probes"].is_array()) {
    for (const auto& probe : root["probes"]) {
      if (!probe.is_array() || probe.size() < 3) continue;
      BlockPos pos;
      pos.x = probe[0].get<int>();
      pos.y = probe[1].get<int>();
      pos.z = probe[2].get<int>();
      probe_positions_.push_back(pos);
    }
  }
  return true;
}

void Gameplay::apply_setup(const core::Player& player) {
  if (world_ == nullptr) return;
  if (!has_spawn_point_) {
    spawn_point_ = player;
    has_spawn_point_ = true;
  }
  for (const ScenarioSetupOp& op : setup_ops_) {
    if (op.kind == ScenarioSetupOp::Kind::clear) {
      inventory_.clear();
      grid_.clear();
      craft_result_.clear();
      cursor_.clear();
      continue;
    }
    if (op.kind == ScenarioSetupOp::Kind::give) {
      const uint32_t item_id = items_.find(op.name);
      if (item_id == 0) {
        reim::log::warn("gameplay", "scenario give: unknown item " + op.name);
        continue;
      }
      ItemStack stack = make_stack(item_id, op.count);
      const int remainder = inventory_.add_partial(items_, stack);
      if (remainder > 0) {
        spawn_drop(make_stack(item_id, remainder), player.x, player.y + 1.0,
                   player.z, 0.0, 0.0, 0.0, 0);
      }
      continue;
    }
    switch (op.kind) {
      case ScenarioSetupOp::Kind::set_health:
        survival_.health = std::clamp(op.value, 0.0f, survival_.max_health);
        if (survival_.health <= 0.0f) finish_death(player);
        continue;
      case ScenarioSetupOp::Kind::set_food:
        survival_.food = std::clamp(op.value, 0.0f, 20.0f);
        continue;
      case ScenarioSetupOp::Kind::set_air:
        survival_.air = std::clamp(op.value, 0.0f, survival_.max_air);
        continue;
      case ScenarioSetupOp::Kind::set_xp: {
        survival_.xp_level = 0;
        survival_.xp_total = 0;
        survival_.xp_in_level = 0.0f;
        survival_add_xp(survival_, static_cast<uint32_t>(std::max(0.0f, op.value)));
        continue;
      }
      case ScenarioSetupOp::Kind::damage_player: {
        DamageCause cause = DamageCause::unknown;
        if (op.name == "fall") cause = DamageCause::fall;
        else if (op.name == "drown") cause = DamageCause::drown;
        else if (op.name == "lava") cause = DamageCause::lava;
        else if (op.name == "fire") cause = DamageCause::fire;
        else if (op.name == "mob") cause = DamageCause::mob;
        else if (op.name == "starve") cause = DamageCause::starve;
        else if (op.name == "explosion") cause = DamageCause::explosion;
        survival_.invuln = 0;
        survival_damage(survival_, op.value, cause);
        if (survival_.health <= 0.0f) finish_death(player);
        continue;
      }
      case ScenarioSetupOp::Kind::spawn_entity: {
        for (int i = 0; i < op.count; ++i) {
          entities_.spawn_named(op.name, op.x + 0.5 + i * 0.7, op.y,
                                op.z + 0.5);
          ++mobs_spawned_;
        }
        continue;
      }
      case ScenarioSetupOp::Kind::clear_entities:
        entities_.clear();
        continue;
      case ScenarioSetupOp::Kind::natural_spawn:
        entities_.set_natural_spawning(op.count != 0);
        continue;
      case ScenarioSetupOp::Kind::keep_inventory:
        keep_inventory_ = op.count != 0;
        continue;
      case ScenarioSetupOp::Kind::platform: {
        world::BlockState state;
        state.name = op.name;
        for (int dx = -op.count; dx <= op.count; ++dx) {
          for (int dz = -op.count; dz <= op.count; ++dz) {
            std::string set_error;
            world_->set_block(op.x + dx, op.y, op.z + dz, state, set_error);
          }
        }
        continue;
      }
      default:
        break;
    }
    world::BlockState state;
    state.name = op.name;
    std::string set_error;
    if (!world_->set_block(op.x, op.y, op.z, state, set_error)) {
      reim::log::warn("gameplay", "scenario set_block " + op.name + " at " +
                                     std::to_string(op.x) + "," +
                                     std::to_string(op.y) + "," +
                                     std::to_string(op.z) + ": " + set_error);
    }
  }
  setup_ops_.clear();
}

void Gameplay::set_canvas(uint32_t width, uint32_t height, uint32_t scale) {
  if (width < 64) width = 64;
  if (height < 64) height = 64;
  if (scale < 1) scale = 1;
  canvas_width_ = width;
  canvas_height_ = height;
  ui_scale_ = scale;
}

Hit Gameplay::raycast(const core::Player& player) const {
  Hit hit;
  if (world_ == nullptr) return hit;
  constexpr double kPi = 3.14159265358979323846;
  const double yaw = static_cast<double>(player.yaw) * kPi / 180.0;
  const double pitch = static_cast<double>(player.pitch) * kPi / 180.0;
  const double dx = -std::sin(yaw) * std::cos(pitch);
  const double dy = -std::sin(pitch);
  const double dz = std::cos(yaw) * std::cos(pitch);
  const double ex = player.x;
  const double ey = player.y + core::kPlayerEyeHeight;
  const double ez = player.z;

  int bx = static_cast<int>(std::floor(ex));
  int by = static_cast<int>(std::floor(ey));
  int bz = static_cast<int>(std::floor(ez));
  const int step_x = dx > 0.0 ? 1 : (dx < 0.0 ? -1 : 0);
  const int step_y = dy > 0.0 ? 1 : (dy < 0.0 ? -1 : 0);
  const int step_z = dz > 0.0 ? 1 : (dz < 0.0 ? -1 : 0);
  const double inf = std::numeric_limits<double>::infinity();
  auto first = [](double origin, int cell, int step, double direction) {
    if (step == 0) return std::numeric_limits<double>::infinity();
    const double boundary = step > 0 ? cell + 1.0 : cell;
    return (boundary - origin) / direction;
  };
  auto delta = [](double direction) {
    return direction == 0.0 ? std::numeric_limits<double>::infinity()
                            : std::fabs(1.0 / direction);
  };
  double t_max_x = first(ex, bx, step_x, dx);
  double t_max_y = first(ey, by, step_y, dy);
  double t_max_z = first(ez, bz, step_z, dz);
  const double t_delta_x = delta(dx);
  const double t_delta_y = delta(dy);
  const double t_delta_z = delta(dz);

  uint32_t face = REIM_FACE_UP;
  double t = 0.0;
  for (int guard = 0; guard < 256; ++guard) {
    world::BlockState state;
    if (world_->block_state_at(bx, by, bz, state) && !is_air(state.name)) {
      const data::BlockDef& def = world_->blocks().def(world_->block_id_at(bx, by, bz));
      if (!def.liquid) {
        hit.valid = true;
        hit.x = bx;
        hit.y = by;
        hit.z = bz;
        hit.block_id = world_->block_id_at(bx, by, bz);
        hit.block_name = state.name;
        hit.face = face;
        hit.distance = static_cast<float>(t);
        hit.hit_x = static_cast<float>(ex + dx * t);
        hit.hit_y = static_cast<float>(ey + dy * t);
        hit.hit_z = static_cast<float>(ez + dz * t);
        return hit;
      }
    }
    if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
      if (t_max_x > kReach) break;
      t = t_max_x;
      bx += step_x;
      t_max_x += t_delta_x;
      face = step_x > 0 ? REIM_FACE_WEST : REIM_FACE_EAST;
    } else if (t_max_y <= t_max_z) {
      if (t_max_y > kReach) break;
      t = t_max_y;
      by += step_y;
      t_max_y += t_delta_y;
      face = step_y > 0 ? REIM_FACE_DOWN : REIM_FACE_UP;
    } else {
      if (t_max_z > kReach) break;
      t = t_max_z;
      bz += step_z;
      t_max_z += t_delta_z;
      face = step_z > 0 ? REIM_FACE_NORTH : REIM_FACE_SOUTH;
    }
  }
  (void)inf;
  return hit;
}

bool Gameplay::resolve_hit(const ReimActionEvent& event, const core::Player& player,
                           Hit& out, std::string& error) const {
  const bool explicit_target =
      event.x != 0.0f || event.y != 0.0f || event.z != 0.0f;
  if (!explicit_target) {
    out = target_;
    if (!out.valid) {
      error = "no block target";
      return false;
    }
    return true;
  }
  const int bx = static_cast<int>(std::floor(event.x));
  const int by = static_cast<int>(std::floor(event.y));
  const int bz = static_cast<int>(std::floor(event.z));
  const double eye_x = player.x;
  const double eye_y = player.y + core::kPlayerEyeHeight;
  const double eye_z = player.z;
  const double dx = bx + 0.5 - eye_x;
  const double dy = by + 0.5 - eye_y;
  const double dz = bz + 0.5 - eye_z;
  if (std::sqrt(dx * dx + dy * dy + dz * dz) > kReach + 1.0) {
    error = "target outside reach";
    return false;
  }
  world::BlockState state;
  if (!world_->block_state_at(bx, by, bz, state) || is_air(state.name)) {
    error = "target block is not loaded or is air";
    return false;
  }
  out.valid = true;
  out.x = bx;
  out.y = by;
  out.z = bz;
  out.block_id = world_->block_id_at(bx, by, bz);
  out.block_name = state.name;
  out.face = face_from_eye(player, bx, by, bz);
  out.distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
  out.hit_x = static_cast<float>(bx + 0.5);
  out.hit_y = static_cast<float>(by + 0.5);
  out.hit_z = static_cast<float>(bz + 0.5);
  return true;
}

bool Gameplay::handle_action(const ReimActionEvent& event, core::Player& player,
                             std::string& error) {
  error.clear();
  switch (event.type) {
    case REIM_ACTIONTYPE_SELECT_SLOT: {
      if (event.a < 0 || event.a > 8) {
        error = "selected slot out of range";
        return false;
      }
      inventory_.selected = event.a;
      return true;
    }
    case REIM_ACTIONTYPE_WHEEL: {
      const int direction = event.wheel > 0.0f ? 1 : (event.wheel < 0.0f ? -1 : 0);
      if (direction == 0) return true;
      inventory_.selected = (inventory_.selected + direction + 9) % 9;
      return true;
    }
    case REIM_ACTIONTYPE_POINTER: {
      pointer(event.screen_x, event.screen_y, event.c & 0x00FFu);
      return true;
    }
    case REIM_ACTIONTYPE_SLOT_CLICK: {
      if (screen_ == REIM_SCREEN_HUD) {
        if (event.a < 0 || event.a > 8) {
          error = "HUD slot out of range";
          return false;
        }
        inventory_.selected = event.a;
        return true;
      }
      return click_slot(event.a, event.b, event.c, error);
    }
    case REIM_ACTIONTYPE_BREAK_START: {
      break_held_ = true;
      break_progress_ = 0.0f;
      break_target_ = Hit{};
      return true;
    }
    case REIM_ACTIONTYPE_BREAK_STOP: {
      break_held_ = false;
      break_progress_ = 0.0f;
      break_target_ = Hit{};
      return true;
    }
    case REIM_ACTIONTYPE_USE_START: {
      use_held_ = true;
      use_consumed_ = false;
      return use_once(player, error);
    }
    case REIM_ACTIONTYPE_USE_STOP: {
      use_held_ = false;
      use_ticks_ = 0;
      return true;
    }
    case REIM_ACTIONTYPE_BREAK_BLOCK: {
      Hit hit;
      if (!resolve_hit(event, player, hit, error)) return false;
      return break_block(hit, player, error);
    }
    case REIM_ACTIONTYPE_PLACE_BLOCK: {
      Hit hit;
      if (!resolve_hit(event, player, hit, error)) return false;
      return place_block(hit, player, error);
    }
    case REIM_ACTIONTYPE_USE_BLOCK: {
      Hit hit;
      if (!resolve_hit(event, player, hit, error)) return false;
      BlockPos pos{hit.x, hit.y, hit.z};
      const std::string_view bare = bare_name(hit.block_name);
      if (bare == "crafting_table") {
        open_crafting_table();
        return true;
      }
      if (bare == "furnace" || bare == "blast_furnace" || bare == "smoker" ||
          bare == "chest" || bare == "barrel" || bare == "trapped_chest") {
        return open_container(hit, error);
      }
      if (bare == "nether_portal") {
        const uint32_t current = world_ != nullptr ? world_->environment().dimension : 0u;
        dimension_request_ = current == REIM_DIMENSION_NETHER
                                 ? REIM_DIMENSION_OVERWORLD
                                 : REIM_DIMENSION_NETHER;
        return true;
      }
      if (!inventory_.held().empty()) {
        const ItemDef& def = items_.def(inventory_.held().item_id);
        if (def.is_food()) return use_once(player, error);
        if (def.is_block()) return place_block(hit, player, error);
      }
      error = "block is not usable";
      return false;
    }
    case REIM_ACTIONTYPE_USE_ITEM: {
      return use_once(player, error);
    }
    case REIM_ACTIONTYPE_PICK_BLOCK: {
      Hit hit;
      if (!resolve_hit(event, player, hit, error)) return false;
      const uint32_t item_id = items_.block_item(hit.block_id);
      if (item_id == 0) {
        error = "block has no item form";
        return false;
      }
      int slot = inventory_.first_slot(item_id);
      if (slot >= 0) {
        if (slot < 9) {
          inventory_.selected = slot;
        } else {
          std::swap(inventory_.slots[static_cast<size_t>(slot)],
                    inventory_.hotbar(inventory_.selected));
        }
        return true;
      }
      error = "item is not in the inventory (survival pick block)";
      return false;
    }
    case REIM_ACTIONTYPE_DROP_ITEM: {
      return drop_from_selected(player, event.b != 0);
    }
    case REIM_ACTIONTYPE_CLOSE_SCREEN: {
      if (screen_ == REIM_SCREEN_HUD) {
        open_pause();
      } else {
        close_screen(player);
      }
      return true;
    }
    case REIM_ACTIONTYPE_ATTACK: {
      if (survival_.dead || screen_ != REIM_SCREEN_HUD) {
        error = survival_.dead ? "player is dead" : "a screen is open";
        return false;
      }
      if (attack_cooldown_ < 1.0f) {
        error = "attack cooldown is not ready";
        return false;
      }
      EntityHit hit = entities_.raycast(player, 3.0);
      if (hit.valid && target_.valid && target_.distance < hit.distance) {
        hit.valid = false;
      }
      if (!hit.valid) {
        /* A swing at air is legal (M3-D2); it still starts the cooldown. */
        attack_cooldown_ = 0.0f;
        emit_sound(kSoundAttack, player.x, player.y + 1.4, player.z, 0.4f, 1.0f);
        return true;
      }
      return attack_entity(hit, player, error);
    }
    case REIM_ACTIONTYPE_RESPAWN: {
      if (!survival_.dead) {
        error = "respawn requires a dead player";
        return false;
      }
      want_respawn_ = true;
      return true;
    }
    case REIM_ACTIONTYPE_DRAG_START:
    case REIM_ACTIONTYPE_DRAG_TO:
    case REIM_ACTIONTYPE_DRAG_END: {
      /* The pointer channel already implements basic drag (press picks up,
       * release drops). Explicit DRAG_* events are accepted as no-ops so a
       * Host that sends both does not double-apply a transaction. */
      return true;
    }
    default:
      error = "action is not a gameplay action";
      return false;
  }
}

void Gameplay::pointer(float screen_x, float screen_y, uint32_t buttons) {
  const uint32_t scale = ui_scale_;
  const UiLayout layout = ui_layout(canvas_width_, canvas_height_, scale);
  raw_pointer_x_ = screen_x;
  raw_pointer_y_ = screen_y;
  pointer_x_ = logical_x(screen_x, layout, scale);
  pointer_y_ = logical_y(screen_y, layout, scale);
  hovered_element_ = 0;
  hovered_slot_ = slot_at(screen_, pointer_x_, pointer_y_);

  const bool prev_left = (pointer_buttons_ & REIM_POINTER_BUTTON_LEFT) != 0;
  const bool left = (buttons & REIM_POINTER_BUTTON_LEFT) != 0;
  const bool prev_right = (pointer_buttons_ & REIM_POINTER_BUTTON_RIGHT) != 0;
  const bool right = (buttons & REIM_POINTER_BUTTON_RIGHT) != 0;

  if (screen_ == REIM_SCREEN_HUD) {
    if (!prev_left && left && hovered_slot_ >= 0) {
      inventory_.selected = hovered_slot_;
    }
    pointer_buttons_ = buttons;
    return;
  }

  if (screen_ == REIM_SCREEN_PAUSE) {
    const SlotRect close = close_button_rect();
    if (pointer_x_ >= close.x && pointer_x_ < close.x + close.w &&
        pointer_y_ >= close.y && pointer_y_ < close.y + close.h) {
      hovered_element_ = 1;
      if (!prev_left && left) {
        screen_ = REIM_SCREEN_HUD;
        ++screen_revision_;
      }
    }
    pointer_buttons_ = buttons;
    return;
  }

  std::string error;
  if (!prev_left && left && hovered_slot_ >= 0) {
    click_slot(hovered_slot_, 0, 0, error);
    dragging_ = true;
    drag_source_ = hovered_slot_;
  }
  if (prev_left && !left && dragging_) {
    /* Releasing over the slot the press picked up from leaves the stack on
     * the cursor (a normal click); releasing elsewhere drops it there. */
    if (hovered_slot_ >= 0 && hovered_slot_ != drag_source_) {
      click_slot(hovered_slot_, 0, 0, error);
    }
    dragging_ = false;
    drag_source_ = -1;
  }
  if (!prev_right && right && hovered_slot_ >= 0) {
    click_slot(hovered_slot_, 1, 0, error);
  }
  pointer_buttons_ = buttons;
}

void Gameplay::release_pointer(uint32_t buttons) { pointer_buttons_ = buttons; }

void Gameplay::on_key(int32_t key, bool pressed, const core::Player& player) {
  if (!pressed) return;
  if (key == REIM_KEY_ENTER && survival_.dead) {
    want_respawn_ = true;
    return;
  }
  switch (key) {
    case REIM_KEY_E:
      if (screen_ == REIM_SCREEN_HUD) {
        open_inventory();
      } else {
        close_screen(player);
      }
      break;
    case REIM_KEY_ESCAPE:
      if (screen_ == REIM_SCREEN_HUD) {
        open_pause();
      } else {
        close_screen(player);
      }
      break;
    case REIM_KEY_Q:
      if (screen_ == REIM_SCREEN_HUD) {
        drop_from_selected(player, false);
      } else if (!cursor_.empty()) {
        ItemStack one = cursor_;
        one.count = 1;
        --cursor_.count;
        if (cursor_.count <= 0) cursor_.clear();
        double fx = 0.0, fy = 0.0, fz = 0.0;
        forward_vector(player, fx, fy, fz);
        spawn_drop(one, player.x + fx * 0.4, player.y + 1.2,
                   player.z + fz * 0.4, fx * 0.25, 0.1, fz * 0.25,
                   kPickupDelayTicks);
      }
      break;
    case REIM_KEY_1:
    case REIM_KEY_2:
    case REIM_KEY_3:
    case REIM_KEY_4:
    case REIM_KEY_5:
    case REIM_KEY_6:
    case REIM_KEY_7:
    case REIM_KEY_8:
    case REIM_KEY_9:
      inventory_.selected = key - REIM_KEY_1;
      break;
    default:
      break;
  }
}

void Gameplay::open_inventory() {
  if (screen_ != REIM_SCREEN_HUD) return;
  screen_ = REIM_SCREEN_INVENTORY;
  grid_.size = 2;
  recompute_grid();
  ++screen_revision_;
  hovered_slot_ = -1;
}

void Gameplay::open_crafting_table() {
  if (screen_ == REIM_SCREEN_CRAFTING_TABLE) return;
  if (screen_ != REIM_SCREEN_HUD) return;
  screen_ = REIM_SCREEN_CRAFTING_TABLE;
  grid_.clear();
  grid_.size = 3;
  recompute_grid();
  ++screen_revision_;
  hovered_slot_ = -1;
}

bool Gameplay::open_container(const Hit& hit, std::string& error) {
  if (screen_ != REIM_SCREEN_HUD) {
    error = "another screen is already open";
    return false;
  }
  const std::string_view bare = bare_name(hit.block_name);
  ContainerKind kind = ContainerKind::none;
  uint32_t screen = REIM_SCREEN_HUD;
  if (bare == "furnace" || bare == "blast_furnace" || bare == "smoker") {
    kind = ContainerKind::furnace;
    screen = REIM_SCREEN_FURNACE;
  } else if (bare == "chest" || bare == "barrel" || bare == "trapped_chest") {
    kind = ContainerKind::chest;
    screen = REIM_SCREEN_CHEST;
  } else {
    error = "block has no container";
    return false;
  }
  const BlockPos pos{hit.x, hit.y, hit.z};
  ensure_container(pos, kind);
  active_container_ = pos;
  has_active_container_ = true;
  screen_ = screen;
  ++screen_revision_;
  hovered_slot_ = -1;
  return true;
}

void Gameplay::open_pause() {
  if (screen_ != REIM_SCREEN_HUD) return;
  screen_ = REIM_SCREEN_PAUSE;
  ++screen_revision_;
  hovered_slot_ = -1;
}

void Gameplay::close_screen(const core::Player& player) {
  if (screen_ == REIM_SCREEN_HUD) return;
  return_grid_to_player(player);
  if (!cursor_.empty()) {
    const int remainder = inventory_.add_partial(items_, cursor_);
    if (remainder > 0) {
      spawn_drop(make_stack(cursor_.item_id, remainder), player.x,
                 player.y + 1.0, player.z, 0.0, 0.1, 0.0, kPickupDelayTicks);
    }
    cursor_.clear();
  }
  screen_ = REIM_SCREEN_HUD;
  active_container_ = BlockPos{};
  has_active_container_ = false;
  hovered_slot_ = -1;
  ++screen_revision_;
}

void Gameplay::return_grid_to_player(const core::Player& player) {
  for (ItemStack& stack : grid_.slots) {
    if (stack.empty()) continue;
    const int remainder = inventory_.add_partial(items_, stack);
    if (remainder > 0) {
      spawn_drop(make_stack(stack.item_id, remainder), player.x,
                 player.y + 1.0, player.z, 0.0, 0.1, 0.0, kPickupDelayTicks);
    }
    stack.clear();
  }
  craft_result_.clear();
}

void Gameplay::recompute_grid() {
  craft_result_.clear();
  if (screen_ != REIM_SCREEN_INVENTORY &&
      screen_ != REIM_SCREEN_CRAFTING_TABLE) {
    return;
  }
  recipes_.match_crafting(items_, grid_, craft_result_);
}

void Gameplay::left_click(ItemStack& target, bool output_only) {
  if (cursor_.empty()) {
    if (target.empty()) return;
    const int limit = max_stack_of(items_, target.item_id);
    if (target.count <= limit) {
      cursor_ = target;
      target.clear();
    } else {
      cursor_ = target;
      cursor_.count = limit;
      target.count -= limit;
    }
    return;
  }
  if (output_only) return;
  if (target.empty()) {
    target = cursor_;
    cursor_.clear();
    return;
  }
  if (target.item_id == cursor_.item_id && target.damage == cursor_.damage) {
    const int limit = max_stack_of(items_, target.item_id);
    const int room = limit - target.count;
    const int moved = std::min(room, cursor_.count);
    if (moved > 0) {
      target.count += moved;
      cursor_.count -= moved;
      if (cursor_.count <= 0) cursor_.clear();
    }
    return;
  }
  std::swap(target, cursor_);
}

void Gameplay::right_click(ItemStack& target, bool output_only) {
  if (cursor_.empty()) {
    if (target.empty()) return;
    const int take = std::max(1, (target.count + 1) / 2);
    cursor_ = target;
    cursor_.count = take;
    target.count -= take;
    if (target.count <= 0) target.clear();
    return;
  }
  if (output_only) return;
  if (target.empty()) {
    target = cursor_;
    target.count = 1;
    --cursor_.count;
    if (cursor_.count <= 0) cursor_.clear();
    return;
  }
  if (target.item_id == cursor_.item_id && target.damage == cursor_.damage &&
      target.count < max_stack_of(items_, target.item_id)) {
    ++target.count;
    --cursor_.count;
    if (cursor_.count <= 0) cursor_.clear();
  }
}

bool Gameplay::click_slot(int slot, int button, uint32_t mods,
                          std::string& error) {
  if (!valid_slot(screen_, slot)) {
    error = "slot " + std::to_string(slot) + " is not part of screen " +
            std::to_string(screen_);
    return false;
  }
  if (button != 0 && button != 1) {
    error = "only left/right clicks are supported";
    return false;
  }
  const bool shift = (mods & REIM_MOD_SHIFT) != 0;
  if (is_result_slot(screen_, slot)) {
    if (shift) return quick_craft_all(error);
    if (craft_result_.empty()) {
      error = "no matching recipe";
      return false;
    }
    if (cursor_.empty()) {
      cursor_ = craft_result_;
    } else if (cursor_.item_id == craft_result_.item_id &&
               cursor_.damage == craft_result_.damage &&
               cursor_.count + craft_result_.count <=
                   max_stack_of(items_, cursor_.item_id)) {
      cursor_.count += craft_result_.count;
    } else {
      error = "cursor cannot accept the recipe result";
      return false;
    }
    recipes_.consume_crafting(grid_);
    ++crafts_taken_;
    recompute_grid();
    return true;
  }
  ItemStack* target = slot_ref(screen_, slot);
  if (target == nullptr) {
    error = "slot is not resolvable";
    return false;
  }
  const bool output_only = is_output_only(screen_, slot);
  if (output_only && !cursor_.empty()) {
    error = "output slots do not accept items";
    return false;
  }
  if (shift) return quick_move(slot, error);
  if (button == 0) {
    left_click(*target, output_only);
  } else {
    right_click(*target, output_only);
  }
  if (screen_ == REIM_SCREEN_INVENTORY || screen_ == REIM_SCREEN_CRAFTING_TABLE) {
    recompute_grid();
  }
  return true;
}

bool Gameplay::move_between(ItemStack& source, ItemStack* targets, size_t count) {
  if (source.empty()) return true;
  const int limit = max_stack_of(items_, source.item_id);
  for (size_t i = 0; i < count && !source.empty(); ++i) {
    ItemStack& target = targets[i];
    if (target.empty()) continue;
    if (target.item_id != source.item_id || target.damage != source.damage) continue;
    if (target.count >= limit) continue;
    const int moved = std::min(limit - target.count, source.count);
    target.count += moved;
    source.count -= moved;
  }
  for (size_t i = 0; i < count && !source.empty(); ++i) {
    ItemStack& target = targets[i];
    if (!target.empty()) continue;
    target = source;
    target.count = std::min(source.count, limit);
    source.count -= target.count;
  }
  if (source.count <= 0) source.clear();
  return source.empty();
}

bool Gameplay::quick_move(int slot, std::string& error) {
  if (is_result_slot(screen_, slot)) return quick_craft_all(error);
  ItemStack* source = slot_ref(screen_, slot);
  if (source == nullptr || source->empty()) return true;

  if (screen_ == REIM_SCREEN_INVENTORY || screen_ == REIM_SCREEN_CRAFTING_TABLE) {
    if (slot >= 1 && slot <= 9 && screen_ == REIM_SCREEN_CRAFTING_TABLE) {
      move_between(*source, inventory_.slots.data(), inventory_.slots.size());
      return true;
    }
    if (slot >= 1 && slot <= 4 && screen_ == REIM_SCREEN_INVENTORY) {
      move_between(*source, inventory_.slots.data(), inventory_.slots.size());
      return true;
    }
    if (is_item_slot_number(screen_, slot)) {
      const bool in_hotbar = (screen_ == REIM_SCREEN_INVENTORY && slot >= 36) ||
                             (screen_ == REIM_SCREEN_CRAFTING_TABLE && slot >= 37);
      if (in_hotbar) {
        move_between(*source, inventory_.slots.data() + 9, kMainSlots);
      } else {
        move_between(*source, inventory_.slots.data(), kHotbarSlots);
      }
      return true;
    }
    return true;
  }

  if (screen_ == REIM_SCREEN_CHEST) {
    BlockEntity* entity = has_active_container_ ? container_at(active_container_)
                                                : nullptr;
    if (entity == nullptr) {
      error = "chest state is missing";
      return false;
    }
    if (slot <= 26) {
      move_between(*source, inventory_.slots.data(), inventory_.slots.size());
    } else {
      move_between(*source, entity->chest.slots.data(), entity->chest.slots.size());
    }
    return true;
  }

  if (screen_ == REIM_SCREEN_FURNACE) {
    BlockEntity* entity = has_active_container_ ? container_at(active_container_)
                                                : nullptr;
    if (entity == nullptr) {
      error = "furnace state is missing";
      return false;
    }
    if (slot <= 2) {
      move_between(*source, inventory_.slots.data(), inventory_.slots.size());
      return true;
    }
    ItemStack probe;
    if (recipes_.smelt(source->item_id, probe)) {
      int room = max_stack_of(items_, source->item_id) - entity->furnace.input.count;
      if (entity->furnace.input.item_id == 0 ||
          (entity->furnace.input.item_id == source->item_id &&
           room > 0)) {
        move_between(*source, &entity->furnace.input, 1);
        return true;
      }
    }
    if (recipes_.fuel_ticks(items_, *source) > 0) {
      move_between(*source, &entity->furnace.fuel, 1);
    }
    return true;
  }
  return true;
}

bool Gameplay::quick_craft_all(std::string& error) {
  if (craft_result_.empty()) {
    error = "no matching recipe";
    return false;
  }
  int guard = 0;
  while (!craft_result_.empty() && guard++ < 256) {
    if (!inventory_.can_add(items_, craft_result_)) break;
    inventory_.add(items_, craft_result_);
    recipes_.consume_crafting(grid_);
    ++crafts_taken_;
    recompute_grid();
  }
  if (guard >= 256) {
    error = "craft loop guard reached";
    return false;
  }
  return true;
}

bool Gameplay::valid_slot(uint32_t screen, int slot) const {
  return slot_ref(screen, slot) != nullptr;
}

bool Gameplay::is_result_slot(uint32_t screen, int slot) const {
  return (screen == REIM_SCREEN_INVENTORY || screen == REIM_SCREEN_CRAFTING_TABLE) &&
         slot == 0;
}

bool Gameplay::is_output_only(uint32_t screen, int slot) const {
  return is_result_slot(screen, slot) ||
         (screen == REIM_SCREEN_FURNACE && slot == 2);
}

const ItemStack* Gameplay::slot_ref(uint32_t screen, int slot) const {
  return const_cast<Gameplay*>(this)->slot_ref(screen, slot);
}

ItemStack* Gameplay::slot_ref(uint32_t screen, int slot) {
  if (screen == REIM_SCREEN_HUD) {
    if (slot < 0 || slot > 8) return nullptr;
    return &inventory_.slots[static_cast<size_t>(slot)];
  }
  switch (screen) {
    case REIM_SCREEN_INVENTORY:
      if (slot == 0) return &craft_result_;
      if (slot >= 1 && slot <= 4) {
        if (grid_.size != 2) return nullptr;
        return &grid_.slots[static_cast<size_t>(slot - 1)];
      }
      if (slot >= 9 && slot <= 35) return &inventory_.slots[static_cast<size_t>(slot)];
      if (slot >= 36 && slot <= 44) {
        return &inventory_.slots[static_cast<size_t>(slot - 36)];
      }
      return nullptr;
    case REIM_SCREEN_CRAFTING_TABLE:
      if (slot == 0) return &craft_result_;
      if (slot >= 1 && slot <= 9) {
        if (grid_.size != 3) return nullptr;
        return &grid_.slots[static_cast<size_t>(slot - 1)];
      }
      if (slot >= 10 && slot <= 36) {
        return &inventory_.slots[static_cast<size_t>(slot - 1)];
      }
      if (slot >= 37 && slot <= 45) {
        return &inventory_.slots[static_cast<size_t>(slot - 37)];
      }
      return nullptr;
    case REIM_SCREEN_FURNACE: {
      BlockEntity* entity =
          has_active_container_ ? container_at(active_container_) : nullptr;
      if (entity == nullptr) return nullptr;
      if (slot == 0) return &entity->furnace.input;
      if (slot == 1) return &entity->furnace.fuel;
      if (slot == 2) return &entity->furnace.output;
      if (slot >= 3 && slot <= 29) {
        return &inventory_.slots[static_cast<size_t>(slot + 6)];
      }
      if (slot >= 30 && slot <= 38) {
        return &inventory_.slots[static_cast<size_t>(slot - 30)];
      }
      return nullptr;
    }
    case REIM_SCREEN_CHEST: {
      BlockEntity* entity =
          has_active_container_ ? container_at(active_container_) : nullptr;
      if (entity == nullptr) return nullptr;
      if (slot >= 0 && slot <= 26) {
        return &entity->chest.slots[static_cast<size_t>(slot)];
      }
      if (slot >= 27 && slot <= 53) {
        return &inventory_.slots[static_cast<size_t>(slot - 18)];
      }
      if (slot >= 54 && slot <= 62) {
        return &inventory_.slots[static_cast<size_t>(slot - 54)];
      }
      return nullptr;
    }
    default:
      return nullptr;
  }
}

BlockEntity* Gameplay::container_at(const BlockPos& pos) {
  const auto found = containers_.find(pos);
  return found == containers_.end() ? nullptr : &found->second;
}

BlockEntity& Gameplay::ensure_container(const BlockPos& pos, ContainerKind kind) {
  BlockEntity& entity = containers_[pos];
  if (entity.kind == ContainerKind::none) entity.kind = kind;
  return entity;
}

void Gameplay::tick_containers() {
  for (auto& [pos, entity] : containers_) {
    (void)pos;
    if (entity.kind != ContainerKind::furnace) continue;
    FurnaceState& furnace = entity.furnace;
    ItemStack smelt_output;
    const bool has_recipe = !furnace.input.empty() &&
                            recipes_.smelt(furnace.input.item_id, smelt_output);
    const int output_limit = smelt_output.item_id == 0
                                 ? 1
                                 : max_stack_of(items_, smelt_output.item_id);
    const bool output_ok =
        has_recipe &&
        (furnace.output.empty() ||
         (furnace.output.item_id == smelt_output.item_id &&
          furnace.output.count + smelt_output.count <= output_limit));
    if (furnace.burn_time == 0 && output_ok && !furnace.fuel.empty()) {
      const int ticks = recipes_.fuel_ticks(items_, furnace.fuel);
      if (ticks > 0) {
        furnace.burn_total = ticks;
        furnace.burn_time = ticks;
        --furnace.fuel.count;
        if (furnace.fuel.count <= 0) furnace.fuel.clear();
      }
    }
    if (furnace.burn_time > 0) {
      --furnace.burn_time;
      if (output_ok) {
        ++furnace.cook_time;
        if (furnace.cook_time >= kCookTicks) {
          if (furnace.output.empty()) {
            furnace.output = smelt_output;
          } else {
            furnace.output.count += smelt_output.count;
          }
          --furnace.input.count;
          if (furnace.input.count <= 0) furnace.input.clear();
          furnace.cook_time = 0;
          ++smelts_completed_;
        }
      } else {
        furnace.cook_time = 0;
      }
    } else {
      furnace.cook_time = 0;
    }
  }
}

void Gameplay::tick(core::Player& player) {
  ++tick_;
  if (!has_spawn_point_) {
    spawn_point_ = player;
    has_spawn_point_ = true;
  }
  tick_sounds_.clear();
  /* Sounds emitted by action handlers between ticks are pending; fold them
   * into this tick so a break/place event is not cleared before publication. */
  tick_sounds_.insert(tick_sounds_.end(), pending_sounds_.begin(),
                      pending_sounds_.end());
  pending_sounds_.clear();
  survival_begin_tick(survival_);
  if (want_respawn_) {
    want_respawn_ = false;
    if (survival_.dead) do_respawn(player);
  }
  if (attack_cooldown_ < 1.0f) {
    attack_cooldown_ = std::min(1.0f, attack_cooldown_ + 0.1f);
  }
  if (use_held_) {
    ++use_ticks_;
  } else {
    use_ticks_ = 0;
  }

  if (!survival_.dead && world_ != nullptr) {
    EntityEvents events;
    entities_.tick(player, world_->environment(), difficulty_, events);
    apply_entity_events(events, player);
    tick_survival(player);
  } else {
    survival_tick(survival_, SurvivalContext{});
  }
  if (survival_.dead && screen_ != REIM_SCREEN_DEATH) finish_death(player);

  target_ = raycast(player);
  target_entity_ = entities_.raycast(player, 3.0);
  if (target_.valid && target_entity_.valid &&
      target_.distance <= target_entity_.distance) {
    target_entity_.valid = false;
  }

  /* Held mining progresses only while the HUD is active and the target is
   * stable; releasing resets it (M3-D2). */
  if (break_held_ && screen_ == REIM_SCREEN_HUD && !survival_.dead) {
    const Hit current = target_;
    if (!current.valid) {
      break_target_ = Hit{};
      break_progress_ = 0.0f;
    } else {
      if (!break_target_.valid || break_target_.x != current.x ||
          break_target_.y != current.y || break_target_.z != current.z) {
        break_target_ = current;
        const BlockRules rules = block_rules(current.block_name);
        const bool harvest = can_harvest(rules, inventory_.held(), items_);
        const int ticks =
            break_ticks(rules, inventory_.held(), items_, harvest);
        break_total_ticks_ = ticks;
        break_progress_ = 0.0f;
      }
      if (break_total_ticks_ > 0) {
        break_progress_ += 1.0f / static_cast<float>(break_total_ticks_);
        if (break_progress_ >= 1.0f) {
          std::string error;
          if (!remove_block_and_drop(break_target_, player, error)) {
            reim::log::warn("gameplay", "held break failed: " + error);
          }
          break_progress_ = 0.0f;
          break_target_ = Hit{};
          target_ = raycast(player);
        }
      } else {
        break_progress_ = 0.0f;
      }
    }
  } else {
    break_progress_ = 0.0f;
  }

  tick_drops(player);
  tick_containers();
  particles_.tick();
  if (world_ != nullptr) {
    particles_.emit_weather(player, world_->environment().weather);
  }

  /* I3: material-agnostic footstep cadence while walking on the ground. */
  if (player.on_ground && !survival_.dead) {
    if (last_step_x_ == 0.0 && last_step_z_ == 0.0) {
      last_step_x_ = player.x;
      last_step_z_ = player.z;
    }
    const double moved = std::sqrt((player.x - last_step_x_) * (player.x - last_step_x_) +
                                   (player.z - last_step_z_) * (player.z - last_step_z_));
    if (moved > 0.15) {
      if (++step_timer_ >= 8) {
        emit_sound(kSoundFootstep, player.x, player.y + 0.1, player.z, 0.35f, 0.9f);
        step_timer_ = 0;
        last_step_x_ = player.x;
        last_step_z_ = player.z;
      }
    } else {
      step_timer_ = 0;
    }
  } else {
    step_timer_ = 0;
    last_step_x_ = player.x;
    last_step_z_ = player.z;
  }

  if (has_active_container_) {
    const data::BlockDef& def =
        world_->blocks().def(world_->block_id_at(active_container_.x,
                                                active_container_.y,
                                                active_container_.z));
    const std::string_view bare = bare_name(def.name);
    if (bare != "furnace" && bare != "blast_furnace" && bare != "smoker" &&
        bare != "chest" && bare != "barrel" && bare != "trapped_chest") {
      close_screen(player);
    }
  }

  tick_sounds_.insert(tick_sounds_.end(), pending_sounds_.begin(),
                      pending_sounds_.end());
  pending_sounds_.clear();
}

void Gameplay::emit_sound(uint32_t id, double x, double y, double z,
                          float volume, float pitch) {
  EntitySoundEvent sound;
  sound.id = id;
  sound.x = x;
  sound.y = y;
  sound.z = z;
  sound.volume = volume;
  sound.pitch = pitch;
  pending_sounds_.push_back(sound);
}

SurvivalContext Gameplay::survival_context(const core::Player& player) const {
  SurvivalContext context;
  context.in_water = player.in_water;
  context.on_ground = player.on_ground;
  context.vy = player.vy;
  context.y = player.y;
  context.moving =
      std::fabs(player.vx) + std::fabs(player.vz) > 0.001;
  context.sprinting = (player.flags & 0x4u) != 0;
  context.difficulty = difficulty_;
  if (world_ == nullptr) return context;
  const int x = static_cast<int>(std::floor(player.x));
  const int y = static_cast<int>(std::floor(player.y));
  const int z = static_cast<int>(std::floor(player.z));
  world::BlockState feet;
  if (world_->block_state_at(x, y, z, feet)) {
    const std::string_view bare = bare_name(feet.name);
    context.lava_at_feet = bare == "lava";
    context.fire_at_feet = bare == "fire" || bare == "soul_fire";
  }
  const int eye_y =
      static_cast<int>(std::floor(player.y + core::kPlayerEyeHeight));
  context.suffocating = world_->collides(x, eye_y, z);
  return context;
}

void Gameplay::apply_player_damage(core::Player& player, float amount,
                                   DamageCause cause) {
  if (gamemode_ == REIM_GAMEMODE_CREATIVE ||
      gamemode_ == REIM_GAMEMODE_SPECTATOR) {
    return;
  }
  if (survival_damage(survival_, amount, cause)) {
    emit_sound(kSoundPlayerHurt, player.x, player.y + 1.5, player.z, 0.8f, 1.0f);
  }
}

void Gameplay::tick_survival(core::Player& player) {
  const SurvivalContext context = survival_context(player);
  survival_tick(survival_, context);
  if (survival_.hurt_this_tick &&
      survival_.last_damage != DamageCause::mob &&
      survival_.last_damage != DamageCause::explosion) {
    emit_sound(kSoundPlayerHurt, player.x, player.y + 1.5, player.z, 0.7f, 1.0f);
  }
  if (survival_.died_this_tick) {
    emit_sound(kSoundPlayerDeath, player.x, player.y + 1.0, player.z, 1.0f, 1.0f);
  }
}

void Gameplay::apply_entity_events(EntityEvents& events, core::Player& player) {
  for (const EntityBlockEdit& edit : events.block_edits) {
    world::BlockState air;
    air.name = "minecraft:air";
    std::string error;
    world_->set_block(edit.x, edit.y, edit.z, air, error);
  }
  for (const EntityDropEvent& drop : events.drops) {
    spawn_drop(drop.stack, drop.x, drop.y, drop.z, 0.0, 0.1, 0.0,
               kPickupDelayTicks);
  }
  for (const EntitySoundEvent& sound : events.sounds) {
    emit_sound(sound.id, sound.x, sound.y, sound.z, sound.volume, sound.pitch);
    if (sound.id == kSoundExplosion) {
      particles_.emit_explosion(sound.x, sound.y, sound.z);
    }
  }
  for (const MobDamageEvent& damage : events.player_damage) {
    if (survival_.dead) break;
    apply_player_damage(player, damage.amount, damage.cause);
    double dx = player.x - damage.from_x;
    double dz = player.z - damage.from_z;
    const double length = std::sqrt(dx * dx + dz * dz);
    if (length > 1e-6) {
      dx /= length;
      dz /= length;
    } else {
      dx = 0.0;
      dz = 1.0;
    }
    player.vx += dx * 0.25;
    player.vz += dz * 0.25;
    if (player.on_ground) player.vy = std::max(player.vy, 0.20);
  }
  if (events.kills > 0) mobs_killed_ += static_cast<uint64_t>(events.kills);
  if (events.spawned > 0) mobs_spawned_ += static_cast<uint64_t>(events.spawned);
  if (events.xp > 0) survival_add_xp(survival_, static_cast<uint32_t>(events.xp));
}

void Gameplay::drop_inventory_on_death(const core::Player& player) {
  auto drop_stack = [&](ItemStack& stack) {
    if (stack.empty()) return;
    const double angle = static_cast<double>(next_random(rng_) % 628) / 100.0;
    spawn_drop(stack, player.x + std::sin(angle) * 0.4, player.y + 1.2,
               player.z + std::cos(angle) * 0.4, std::sin(angle) * 0.15, 0.15,
               std::cos(angle) * 0.15, kPickupDelayTicks);
    stack.clear();
  };
  for (ItemStack& stack : inventory_.slots) drop_stack(stack);
  for (ItemStack& stack : grid_.slots) drop_stack(stack);
  craft_result_.clear();
  drop_stack(cursor_);
}

void Gameplay::finish_death(const core::Player& player) {
  if (screen_ == REIM_SCREEN_DEATH) return;
  survival_.dead = true;
  if (!keep_inventory_) drop_inventory_on_death(player);
  cursor_.clear();
  grid_.clear();
  craft_result_.clear();
  containers_.clear();
  has_active_container_ = false;
  break_held_ = false;
  break_target_ = Hit{};
  break_progress_ = 0.0f;
  use_held_ = false;
  screen_ = REIM_SCREEN_DEATH;
  ++screen_revision_;
  hovered_slot_ = -1;
}

void Gameplay::do_respawn(core::Player& player) {
  survival_respawn(survival_);
  if (has_spawn_point_) {
    const float yaw = spawn_point_.yaw;
    const float pitch = spawn_point_.pitch;
    player = core::Player{};
    player.x = spawn_point_.x;
    player.y = spawn_point_.y;
    player.z = spawn_point_.z;
    player.yaw = yaw;
    player.pitch = pitch;
  }
  screen_ = REIM_SCREEN_HUD;
  ++screen_revision_;
  hovered_slot_ = -1;
  active_container_ = BlockPos{};
  has_active_container_ = false;
  want_respawn_ = false;
}

bool Gameplay::attack_entity(const EntityHit& hit, core::Player& player,
                             std::string& error) {
  if (attack_cooldown_ < 1.0f) {
    error = "attack cooldown is not ready";
    return false;
  }
  const ItemStack& held = inventory_.held();
  float damage = held.empty() ? 1.0f : items_.def(held.item_id).attack_damage;
  const bool critical = player.vy < -0.08 && !player.on_ground;
  if (critical) damage *= 1.5f;
  EntityEvents events;
  if (!entities_.damage(hit.id, damage, player, events)) {
    error = "entity target is not damageable";
    return false;
  }
  apply_entity_events(events, player);
  attack_cooldown_ = 0.0f;
  emit_sound(kSoundAttack, player.x, player.y + 1.4, player.z, 0.7f, 1.0f);
  return true;
}

bool Gameplay::break_block(const Hit& hit, const core::Player& player,
                           std::string& error) {
  return remove_block_and_drop(hit, player, error);
}

bool Gameplay::remove_block_and_drop(const Hit& hit, const core::Player& player,
                                     std::string& error) {
  const BlockRules rules = block_rules(hit.block_name);
  if (rules.unbreakable) {
    error = "block is unbreakable";
    return false;
  }
  const std::string_view bare = bare_name(hit.block_name);
  if (bare == "bedrock" || is_air(hit.block_name)) {
    error = "block cannot be broken";
    return false;
  }
  const bool harvest = can_harvest(rules, inventory_.held(), items_);
  const BlockPos pos{hit.x, hit.y, hit.z};
  if (auto* entity = container_at(pos); entity != nullptr) {
    if (entity->kind == ContainerKind::chest) {
      for (ItemStack& stack : entity->chest.slots) {
        if (stack.empty()) continue;
        spawn_drop(stack, hit.x + 0.5, hit.y + 0.5, hit.z + 0.5, 0.0, 0.15, 0.0,
                   kPickupDelayTicks);
      }
    } else if (entity->kind == ContainerKind::furnace) {
      for (ItemStack* stack : {&entity->furnace.input, &entity->furnace.fuel,
                               &entity->furnace.output}) {
        if (stack->empty()) continue;
        spawn_drop(*stack, hit.x + 0.5, hit.y + 0.5, hit.z + 0.5, 0.0, 0.15,
                   0.0, kPickupDelayTicks);
      }
    }
    containers_.erase(pos);
    if (has_active_container_ && active_container_ == pos) {
      close_screen(player);
    }
  }

  world::BlockState air;
  air.name = "minecraft:air";
  std::string set_error;
  if (!world_->set_block(hit.x, hit.y, hit.z, air, set_error)) {
    error = "cannot remove block: " + set_error;
    return false;
  }
  emit_sound(kSoundBlockBreak, hit.x + 0.5, hit.y + 0.5, hit.z + 0.5, 1.0f, 0.9f);
  {
    const uint32_t name_hash = static_cast<uint32_t>(
        std::hash<std::string_view>{}(bare_name(hit.block_name)) & 0xFFFFFFu);
    const uint8_t r = static_cast<uint8_t>(80 + (name_hash & 0x7F));
    const uint8_t g = static_cast<uint8_t>(70 + ((name_hash >> 8) & 0x7F));
    const uint8_t b = static_cast<uint8_t>(60 + ((name_hash >> 16) & 0x7F));
    particles_.emit_block_break(hit.x + 0.5, hit.y + 0.5, hit.z + 0.5,
                                (0xFFu << 24) | (static_cast<uint32_t>(b) << 16) |
                                    (static_cast<uint32_t>(g) << 8) | r,
                                12);
  }

  const std::vector<ItemDrop> drops =
      block_drops(items_, hit.block_name, harvest, rng_);
  for (const ItemDrop& drop : drops) {
    if (drop.item_id == 0) continue;
    spawn_drop(make_stack(drop.item_id, drop.min_count), hit.x + 0.5,
               hit.y + 0.5, hit.z + 0.5, 0.0, 0.15, 0.0, kPickupDelayTicks);
  }
  if (!inventory_.held().empty()) {
    const ItemDef& held = items_.def(inventory_.held().item_id);
    if (held.is_tool()) {
      damage_held(1);
    }
  }
  ++blocks_broken_;
  return true;
}

void Gameplay::damage_held(int amount) {
  ItemStack& held = inventory_.held();
  if (held.empty()) return;
  const ItemDef& def = items_.def(held.item_id);
  if (def.max_damage <= 0 || !def.is_tool()) return;
  held.damage += amount;
  held.flags |= 0x00000001u;
  if (held.damage >= def.max_damage) {
    reim::log::info("gameplay", "tool broke: " + def.name);
    held.clear();
  }
}

bool Gameplay::place_block(const Hit& hit, const core::Player& player,
                           std::string& error) {
  ItemStack& held = inventory_.held();
  if (held.empty()) {
    error = "held item is empty";
    return false;
  }
  const ItemDef& def = items_.def(held.item_id);
  if (!def.is_block() || def.block_id == 0) {
    error = "held item is not a placeable block";
    return false;
  }
  int nx = 0;
  int ny = 0;
  int nz = 0;
  face_normal(hit.face, nx, ny, nz);
  const int bx = hit.x + nx;
  const int by = hit.y + ny;
  const int bz = hit.z + nz;
  world::BlockState existing;
  if (world_->block_state_at(bx, by, bz, existing) && !is_replaceable(existing.name)) {
    error = "placement target is occupied";
    return false;
  }

  const data::BlockDef& block = world_->blocks().def(def.block_id);
  if (block.collision) {
    double x0 = 0.0, y0 = 0.0, z0 = 0.0, x1 = 0.0, y1 = 0.0, z1 = 0.0;
    player_bounds(player, x0, y0, z0, x1, y1, z1);
    if (box_overlaps_block(x0, y0, z0, x1, y1, z1, bx, by, bz)) {
      error = "player occupies the placement position";
      return false;
    }
  }
  const world::BlockState state = placement_state(items_, held.item_id, hit, player);
  std::string set_error;
  if (!world_->set_block(bx, by, bz, state, set_error)) {
    error = "cannot place block: " + set_error;
    return false;
  }
  --held.count;
  if (held.count <= 0) held.clear();
  ++blocks_placed_;
  emit_sound(kSoundBlockPlace, bx + 0.5, by + 0.5, bz + 0.5, 1.0f, 0.9f);
  particles_.emit_block_break(bx + 0.5, by + 0.5, bz + 0.5, 0xFFB0B0B0u, 4);
  return true;
}

bool Gameplay::use_once(const core::Player& player, std::string& error) {
  if (screen_ != REIM_SCREEN_HUD) {
    error = "a screen is already open";
    return false;
  }
  Hit hit = target_;
  if (hit.valid) {
    const std::string_view bare = bare_name(hit.block_name);
    if (bare == "crafting_table") {
      open_crafting_table();
      return true;
    }
    if (bare == "furnace" || bare == "blast_furnace" || bare == "smoker" ||
        bare == "chest" || bare == "barrel" || bare == "trapped_chest") {
      return open_container(hit, error);
    }
    if (bare == "nether_portal") {
      const uint32_t current = world_ != nullptr ? world_->environment().dimension : 0u;
      dimension_request_ = current == REIM_DIMENSION_NETHER
                               ? REIM_DIMENSION_OVERWORLD
                               : REIM_DIMENSION_NETHER;
      return true;
    }
  }
  const ItemStack& held = inventory_.held();
  if (!held.empty()) {
    const ItemDef& def = items_.def(held.item_id);
    if (def.is_food()) {
      if (survival_.food >= 20.0f) {
        error = "not hungry";
        return false;
      }
      ItemStack& mutable_held = inventory_.held();
      survival_.food = std::min(20.0f, survival_.food + static_cast<float>(def.food));
      survival_.saturation =
          std::min(survival_.food, survival_.saturation + def.saturation);
      survival_add_exhaustion(survival_, 3.0f);
      --mutable_held.count;
      if (mutable_held.count <= 0) mutable_held.clear();
      return true;
    }
    if (def.is_block() && hit.valid) {
      return place_block(hit, player, error);
    }
  }
  /* Using air with an empty/non-usable hand is a legal no-op (M3-D04). */
  error.clear();
  return true;
}

bool Gameplay::drop_from_selected(const core::Player& player, bool whole) {
  ItemStack& held = inventory_.held();
  if (held.empty()) return false;
  const int count = whole ? held.count : 1;
  ItemStack drop = held;
  drop.count = count;
  held.count -= count;
  if (held.count <= 0) held.clear();
  double fx = 0.0, fy = 0.0, fz = 0.0;
  forward_vector(player, fx, fy, fz);
  spawn_drop(drop, player.x + fx * 0.4, player.y + 1.2, player.z + fz * 0.4,
             fx * 0.3, 0.1, fz * 0.3, kPickupDelayTicks);
  return true;
}

void Gameplay::spawn_drop(ItemStack stack, double x, double y, double z,
                          double vx, double vy, double vz, int pickup_delay) {
  if (stack.empty()) return;
  DroppedItem drop;
  drop.id = next_drop_id_++;
  drop.stack = stack;
  drop.x = x;
  drop.y = y;
  drop.z = z;
  drop.vx = vx;
  drop.vy = vy;
  drop.vz = vz;
  drop.pickup_delay = pickup_delay;
  items_dropped_ += static_cast<uint64_t>(std::max(0, stack.count));
  drops_.push_back(drop);
}

void Gameplay::tick_drops(const core::Player& player) {
  const double px = player.x;
  const double py = player.y + kPickupCenterHeight;
  const double pz = player.z;
  std::vector<bool> remove(drops_.size(), false);
  for (size_t i = 0; i < drops_.size(); ++i) {
    DroppedItem& drop = drops_[i];
    ++drop.age;
    if (drop.pickup_delay > 0) --drop.pickup_delay;
    drop.vy -= 0.04;
    drop.vy = std::max(drop.vy, -0.6);
    auto axis_move = [&](double& position, double delta, int axis) {
      if (delta == 0.0) return;
      const double next = position + delta;
      const int cx = static_cast<int>(std::floor(axis == 0 ? next : drop.x));
      const int cy = static_cast<int>(std::floor(axis == 1 ? next : drop.y));
      const int cz = static_cast<int>(std::floor(axis == 2 ? next : drop.z));
      if (!world_->collides(cx, cy, cz)) {
        position = next;
      } else if (axis == 1) {
        drop.vy = 0.0;
      } else {
        drop.vx = 0.0;
        drop.vz = 0.0;
      }
    };
    axis_move(drop.x, drop.vx, 0);
    axis_move(drop.z, drop.vz, 2);
    axis_move(drop.y, drop.vy, 1);
    drop.vx *= 0.98;
    drop.vz *= 0.98;

    const double dx = drop.x - px;
    const double dy = drop.y - py;
    const double dz = drop.z - pz;
    if (drop.pickup_delay <= 0 &&
        dx * dx + dy * dy + dz * dz <= kPickupRadius * kPickupRadius) {
      const int before = drop.stack.count;
      const int remainder = inventory_.add_partial(items_, drop.stack);
      const int picked = before - remainder;
      items_picked_ += static_cast<uint64_t>(picked);
      if (picked > 0) {
        emit_sound(kSoundPickup, player.x, player.y + 1.2, player.z, 0.4f, 1.6f);
      }
      if (remainder <= 0) {
        remove[i] = true;
        drop.stack.clear();
        continue;
      }
      drop.stack.count = remainder;
    }
    if (drop.age >= kDespawnTicks) {
      remove[i] = true;
    }
  }

  /* Merge compatible drops that ended up in the same spot (M3-D5). */
  for (size_t i = 0; i < drops_.size(); ++i) {
    if (remove[i] || drops_[i].stack.empty()) continue;
    const int limit = max_stack_of(items_, drops_[i].stack.item_id);
    for (size_t j = i + 1; j < drops_.size(); ++j) {
      if (remove[j] || drops_[j].stack.empty()) continue;
      if (drops_[i].stack.item_id != drops_[j].stack.item_id ||
          drops_[i].stack.damage != drops_[j].stack.damage) {
        continue;
      }
      if (drops_[i].stack.count >= limit) break;
      const double dx = drops_[i].x - drops_[j].x;
      const double dy = drops_[i].y - drops_[j].y;
      const double dz = drops_[i].z - drops_[j].z;
      if (dx * dx + dy * dy + dz * dz > 0.75 * 0.75) continue;
      const int moved =
          std::min(limit - drops_[i].stack.count, drops_[j].stack.count);
      drops_[i].stack.count += moved;
      drops_[j].stack.count -= moved;
      if (drops_[j].stack.count <= 0) {
        drops_[j].stack.clear();
        remove[j] = true;
      }
    }
  }

  size_t write = 0;
  for (size_t i = 0; i < drops_.size(); ++i) {
    if (remove[i]) continue;
    if (write != i) drops_[write] = drops_[i];
    ++write;
  }
  drops_.resize(write);
}

bool Gameplay::tooltip_for_slot(uint32_t screen, int slot, std::string& out) const {
  const ItemStack* stack = slot_ref(screen, slot);
  if (stack == nullptr || stack->empty()) return false;
  const ItemDef& def = items_.def(stack->item_id);
  out = def.display;
  if (stack->count > 1) out += " x" + std::to_string(stack->count);
  if (def.is_tool() && def.max_damage > 0) {
    out += "  " + std::to_string(def.max_damage - stack->damage) + "/" +
           std::to_string(def.max_damage);
  }
  return true;
}

GameView Gameplay::view(const core::Player& player) const {
  GameView view;
  view.screen = screen_;
  view.screen_revision = screen_revision_;
  view.selected_slot = static_cast<uint32_t>(inventory_.selected);
  view.held = inventory_.held();
  view.cursor = cursor_;
  int slots[64] = {};
  int count = 0;
  screen_slot_numbers(screen_, slots, 64, count);
  for (int i = 0; i < count && i < 64; ++i) {
    const ItemStack* stack = slot_ref(screen_, slots[i]);
    if (stack == nullptr) continue;
    UiSlot slot;
    slot.slot = static_cast<uint32_t>(slots[i]);
    slot.item = *stack;
    view.slots.push_back(slot);
  }
  for (const ItemStack& stack : inventory_.slots) {
    if (stack.empty()) continue;
    view.item_counts[items_.item_name(stack.item_id)] += stack.count;
  }
  for (const ItemStack& stack : grid_.slots) {
    if (stack.empty()) continue;
    view.item_counts[items_.item_name(stack.item_id)] += stack.count;
  }
  view.hovered_slot = hovered_slot_;
  view.hovered_element = hovered_element_;
  if (hovered_slot_ >= 0) {
    tooltip_for_slot(screen_, hovered_slot_, view.hovered_tooltip);
  }
  view.pointer_x = pointer_x_;
  view.pointer_y = pointer_y_;
  view.canvas_width = canvas_width_;
  view.canvas_height = canvas_height_;
  view.ui_scale = ui_scale_;
  view.pointer_visible = (screen_ != REIM_SCREEN_HUD);
  view.grid_size = grid_.size;
  view.grid = grid_.slots;
  view.craft_result = craft_result_;
  view.drops = drops_;

  if (target_.valid) {
    const BlockRules rules = block_rules(target_.block_name);
    view.interaction.target_type = REIM_INTERACTION_BLOCK;
    view.interaction.x = target_.x;
    view.interaction.y = target_.y;
    view.interaction.z = target_.z;
    view.interaction.block_id = target_.block_id;
    view.interaction.block_name = target_.block_name;
    view.interaction.face = target_.face;
    view.interaction.hit_x = target_.hit_x;
    view.interaction.hit_y = target_.hit_y;
    view.interaction.hit_z = target_.hit_z;
    view.interaction.can_break = !rules.unbreakable;
    const ItemDef& held = items_.def(inventory_.held().item_id);
    view.interaction.can_place =
        held.is_block() && screen_ == REIM_SCREEN_HUD;
    view.interaction.reach_distance = kReach;
    if (break_held_ && break_target_.valid) {
      view.interaction.break_progress = std::clamp(break_progress_, 0.0f, 1.0f);
    }
  }
  if (target_entity_.valid) {
    view.interaction.target_type = REIM_INTERACTION_ENTITY;
    view.interaction.entity_id = target_entity_.id;
    view.interaction.entity_type = static_cast<uint32_t>(target_entity_.type);
    view.interaction.hit_x = static_cast<float>(target_entity_.x);
    view.interaction.hit_y = static_cast<float>(target_entity_.y);
    view.interaction.hit_z = static_cast<float>(target_entity_.z);
    view.interaction.can_break = false;
    view.interaction.can_place = false;
  }
  view.interaction.attack_cooldown = attack_cooldown_;

  if (has_active_container_) {
    if (const BlockEntity* entity = containers_.find(active_container_) !=
                                            containers_.end()
                                        ? &containers_.at(active_container_)
                                        : nullptr;
        entity != nullptr) {
      if (entity->kind == ContainerKind::furnace) {
        view.furnace.valid = true;
        view.furnace.x = active_container_.x;
        view.furnace.y = active_container_.y;
        view.furnace.z = active_container_.z;
        view.furnace.input = entity->furnace.input;
        view.furnace.fuel = entity->furnace.fuel;
        view.furnace.output = entity->furnace.output;
        view.furnace.burn_time = entity->furnace.burn_time;
        view.furnace.burn_total = entity->furnace.burn_total;
        view.furnace.cook_time = entity->furnace.cook_time;
      } else if (entity->kind == ContainerKind::chest) {
        view.chest.valid = true;
        view.chest.x = active_container_.x;
        view.chest.y = active_container_.y;
        view.chest.z = active_container_.z;
        view.chest.slots = entity->chest.slots;
      }
    }
  }

  for (const BlockPos& pos : probe_positions_) {
    Probe probe;
    probe.x = pos.x;
    probe.y = pos.y;
    probe.z = pos.z;
    world::BlockState state;
    if (world_->block_state_at(pos.x, pos.y, pos.z, state)) {
      probe.block = state.name;
      for (const auto& [key, value] : state.properties) {
        probe.block += "[" + key + "=" + value + "]";
      }
      probe.biome = world_->biome_at(pos.x, pos.y, pos.z);
    } else {
      probe.block = "<unloaded>";
    }
    view.probes.push_back(std::move(probe));
  }
  view.food = survival_.food;
  view.health = survival_.health;
  view.max_health = survival_.max_health;
  view.saturation = survival_.saturation;
  view.exhaustion = survival_.exhaustion;
  view.air = survival_.air;
  view.max_air = survival_.max_air;
  view.xp_level = survival_.xp_level;
  view.xp_total = survival_.xp_total;
  view.xp_progress = survival_xp_progress(survival_);
  view.dead = survival_.dead;
  view.on_fire = survival_.fire_ticks > 0;
  view.deaths = survival_.deaths;
  view.damage_taken = survival_.damage_taken;
  view.damage_cause = damage_cause_name(survival_.last_damage);
  view.entities = entities_.view();
  view.entity_sounds = tick_sounds_;
  view.break_held = break_held_;
  view.use_held = use_held_;
  view.use_ticks = use_ticks_;
  view.blocks_broken = blocks_broken_;
  view.blocks_placed = blocks_placed_;
  view.items_dropped = items_dropped_;
  view.items_picked = items_picked_;
  view.crafts_taken = crafts_taken_;
  view.smelts_completed = smelts_completed_;
  view.mobs_killed = mobs_killed_;
  view.mobs_spawned = mobs_spawned_;
  (void)player;
  return view;
}

} // namespace reim::game
