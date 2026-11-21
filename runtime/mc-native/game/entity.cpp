#include "game/entity.h"

#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

#include "json.hpp"

namespace reim::game {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 0.08;
constexpr double kMaxFall = 1.2;
constexpr int kDespawnDistance = 64;
constexpr size_t kEntityCap = 40;
constexpr int kAttackCooldownTicks = 20;
constexpr int kCreeperFuseTicks = 30;
constexpr double kCreeperTrigger = 3.0;
constexpr double kExplosionRadius = 4.0;
constexpr int kExplosionBlockRadius = 2;

std::string_view bare_name(std::string_view name) {
  const size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

bool is_air_name(std::string_view name) {
  return name == "minecraft:air" || name == "air" || name.empty();
}

bool unbreakable_by_explosion(std::string_view name) {
  const std::string_view bare = bare_name(name);
  return bare == "bedrock" || bare == "obsidian" || bare == "barrier" ||
         bare == "command_block" || bare == "chain_command_block" ||
         bare == "repeating_command_block" || bare == "structure_block" ||
         bare == "jigsaw" || bare == "end_portal" || bare == "end_portal_frame" ||
         bare == "nether_portal" || bare == "light" || bare == "structure_void";
}

float difficulty_scale(uint32_t difficulty) {
  switch (difficulty) {
    case 0: return 0.0f;
    case 1: return 0.5f;
    case 3: return 1.5f;
    default: return 1.0f;
  }
}

const std::array<EntityTypeInfo, kEntityTypeCount>& type_table() {
  static const std::array<EntityTypeInfo, kEntityTypeCount> kTable{{
      {"minecraft:zombie", "Zombie", true, 0.30f, 1.95f, 0.10f, 20.0f, 3.0f, 5, 16.0f, 1.8f},
      {"minecraft:skeleton", "Skeleton", true, 0.30f, 1.95f, 0.10f, 20.0f, 2.0f, 5, 16.0f, 1.8f},
      {"minecraft:creeper", "Creeper", true, 0.30f, 1.70f, 0.10f, 20.0f, 0.0f, 5, 16.0f, 2.5f},
      {"minecraft:spider", "Spider", true, 0.65f, 0.90f, 0.14f, 16.0f, 2.0f, 5, 16.0f, 1.6f},
      {"minecraft:cow", "Cow", false, 0.45f, 1.40f, 0.07f, 10.0f, 0.0f, 1, 10.0f, 1.6f},
      {"minecraft:pig", "Pig", false, 0.45f, 0.90f, 0.08f, 10.0f, 0.0f, 1, 10.0f, 1.6f},
      {"minecraft:sheep", "Sheep", false, 0.45f, 1.30f, 0.07f, 8.0f, 0.0f, 1, 10.0f, 1.6f},
      {"minecraft:chicken", "Chicken", false, 0.20f, 0.70f, 0.10f, 4.0f, 0.0f, 1, 10.0f, 1.2f},
  }};
  return kTable;
}

uint32_t type_index(EntityType type) {
  const uint32_t value = static_cast<uint32_t>(type);
  return value >= 1 && value <= kEntityTypeCount ? value - 1 : 0;
}

} // namespace

const char* entity_type_name(EntityType type) {
  return type_table()[type_index(type)].name;
}

const char* entity_type_display(EntityType type) {
  return type_table()[type_index(type)].display;
}

EntityType entity_type_from_name(std::string_view name) {
  const std::string_view bare = bare_name(name);
  for (uint32_t i = 0; i < kEntityTypeCount; ++i) {
    const EntityType type = static_cast<EntityType>(i + 1);
    if (bare_name(entity_type_name(type)) == bare) return type;
  }
  return EntityType::zombie;
}

const EntityTypeInfo& entity_type_info(EntityType type) {
  return type_table()[type_index(type)];
}

void EntityManager::initialize(WorldAccess* world, const ItemTable* items,
                               uint64_t seed) {
  world_ = world;
  items_ = items;
  seed_ = seed;
  rng_ = seed != 0 ? seed : 0x9E3779B97F4A7C15ull;
  next_id_ = 1;
  tick_ = 0;
  spawn_timer_ = 0;
  entities_.clear();
}

void EntityManager::clear() {
  entities_.clear();
  next_id_ = 1;
}

uint32_t EntityManager::roll(uint32_t bound) {
  if (bound == 0) return 0;
  return static_cast<uint32_t>(next_random(rng_) % bound);
}

double EntityManager::roll01() {
  return static_cast<double>(next_random(rng_) >> 11) *
         (1.0 / 9007199254740992.0);
}

bool EntityManager::solid_at(int x, int y, int z) const {
  return world_ != nullptr && world_->collides(x, y, z);
}

bool EntityManager::liquid_at(int x, int y, int z) const {
  if (world_ == nullptr) return false;
  return world_->blocks().def(world_->block_id_at(x, y, z)).liquid;
}

bool EntityManager::collides(double x, double y, double z, float half,
                             float height) const {
  const double eps = 1e-6;
  const int x0 = static_cast<int>(std::floor(x - half));
  const int x1 = static_cast<int>(std::floor(x + half - eps));
  const int y0 = static_cast<int>(std::floor(y));
  const int y1 = static_cast<int>(std::floor(y + height - eps));
  const int z0 = static_cast<int>(std::floor(z - half));
  const int z1 = static_cast<int>(std::floor(z + half - eps));
  for (int by = y0; by <= y1; ++by) {
    for (int bx = x0; bx <= x1; ++bx) {
      for (int bz = z0; bz <= z1; ++bz) {
        if (solid_at(bx, by, bz)) return true;
      }
    }
  }
  return false;
}

void EntityManager::move_entity(Entity& entity, double dx, double dy, double dz) {
  const EntityTypeInfo& info = entity_type_info(entity.type);
  auto blocked = [&](double x, double y, double z) {
    return collides(x, y, z, info.half_width, info.height);
  };
  if (dx != 0.0) {
    if (!blocked(entity.x + dx, entity.y, entity.z)) {
      entity.x += dx;
    } else if (entity.on_ground &&
               !blocked(entity.x + dx, entity.y + 1.0, entity.z) &&
               !blocked(entity.x, entity.y + 1.0, entity.z)) {
      /* One-block step up (M3-F6); no path smoothing beyond this. */
      entity.x += dx;
      entity.y += 1.0;
    } else {
      entity.vx = 0.0;
      entity.path.clear();
    }
  }
  if (dz != 0.0) {
    if (!blocked(entity.x, entity.y, entity.z + dz)) {
      entity.z += dz;
    } else if (entity.on_ground &&
               !blocked(entity.x, entity.y + 1.0, entity.z + dz) &&
               !blocked(entity.x, entity.y + 1.0, entity.z)) {
      entity.z += dz;
      entity.y += 1.0;
    } else {
      entity.vz = 0.0;
      entity.path.clear();
    }
  }
  if (dy != 0.0) {
    const double ny = entity.y + dy;
    if (!blocked(entity.x, ny, entity.z)) {
      entity.y = ny;
    } else if (dy < 0.0) {
      entity.y = std::floor(ny) + 1.0;
      entity.vy = 0.0;
      entity.on_ground = true;
    } else {
      entity.vy = 0.0;
    }
  }
}

void EntityManager::physics(Entity& entity) {
  const EntityTypeInfo& info = entity_type_info(entity.type);
  const int fx = static_cast<int>(std::floor(entity.x));
  const int fy = static_cast<int>(std::floor(entity.y));
  const int fz = static_cast<int>(std::floor(entity.z));
  entity.in_water = liquid_at(fx, fy, fz) ||
                    liquid_at(fx, static_cast<int>(std::floor(
                                                  entity.y + info.height * 0.5)),
                              fz);
  if (entity.in_water) {
    entity.vy -= 0.02;
    entity.vy = std::max(entity.vy, -0.15);
    entity.vx *= 0.8;
    entity.vz *= 0.8;
  } else {
    entity.vy -= kGravity;
    entity.vy = std::max(entity.vy, -kMaxFall);
  }

  move_entity(entity, entity.vx, entity.vy, entity.vz);

  entity.on_ground = collides(entity.x, entity.y - 0.05, entity.z,
                              info.half_width, info.height);
  if (entity.on_ground && entity.vy < 0.0) entity.vy = 0.0;
  entity.vx *= 0.6;
  entity.vz *= 0.6;

  const double speed = std::sqrt(entity.vx * entity.vx + entity.vz * entity.vz);
  entity.limb_amount +=
      (static_cast<float>(std::min(1.0, speed * 6.0)) - entity.limb_amount) * 0.4f;
  entity.limb_swing += static_cast<float>(speed * 55.0);
}

bool EntityManager::exposed_to_sky(int x, int y, int z) const {
  for (int by = y + 1; by < y + 64; ++by) {
    if (solid_at(x, by, z)) return false;
  }
  return true;
}

uint64_t EntityManager::spawn(EntityType type, double x, double y, double z) {
  const EntityTypeInfo& info = entity_type_info(type);
  Entity entity;
  entity.id = next_id_++;
  entity.type = type;
  entity.x = x;
  entity.y = y;
  entity.z = z;
  entity.prev_x = x;
  entity.prev_y = y;
  entity.prev_z = z;
  entity.health = info.max_health;
  entity.max_health = info.max_health;
  entity.on_ground = false;
  /* Do not spawn inside a wall; rise until the AABB is free. */
  for (int guard = 0; guard < 8 && collides(entity.x, entity.y, entity.z,
                                            info.half_width, info.height);
       ++guard) {
    entity.y += 1.0;
  }
  entities_.push_back(entity);
  return entity.id;
}

uint64_t EntityManager::spawn_named(std::string_view name, double x, double y,
                                    double z) {
  return spawn(entity_type_from_name(name), x, y, z);
}

std::vector<EntityView> EntityManager::view() const {
  std::vector<EntityView> out;
  out.reserve(entities_.size());
  for (const Entity& entity : entities_) {
    EntityView view;
    view.id = entity.id;
    view.type = entity.type;
    view.x = entity.x;
    view.y = entity.y;
    view.z = entity.z;
    view.prev_x = entity.prev_x;
    view.prev_y = entity.prev_y;
    view.prev_z = entity.prev_z;
    view.yaw = entity.yaw;
    view.pitch = entity.pitch;
    view.prev_yaw = entity.prev_yaw;
    view.prev_pitch = entity.prev_pitch;
    view.health = entity.health;
    view.max_health = entity.max_health;
    view.hurt_time = entity.hurt_time;
    view.death_time = entity.death_time;
    view.limb_swing = entity.limb_swing;
    view.limb_amount = entity.limb_amount;
    view.head_yaw = entity.head_yaw;
    view.head_pitch = entity.head_pitch;
    view.on_fire = entity.fire_ticks > 0;
    view.dying = entity.death_time >= 0;
    out.push_back(view);
  }
  return out;
}

std::map<std::string, int> EntityManager::counts() const {
  std::map<std::string, int> out;
  for (const Entity& entity : entities_) {
    if (entity.death_time >= 0) continue;
    ++out[entity_type_name(entity.type)];
  }
  return out;
}

EntityHit EntityManager::raycast(const core::Player& player, double reach) const {
  EntityHit best;
  const double yaw = static_cast<double>(player.yaw) * kPi / 180.0;
  const double pitch = static_cast<double>(player.pitch) * kPi / 180.0;
  const double dx = -std::sin(yaw) * std::cos(pitch);
  const double dy = -std::sin(pitch);
  const double dz = std::cos(yaw) * std::cos(pitch);
  const double ox = player.x;
  const double oy = player.y + core::kPlayerEyeHeight;
  const double oz = player.z;
  for (const Entity& entity : entities_) {
    if (entity.death_time >= 0) continue;
    const EntityTypeInfo& info = entity_type_info(entity.type);
    const double min_x = entity.x - info.half_width;
    const double max_x = entity.x + info.half_width;
    const double min_y = entity.y;
    const double max_y = entity.y + info.height;
    const double min_z = entity.z - info.half_width;
    const double max_z = entity.z + info.half_width;
    double t0 = 0.0;
    double t1 = reach;
    auto slab = [&](double origin, double direction, double low, double high) {
      if (std::fabs(direction) < 1e-9) {
        return origin >= low && origin <= high;
      }
      double a = (low - origin) / direction;
      double b = (high - origin) / direction;
      if (a > b) std::swap(a, b);
      t0 = std::max(t0, a);
      t1 = std::min(t1, b);
      return t0 <= t1;
    };
    if (!slab(ox, dx, min_x, max_x)) continue;
    if (!slab(oy, dy, min_y, max_y)) continue;
    if (!slab(oz, dz, min_z, max_z)) continue;
    if (t0 < 0.0 || t0 > reach) continue;
    if (!best.valid || t0 < best.distance) {
      best.valid = true;
      best.id = entity.id;
      best.type = entity.type;
      best.distance = t0;
      best.x = ox + dx * t0;
      best.y = oy + dy * t0;
      best.z = oz + dz * t0;
    }
  }
  return best;
}

void EntityManager::apply_drops(EntityType type, double x, double y, double z,
                                EntityEvents& events) {
  struct Drop {
    const char* item;
    int min_count;
    int max_count;
    uint32_t chance_percent;
  };
  std::vector<Drop> table;
  switch (type) {
    case EntityType::zombie:
      table = {{"minecraft:rotten_flesh", 0, 2, 100},
               {"minecraft:iron_ingot", 1, 1, 3}};
      break;
    case EntityType::skeleton:
      table = {{"minecraft:bone", 0, 2, 100},
               {"minecraft:arrow", 0, 2, 100}};
      break;
    case EntityType::creeper:
      table = {{"minecraft:gunpowder", 0, 2, 100}};
      break;
    case EntityType::spider:
      table = {{"minecraft:string", 0, 2, 100}};
      break;
    case EntityType::cow:
      table = {{"minecraft:beef", 1, 3, 100},
               {"minecraft:leather", 0, 2, 100}};
      break;
    case EntityType::pig:
      table = {{"minecraft:porkchop", 1, 3, 100}};
      break;
    case EntityType::sheep:
      table = {{"minecraft:mutton", 1, 2, 100},
               {"minecraft:white_wool", 1, 1, 100}};
      break;
    case EntityType::chicken:
      table = {{"minecraft:chicken", 1, 1, 100},
               {"minecraft:feather", 0, 2, 100}};
      break;
  }
  if (items_ == nullptr) return;
  for (const Drop& drop : table) {
    if (drop.chance_percent < 100 && roll(100) >= drop.chance_percent) continue;
    const int count = drop.min_count == drop.max_count
                          ? drop.min_count
                          : drop.min_count +
                                static_cast<int>(roll(static_cast<uint32_t>(
                                    drop.max_count - drop.min_count + 1)));
    if (count <= 0) continue;
    ItemStack stack;
    stack.item_id = items_->find(drop.item);
    if (stack.item_id == 0) continue;
    stack.count = count;
    EntityDropEvent event;
    event.stack = stack;
    event.x = x;
    event.y = y;
    event.z = z;
    events.drops.push_back(event);
  }
}

void EntityManager::die(Entity& entity, EntityEvents& events) {
  if (entity.death_time >= 0) return;
  entity.health = 0.0f;
  entity.death_time = 20;
  EntitySoundEvent sound;
  sound.id = kSoundMobDeath;
  sound.x = entity.x;
  sound.y = entity.y + 0.8;
  sound.z = entity.z;
  sound.volume = 0.8f;
  sound.pitch = 1.0f;
  events.sounds.push_back(sound);
  ++events.kills;
  events.xp += entity_type_info(entity.type).xp;
  apply_drops(entity.type, entity.x, entity.y + 0.5, entity.z, events);
}

bool EntityManager::damage(uint64_t id, float amount, const core::Player& attacker,
                           EntityEvents& events) {
  if (amount <= 0.0f) return false;
  for (Entity& entity : entities_) {
    if (entity.id != id || entity.death_time >= 0) continue;
    entity.health -= amount;
    entity.hurt_time = 10;
    EntitySoundEvent sound;
    sound.id = kSoundMobHurt;
    sound.x = entity.x;
    sound.y = entity.y + 0.8;
    sound.z = entity.z;
    sound.volume = 0.7f;
    sound.pitch = 0.9f + static_cast<float>(roll(20)) / 100.0f;
    events.sounds.push_back(sound);
    double nx = entity.x - attacker.x;
    double nz = entity.z - attacker.z;
    const double length = std::sqrt(nx * nx + nz * nz);
    if (length > 1e-6) {
      nx /= length;
      nz /= length;
    } else {
      nx = 0.0;
      nz = 1.0;
    }
    entity.vx += nx * 0.35;
    entity.vz += nz * 0.35;
    if (entity.on_ground) entity.vy += 0.2;
    entity.path.clear();
    entity.flee_ticks = entity_type_info(entity.type).hostile ? 0 : 100;
    if (entity.health <= 0.0f) die(entity, events);
    return true;
  }
  return false;
}

void EntityManager::attack_player(Entity& entity, const core::Player& player,
                                  uint32_t difficulty, EntityEvents& events) {
  if (entity.attack_cooldown > 0) return;
  const EntityTypeInfo& info = entity_type_info(entity.type);
  const double dx = player.x - entity.x;
  const double dz = player.z - entity.z;
  const double dist = std::sqrt(dx * dx + dz * dz);
  if (dist > info.attack_range) return;
  MobDamageEvent damage;
  damage.amount = info.attack_damage * difficulty_scale(difficulty);
  damage.cause = DamageCause::mob;
  damage.from_x = entity.x;
  damage.from_z = entity.z;
  if (damage.amount > 0.0f) events.player_damage.push_back(damage);
  entity.attack_cooldown = kAttackCooldownTicks;
  EntitySoundEvent sound;
  sound.id = kSoundAttack;
  sound.x = entity.x;
  sound.y = entity.y + 1.0;
  sound.z = entity.z;
  sound.volume = 0.6f;
  events.sounds.push_back(sound);
}

void EntityManager::explode(Entity& entity, const core::Player& player,
                            EntityEvents& events) {
  entity.fuse = -2;
  EntitySoundEvent sound;
  sound.id = kSoundExplosion;
  sound.x = entity.x;
  sound.y = entity.y + 0.5;
  sound.z = entity.z;
  sound.volume = 1.0f;
  sound.pitch = 1.0f;
  events.sounds.push_back(sound);

  const double dx = player.x - entity.x;
  const double dy = (player.y + 0.9) - (entity.y + 0.5);
  const double dz = player.z - entity.z;
  const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < kExplosionRadius) {
    const float falloff = static_cast<float>(1.0 - dist / kExplosionRadius);
    MobDamageEvent damage;
    damage.amount = 6.0f * falloff;
    damage.cause = DamageCause::explosion;
    damage.from_x = entity.x;
    damage.from_z = entity.z;
    damage.explosion = true;
    events.player_damage.push_back(damage);
  }

