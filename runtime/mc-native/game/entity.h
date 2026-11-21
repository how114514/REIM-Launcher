#pragma once

#include "core/player.h"
#include "game/item.h"
#include "game/survival.h"
#include "game/world_access.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reim::game {

/*
 * Runtime entity framework and the first eight mobs (M3-F4..F8).
 *
 * Entities live on the simulation thread and are owned by Gameplay. Physics,
 * AI, spawning, combat and drops are decided here; the Host only draws the
 * entity render list published through the Contract. Model/animation data is
 * native because 26.3 entity models are Java code (M3_PLAN 8.7/9.2).
 *
 * M3 scope: 8 mobs, steering + A* subset, melee combat, creeper explosion.
 * Ranged skeletons/arrows, breeding, taming and bosses are out of range.
 */
enum class EntityType : uint32_t {
  zombie = 1,
  skeleton = 2,
  creeper = 3,
  spider = 4,
  cow = 5,
  pig = 6,
  sheep = 7,
  chicken = 8
};

constexpr uint32_t kEntityTypeCount = 8;

const char* entity_type_name(EntityType type);
const char* entity_type_display(EntityType type);
EntityType entity_type_from_name(std::string_view name);

struct EntityTypeInfo {
  const char* name;
  const char* display;
  bool hostile;
  float half_width;
  float height;
  float speed;
  float max_health;
  float attack_damage;
  int xp;
  float follow_range;
  float attack_range;
};

const EntityTypeInfo& entity_type_info(EntityType type);

/* Sound ids exposed through SOUND_INDEX_JSON (M3-F8/I3). */
constexpr uint32_t kSoundMobHurt = 10;
constexpr uint32_t kSoundMobDeath = 11;
constexpr uint32_t kSoundExplosion = 12;
constexpr uint32_t kSoundPlayerHurt = 13;
constexpr uint32_t kSoundAttack = 14;
constexpr uint32_t kSoundCreeperPrime = 15;
constexpr uint32_t kSoundPlayerDeath = 16;
constexpr uint32_t kSoundBlockBreak = 20;
constexpr uint32_t kSoundBlockPlace = 21;
constexpr uint32_t kSoundFootstep = 22;
constexpr uint32_t kSoundPickup = 23;
constexpr uint32_t kSoundBlockHit = 24;
constexpr uint32_t kSoundWeatherRain = 25;
constexpr uint32_t kSoundThunder = 26;
constexpr uint32_t kSoundAmbientCave = 27;
constexpr uint32_t kSoundXpPickup = 28;

struct MobDamageEvent {
  float amount = 1.0f;
  DamageCause cause = DamageCause::mob;
  double from_x = 0.0;
  double from_z = 0.0;
  bool explosion = false;
};

struct EntitySoundEvent {
  uint32_t id = 0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  float volume = 1.0f;
  float pitch = 1.0f;
};