  const int cx = static_cast<int>(std::floor(entity.x));
  const int cy = static_cast<int>(std::floor(entity.y));
  const int cz = static_cast<int>(std::floor(entity.z));
  if (world_ != nullptr) {
    for (int ox = -kExplosionBlockRadius; ox <= kExplosionBlockRadius; ++ox) {
      for (int oy = -kExplosionBlockRadius; oy <= kExplosionBlockRadius; ++oy) {
        for (int oz = -kExplosionBlockRadius; oz <= kExplosionBlockRadius; ++oz) {
          if (ox * ox + oy * oy + oz * oz >
              kExplosionBlockRadius * kExplosionBlockRadius) {
            continue;
          }
          const int bx = cx + ox;
          const int by = cy + oy;
          const int bz = cz + oz;
          world::BlockState state;
          if (!world_->block_state_at(bx, by, bz, state)) continue;
          if (is_air_name(state.name)) continue;
          if (unbreakable_by_explosion(state.name)) continue;
          events.block_edits.push_back(EntityBlockEdit{bx, by, bz});
        }
      }
    }
  }
  entity.health = 0.0f;
  entity.death_time = 0;
  ++events.kills;
}

bool EntityManager::build_path(Entity& entity, const core::Player& player) {
  entity.path.clear();
  entity.path_index = 0;
  if (world_ == nullptr) return false;

  const int sx = static_cast<int>(std::floor(entity.x));
  const int sy = static_cast<int>(std::floor(entity.y));
  const int sz = static_cast<int>(std::floor(entity.z));
  const int gx = static_cast<int>(std::floor(player.x));
  const int gy = static_cast<int>(std::floor(player.y));
  const int gz = static_cast<int>(std::floor(player.z));
  constexpr int kRange = 12;
  if (std::abs(gx - sx) > kRange || std::abs(gz - sz) > kRange ||
      std::abs(gy - sy) > 6) {
    return false;
  }

  auto key = [](int x, int y, int z) -> int64_t {
    return (static_cast<int64_t>(x & 0x3FFFF) << 36) |
           (static_cast<int64_t>(z & 0x3FFFF) << 8) |
           static_cast<int64_t>(y & 0xFF);
  };
  auto walkable = [&](int x, int y, int z) {
    if (solid_at(x, y, z)) return false;
    if (solid_at(x, y + 1, z)) return false;
    const bool support = solid_at(x, y - 1, z) || liquid_at(x, y, z) ||
                         liquid_at(x, y - 1, z);
    return support;
  };

  struct Node {
    int x, y, z;
    float g = 0.0f;
    float f = 0.0f;
    int parent = -1;
    bool closed = false;
  };
  std::vector<Node> nodes;
  std::unordered_map<int64_t, int> index;
  auto add_node = [&](int x, int y, int z, float g, float f, int parent) {
    const int64_t k = key(x, y, z);
    auto found = index.find(k);
    if (found != index.end()) {
      Node& existing = nodes[static_cast<size_t>(found->second)];
      if (g < existing.g) {
        existing.g = g;
        existing.f = f;
        existing.parent = parent;
      }
      return found->second;
    }
    const int id = static_cast<int>(nodes.size());
    nodes.push_back(Node{x, y, z, g, f, parent, false});
    index.emplace(k, id);
    return id;
  };

  auto heuristic = [&](int x, int y, int z) {
    const int dx = std::abs(x - gx);
    const int dy = std::abs(y - gy);
    const int dz = std::abs(z - gz);
    const int diagonal = std::min(dx, dz);
    return static_cast<float>(std::max(dx, dz) - diagonal) +
           static_cast<float>(diagonal) * 1.4142f +
           static_cast<float>(dy) * 1.5f;
  };

  add_node(sx, sy, sz, 0.0f, heuristic(sx, sy, sz), -1);
  int goal_index = -1;
  for (int expansions = 0; expansions < 800; ++expansions) {
    int current = -1;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (nodes[i].closed) continue;
      if (nodes[i].f < best) {
        best = nodes[i].f;
        current = static_cast<int>(i);
      }
    }
    if (current < 0) break;
    Node& node = nodes[static_cast<size_t>(current)];
    if (node.x == gx && node.z == gz &&
        std::abs(node.y - gy) <= 1) {
      goal_index = current;
      break;
    }
    node.closed = true;
    static constexpr int kDirections[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (const auto& direction : kDirections) {
      const bool diagonal = direction[0] != 0 && direction[1] != 0;
      const int nx = node.x + direction[0];
      const int nz = node.z + direction[1];
      for (int dy : {0, 1, -1}) {
        const int ny = node.y + dy;
        if (std::abs(ny - gy) > 6) continue;
        if (!walkable(nx, ny, nz)) continue;
        if (diagonal &&
            (!walkable(node.x + direction[0], node.y, node.z) ||
             !walkable(node.x, node.y, node.z + direction[1]))) {
          continue;
        }
        const float step = diagonal ? 1.4142f : 1.0f;
        const float move_cost = step + (dy != 0 ? 1.5f : 0.0f);
        const float g = node.g + move_cost;
        const int id = add_node(nx, ny, nz, g, g + heuristic(nx, ny, nz), current);
        (void)id;
      }
    }
  }

  if (goal_index < 0) return false;
  std::vector<std::array<double, 3>> reversed;
  for (int at = goal_index; at >= 0; at = nodes[static_cast<size_t>(at)].parent) {
    const Node& node = nodes[static_cast<size_t>(at)];
    reversed.push_back({node.x + 0.5, static_cast<double>(node.y),
                        node.z + 0.5});
    if (node.parent < 0) break;
  }
  reversed.pop_back();  /* start cell */
  entity.path.assign(reversed.rbegin(), reversed.rend());
  return !entity.path.empty();
}