struct EntityDropEvent {
  ItemStack stack;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct EntityBlockEdit {
  int x = 0;
  int y = 0;
  int z = 0;
};

struct EntityEvents {
  std::vector<MobDamageEvent> player_damage;
  std::vector<EntitySoundEvent> sounds;
  std::vector<EntityDropEvent> drops;
  std::vector<EntityBlockEdit> block_edits;
  int kills = 0;
  int xp = 0;
  int spawned = 0;
};

struct EntityHit {
  bool valid = false;
  uint64_t id = 0;
  EntityType type = EntityType::zombie;
  double distance = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/* Immutable per-tick copy consumed by the render/status builders. */
struct EntityView {
  uint64_t id = 0;
  EntityType type = EntityType::zombie;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double prev_x = 0.0;
  double prev_y = 0.0;
  double prev_z = 0.0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float prev_yaw = 0.0f;
  float prev_pitch = 0.0f;
  float health = 0.0f;
  float max_health = 0.0f;
  int hurt_time = 0;
  int death_time = -1;
  float limb_swing = 0.0f;
  float limb_amount = 0.0f;
  float head_yaw = 0.0f;
  float head_pitch = 0.0f;
  bool on_fire = false;
  bool dying = false;
};

struct Entity {
  uint64_t id = 0;
  EntityType type = EntityType::zombie;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double prev_x = 0.0;
  double prev_y = 0.0;
  double prev_z = 0.0;
  double vx = 0.0;
  double vy = 0.0;
  double vz = 0.0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float prev_yaw = 0.0f;
  float prev_pitch = 0.0f;
  float head_yaw = 0.0f;
  float head_pitch = 0.0f;
  float health = 20.0f;
  float max_health = 20.0f;
  int hurt_time = 0;
  int death_time = -1;   /* ticks since death; -1 = alive */
  int age = 0;
  int attack_cooldown = 0;
  int ai_timer = 0;
  int wander_timer = 0;
  int flee_ticks = 0;
  int fire_ticks = 0;
  int fire_damage_timer = 0;
  int fuse = -1;         /* creeper: -1 idle, >=0 priming */
  bool explode_pending = false;
  bool on_ground = false;
  bool in_water = false;
  double wander_x = 0.0;
  double wander_z = 0.0;
  float limb_swing = 0.0f;
  float limb_amount = 0.0f;
  std::vector<std::array<double, 3>> path;
  size_t path_index = 0;
};

class EntityManager {
 public:
  void initialize(WorldAccess* world, const ItemTable* items, uint64_t seed);
  void set_natural_spawning(bool enabled) { natural_spawning_ = enabled; }
  bool natural_spawning() const { return natural_spawning_; }

  void clear();
  uint64_t spawn(EntityType type, double x, double y, double z);
  uint64_t spawn_named(std::string_view name, double x, double y, double z);

  void tick(const core::Player& player, const EnvironmentState& environment,
            uint32_t difficulty, EntityEvents& events);

  /* Ray from the player's eye along the look direction; nearest entity AABB
   * hit up to `reach`. */
  EntityHit raycast(const core::Player& player, double reach) const;

  /* Applies player melee damage; fills drops/xp/sounds on death. */
  bool damage(uint64_t id, float amount, const core::Player& attacker,
              EntityEvents& events);

  const std::vector<Entity>& entities() const { return entities_; }
  std::vector<EntityView> view() const;
  std::map<std::string, int> counts() const;
  size_t size() const { return entities_.size(); }

  /* Deterministic RNG state used by spawning/drops (test/diagnostic aid). */
  uint64_t rng_state() const { return rng_; }

 private:
  struct PathNode {
    int x = 0;
    int y = 0;
    int z = 0;
  };

  bool collides(double x, double y, double z, float half, float height) const;
  bool liquid_at(int x, int y, int z) const;
  bool solid_at(int x, int y, int z) const;
  void move_entity(Entity& entity, double dx, double dy, double dz);
  void physics(Entity& entity);
  void tick_ai(Entity& entity, const core::Player& player,
               const EnvironmentState& environment, uint32_t difficulty,
               EntityEvents& events);
  void attack_player(Entity& entity, const core::Player& player,
                     uint32_t difficulty, EntityEvents& events);
  void explode(Entity& entity, const core::Player& player, EntityEvents& events);
  void die(Entity& entity, EntityEvents& events);
  void apply_drops(EntityType type, double x, double y, double z,
                   EntityEvents& events);
  void natural_spawn(const core::Player& player,
                     const EnvironmentState& environment, uint32_t difficulty,
                     EntityEvents& events);
  bool exposed_to_sky(int x, int y, int z) const;
  bool build_path(Entity& entity, const core::Player& player);
  void follow_path(Entity& entity, bool can_jump);
  uint32_t roll(uint32_t bound);
  double roll01();

  WorldAccess* world_ = nullptr;
  const ItemTable* items_ = nullptr;
  std::vector<Entity> entities_;
  uint64_t next_id_ = 1;
  uint64_t rng_ = 0x9E3779B97F4A7C15ull;
  uint64_t seed_ = 0;
  uint64_t tick_ = 0;
  int spawn_timer_ = 0;
  bool natural_spawning_ = true;
};

/* MODEL_DEFS_JSON models for the eight mobs (model ids 1..8). */
std::string entity_model_defs_json();

} // namespace reim::game