void EntityManager::follow_path(Entity& entity, bool can_jump) {
  const EntityTypeInfo& info = entity_type_info(entity.type);
  if (entity.path.empty() || entity.path_index >= entity.path.size()) return;
  const std::array<double, 3>& target = entity.path[entity.path_index];
  const double dx = target[0] - entity.x;
  const double dz = target[2] - entity.z;
  const double dist = std::sqrt(dx * dx + dz * dz);
  if (dist < 0.55) {
    ++entity.path_index;
    if (entity.path_index >= entity.path.size()) entity.path.clear();
    return;
  }
  entity.vx = dx / dist * info.speed;
  entity.vz = dz / dist * info.speed;
  if (can_jump && target[1] > entity.y + 0.6 && entity.on_ground) {
    entity.vy = 0.42;
  }
}

void EntityManager::tick_ai(Entity& entity, const core::Player& player,
                            const EnvironmentState& environment,
                            uint32_t difficulty, EntityEvents& events) {
  const EntityTypeInfo& info = entity_type_info(entity.type);
  if (entity.attack_cooldown > 0) --entity.attack_cooldown;
  if (entity.ai_timer > 0) --entity.ai_timer;

  const double dx = player.x - entity.x;
  const double dz = player.z - entity.z;
  const double horizontal = std::sqrt(dx * dx + dz * dz);
  const bool day_and_exposed =
      environment.daylight && !entity.in_water &&
      exposed_to_sky(static_cast<int>(std::floor(entity.x)),
                     static_cast<int>(std::floor(entity.y)),
                     static_cast<int>(std::floor(entity.z)));

  /* Zombies and skeletons burn in daylight (M3-F5). */
  if (info.hostile && (entity.type == EntityType::zombie ||
                       entity.type == EntityType::skeleton) &&
      day_and_exposed) {
    entity.fire_ticks = std::max(entity.fire_ticks, 160);
  }
  if (entity.in_water) entity.fire_ticks = 0;
  if (entity.fire_ticks > 0) {
    --entity.fire_ticks;
    if (++entity.fire_damage_timer >= 20) {
      entity.fire_damage_timer = 0;
      entity.health -= 1.0f;
      if (entity.health <= 0.0f) die(entity, events);
    }
  }

  if (entity.flee_ticks > 0) --entity.flee_ticks;

  bool moving = false;
  if (info.hostile && difficulty > 0 && horizontal <= info.follow_range) {
    entity.head_yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
    if (entity.type == EntityType::creeper) {
      if (entity.fuse == -1 && horizontal <= kCreeperTrigger) {
        entity.fuse = 0;
        EntitySoundEvent sound;
        sound.id = kSoundCreeperPrime;
        sound.x = entity.x;
        sound.y = entity.y + 1.0;
        sound.z = entity.z;
        sound.volume = 1.0f;
        events.sounds.push_back(sound);
      }
      if (entity.fuse >= 0) {
        ++entity.fuse;
        if (entity.fuse >= kCreeperFuseTicks) {
          explode(entity, player, events);
          return;
        }
      }
    }
    if (entity.fuse < 0 && horizontal > info.attack_range) {
      if (entity.ai_timer <= 0 || entity.path.empty()) {
        if (!build_path(entity, player)) entity.path.clear();
        entity.ai_timer = 20;
      }
      if (!entity.path.empty()) {
        follow_path(entity, true);
      } else {
        entity.vx = dx / std::max(horizontal, 0.001) * info.speed;
        entity.vz = dz / std::max(horizontal, 0.001) * info.speed;
      }
      moving = true;
    } else if (entity.fuse < 0) {
      entity.vx = 0.0;
      entity.vz = 0.0;
      attack_player(entity, player, difficulty, events);
    }
  } else if (!info.hostile || difficulty == 0) {
    if (entity.flee_ticks > 0 && horizontal < 12.0) {
      const double length = std::max(horizontal, 0.001);
      entity.vx = -dx / length * info.speed * 1.4;
      entity.vz = -dz / length * info.speed * 1.4;
      entity.head_yaw = static_cast<float>(std::atan2(dx, -dz) * 180.0 / kPi);
      moving = true;
    } else {
      if (entity.wander_timer <= 0) {
        const double angle = roll01() * 2.0 * kPi;
        const double radius = 2.0 + roll01() * 6.0;
        entity.wander_x = entity.x + std::sin(angle) * radius;
        entity.wander_z = entity.z + std::cos(angle) * radius;
        entity.wander_timer = 60 + static_cast<int>(roll(100));
      }
      --entity.wander_timer;
      const double wx = entity.wander_x - entity.x;
      const double wz = entity.wander_z - entity.z;
      const double length = std::sqrt(wx * wx + wz * wz);
      if (length > 0.6) {
        entity.vx = wx / length * info.speed * 0.7;
        entity.vz = wz / length * info.speed * 0.7;
        entity.head_yaw = static_cast<float>(std::atan2(-wx, wz) * 180.0 / kPi);
        moving = true;
      } else {
        entity.vx = 0.0;
        entity.vz = 0.0;
      }
      if (horizontal < 8.0) {
        entity.head_yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
        entity.head_pitch = static_cast<float>(
            -std::atan2((player.y + 1.6) - (entity.y + info.height * 0.8),
                        std::max(horizontal, 0.001)) *
            180.0 / kPi);
      }
    }
  } else {
    entity.vx = 0.0;
    entity.vz = 0.0;
  }
  (void)moving;

  if (horizontal > 1e-4) {
    const float target_yaw =
        static_cast<float>(std::atan2(-entity.vx, entity.vz) * 180.0 / kPi);
    if (std::fabs(entity.vx) + std::fabs(entity.vz) > 1e-5) {
      entity.yaw = target_yaw;
    }
  }
}

void EntityManager::natural_spawn(const core::Player& player,
                                  const EnvironmentState& environment,
                                  uint32_t difficulty, EntityEvents& events) {
  if (difficulty == 0 || !natural_spawning_ ||
      entities_.size() >= kEntityCap) {
    return;
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    const double angle = roll01() * 2.0 * kPi;
    const double radius = 12.0 + roll01() * 20.0;
    const int x = static_cast<int>(std::floor(player.x + std::sin(angle) * radius));
    const int z = static_cast<int>(std::floor(player.z + std::cos(angle) * radius));
    if (!world_->is_loaded(x, z)) continue;
    const int top = static_cast<int>(std::floor(player.y)) + 16;
    int surface = std::numeric_limits<int>::min();
    std::string surface_name;
    for (int y = std::min(top, 320); y > -64; --y) {
      world::BlockState state;
      if (!world_->block_state_at(x, y, z, state)) continue;
      const data::BlockDef& def = world_->blocks().def(world_->block_id_at(x, y, z));
      if (!def.collision) continue;
      surface = y;
      surface_name = state.name;
      break;
    }
    if (surface == std::numeric_limits<int>::min()) continue;
    world::BlockState above;
    if (!world_->block_state_at(x, surface + 1, z, above) ||
        !world_->block_state_at(x, surface + 2, z, above)) {
      continue;
    }
    const std::string_view bare = bare_name(surface_name);
    const bool nether = environment.dimension == 1;
    const bool soil = bare == "grass_block" || bare == "dirt" ||
                      bare == "sand" || bare == "podzol" ||
                      (nether && (bare == "netherrack" || bare == "blackstone" ||
                                  bare == "soul_sand"));
    if (!soil) continue;
    const bool hostile_allowed =
        nether || !environment.daylight || surface < 50;
    /* The M3 light engine is not available yet (C4); passive spawning accepts
     * any soil top at day instead of vanilla's grass-only rule. */
    const bool passive_allowed =
        !nether && environment.daylight &&
        (bare == "grass_block" || bare == "dirt" || bare == "sand" ||
         bare == "podzol");
    const bool spawn_hostile = hostile_allowed && roll(100) < 70;
    if (!spawn_hostile && !passive_allowed) continue;
    EntityType type;
    if (spawn_hostile) {
      static constexpr EntityType kHostiles[4] = {
          EntityType::zombie, EntityType::skeleton, EntityType::creeper,
          EntityType::spider};
      type = kHostiles[roll(4)];
    } else {
      static constexpr EntityType kPassive[4] = {
          EntityType::cow, EntityType::pig, EntityType::sheep,
          EntityType::chicken};
      type = kPassive[roll(4)];
    }
    spawn(type, x + 0.5, surface + 1.0, z + 0.5);
    ++events.spawned;
    if (entities_.size() >= kEntityCap) break;
  }
}

void EntityManager::tick(const core::Player& player,
                         const EnvironmentState& environment,
                         uint32_t difficulty, EntityEvents& events) {
  ++tick_;
  for (Entity& entity : entities_) {
    entity.prev_x = entity.x;
    entity.prev_y = entity.y;
    entity.prev_z = entity.z;
    entity.prev_yaw = entity.yaw;
    entity.prev_pitch = entity.pitch;
    if (entity.hurt_time > 0) --entity.hurt_time;
  }

  std::vector<bool> remove(entities_.size(), false);
  for (size_t i = 0; i < entities_.size(); ++i) {
    Entity& entity = entities_[i];
    ++entity.age;
    if (entity.death_time >= 0) {
      --entity.death_time;
      if (entity.death_time <= 0) remove[i] = true;
      continue;
    }
    tick_ai(entity, player, environment, difficulty, events);
    if (entity.death_time >= 0) continue;  /* exploded this tick */
    physics(entity);

    const double dx = entity.x - player.x;
    const double dz = entity.z - player.z;
    const double dist = std::sqrt(dx * dx + dz * dz);
    if (entity_type_info(entity.type).hostile && dist > kDespawnDistance &&
        entity.age > 200) {
      remove[i] = true;
      continue;
    }
    /* Passive mobs never teleport away; they persist like vanilla. */
    if (entity.fire_ticks > 0 && entity.in_water) entity.fire_ticks = 0;
    if (entity.explode_pending) remove[i] = true;
  }

  size_t write = 0;
  for (size_t i = 0; i < entities_.size(); ++i) {
    if (remove[i]) continue;
    if (write != i) entities_[write] = std::move(entities_[i]);
    ++write;
  }
  entities_.resize(write);

  if (++spawn_timer_ >= 20) {
    spawn_timer_ = 0;
    natural_spawn(player, environment, difficulty, events);
  }
}

std::string entity_model_defs_json() {
  nlohmann::json root;
  root["format"] = 1;
  nlohmann::json models = nlohmann::json::array();

  auto part = [](const char* name, std::array<double, 3> pivot,
                 std::array<double, 3> size, uint32_t color) {
    nlohmann::json p;
    p["name"] = name;
    p["pivot"] = {pivot[0], pivot[1], pivot[2]};
    p["size"] = {size[0], size[1], size[2]};
    p["color"] = color;
    return p;
  };
  auto humanoid = [&](int id, const char* name, uint32_t head, uint32_t body,
                      uint32_t limbs) {
    nlohmann::json model;
    model["id"] = id;
    model["name"] = name;
    model["parts"] = nlohmann::json::array(
        {part("head", {0.0, 1.55, 0.0}, {0.5, 0.5, 0.5}, head),
         part("body", {0.0, 1.15, 0.0}, {0.5, 0.6, 0.25}, body),
         part("right_arm", {0.35, 1.4, 0.0}, {0.2, 0.6, 0.2}, limbs),
         part("left_arm", {-0.35, 1.4, 0.0}, {0.2, 0.6, 0.2}, limbs),
         part("right_leg", {0.15, 0.6, 0.0}, {0.22, 0.6, 0.22}, limbs),
         part("left_leg", {-0.15, 0.6, 0.0}, {0.22, 0.6, 0.22}, limbs)});
    models.push_back(model);
  };
  auto quadruped = [&](int id, const char* name, uint32_t body_color,
                       uint32_t head_color, uint32_t leg_color) {
    nlohmann::json model;
    model["id"] = id;
    model["name"] = name;
    model["parts"] = nlohmann::json::array(
        {part("head", {0.0, 0.95, 0.55}, {0.45, 0.45, 0.45}, head_color),
         part("body", {0.0, 0.75, 0.0}, {0.55, 0.45, 0.9}, body_color),
         part("front_right_leg", {0.22, 0.35, 0.3}, {0.18, 0.5, 0.18}, leg_color),
         part("front_left_leg", {-0.22, 0.35, 0.3}, {0.18, 0.5, 0.18}, leg_color),
         part("back_right_leg", {0.22, 0.35, -0.3}, {0.18, 0.5, 0.18}, leg_color),
         part("back_left_leg", {-0.22, 0.35, -0.3}, {0.18, 0.5, 0.18}, leg_color)});
    models.push_back(model);
  };

  humanoid(1, "reim:zombie", 0x5E7D46, 0x3E5A34, 0x4C7A34);
  humanoid(2, "reim:skeleton", 0xD8D8D0, 0xB8B8B0, 0xC8C8C0);
  {
    nlohmann::json creeper;
    creeper["id"] = 3;
    creeper["name"] = "reim:creeper";
    creeper["parts"] = nlohmann::json::array(
        {part("head", {0.0, 1.45, 0.0}, {0.45, 0.45, 0.45}, 0x6CBF5A),
         part("body", {0.0, 0.95, 0.0}, {0.5, 0.7, 0.3}, 0x4FA34F),
         part("front_right_leg", {0.15, 0.3, 0.15}, {0.2, 0.4, 0.2}, 0x3F8A3F),
         part("front_left_leg", {-0.15, 0.3, 0.15}, {0.2, 0.4, 0.2}, 0x3F8A3F),
         part("back_right_leg", {0.15, 0.3, -0.15}, {0.2, 0.4, 0.2}, 0x3F8A3F),
         part("back_left_leg", {-0.15, 0.3, -0.15}, {0.2, 0.4, 0.2}, 0x3F8A3F)});
    models.push_back(creeper);
  }
  {
    nlohmann::json spider;
    spider["id"] = 4;
    spider["name"] = "reim:spider";
    nlohmann::json parts = nlohmann::json::array();
    parts.push_back(part("head", {0.0, 0.6, 0.7}, {0.4, 0.35, 0.4}, 0x3A2A22));
    parts.push_back(part("body", {0.0, 0.55, 0.0}, {0.7, 0.5, 0.9}, 0x2E221C));
    for (int i = 0; i < 8; ++i) {
      const double side = (i % 2 == 0) ? 1.0 : -1.0;
      const double z = 0.35 - static_cast<double>(i / 2) * 0.25;
      parts.push_back(part(i % 2 == 0 ? "right_leg" : "left_leg",
                           {side * 0.45, 0.45, z}, {0.7, 0.12, 0.12},
                           0x2A1E18));
    }
    spider["parts"] = std::move(parts);
    models.push_back(spider);
  }
  quadruped(5, "reim:cow", 0xE8E8E8, 0x4A3524, 0x4A3524);
  quadruped(6, "reim:pig", 0xE89A9A, 0xE8A0A0, 0xD88A8A);
  quadruped(7, "reim:sheep", 0xF0F0EE, 0xE0D8CE, 0xC8BEB0);
  {
    nlohmann::json chicken;
    chicken["id"] = 8;
    chicken["name"] = "reim:chicken";
    chicken["parts"] = nlohmann::json::array(
        {part("head", {0.0, 0.6, 0.25}, {0.3, 0.3, 0.3}, 0xF5F0E6),
         part("body", {0.0, 0.35, 0.0}, {0.4, 0.35, 0.45}, 0xEDE8DC),
         part("right_wing", {0.22, 0.38, 0.0}, {0.08, 0.25, 0.35}, 0xD8D0C0),
         part("left_wing", {-0.22, 0.38, 0.0}, {0.08, 0.25, 0.35}, 0xD8D0C0),
         part("right_leg", {0.1, 0.15, 0.0}, {0.08, 0.3, 0.08}, 0xE8B040),
         part("left_leg", {-0.1, 0.15, 0.0}, {0.08, 0.3, 0.08}, 0xE8B040)});
    models.push_back(chicken);
  }

  root["models"] = std::move(models);
  root["source"] = "native (26.3 entity models are Java code; M3_PLAN 9.2)";
  return root.dump();
}

} // namespace reim::game
