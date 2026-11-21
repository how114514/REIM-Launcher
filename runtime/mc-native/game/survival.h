#pragma once

#include <cstdint>

namespace reim::game {

/*
 * Survival state and rules (M3-F1/F2/F3).
 *
 * The state is owned by the Gameplay authority and ticked on the 20 TPS
 * simulation thread. The module is deliberately independent of the world and
 * of the player movement code: the caller supplies a SurvivalContext snapshot
 * each tick, so the rules stay unit-testable without an Anvil world.
 *
 * M3 scope (docs/M3_PLAN.md 1.3/8.6): health/hunger/air/experience,
 * fall/drown/lava/fire/suffocation/mob/void/starve damage, death/respawn.
 * Armour reduction, status effects, shields and bosses are out of range.
 */
enum class DamageCause : uint8_t {
  none = 0,
  fall = 1,
  drown = 2,
  lava = 3,
  fire = 4,
  suffocation = 5,
  mob = 6,
  void_ = 7,
  starve = 8,
  explosion = 9,
  player = 10,
  unknown = 11
};

const char* damage_cause_name(DamageCause cause);

struct SurvivalState {
  float health = 20.0f;
  float max_health = 20.0f;
  float food = 20.0f;         /* 0..20 */
  float saturation = 5.0f;    /* 0..food */
  float exhaustion = 0.0f;
  float air = 300.0f;         /* ticks */
  float max_air = 300.0f;

  uint32_t xp_level = 0;
  uint32_t xp_total = 0;      /* lifetime points, never decreases on respawn */
  float xp_in_level = 0.0f;   /* points inside the current level */

  bool dead = false;
  uint32_t deaths = 0;
  uint32_t hurt_time = 0;     /* ticks of red flash */
  uint32_t invuln = 0;        /* ticks of damage immunity */
  uint32_t fire_ticks = 0;
  DamageCause last_damage = DamageCause::none;

  /* Counters for status/scenes. */
  float damage_taken = 0.0f;
  uint32_t starvation_ticks = 0;

  /* Set by damage()/tick() for the caller; cleared by survival_begin_tick. */
  bool hurt_this_tick = false;
  bool died_this_tick = false;

  /* Internal roll-back state for fall damage. */
  double fall_distance = 0.0;
  bool was_on_ground = true;
  uint32_t regen_timer = 0;
  uint32_t starve_timer = 0;
  uint32_t drown_timer = 0;
  uint32_t lava_timer = 0;
  uint32_t fire_timer = 0;
  uint32_t suffocate_timer = 0;
  uint32_t void_timer = 0;
};

/* Per-tick facts the caller samples from the player/world. */
struct SurvivalContext {
  bool in_water = false;
  bool on_ground = false;
  double vy = 0.0;             /* blocks per tick; negative = falling */
  double y = 0.0;
  bool moving = false;
  bool sprinting = false;
  bool lava_at_feet = false;
  bool fire_at_feet = false;
  bool suffocating = false;
  uint32_t difficulty = 2;     /* ReimDifficulty */
};

void survival_reset(SurvivalState& state, bool fresh_spawn);
/* Respawn after death: restores health/food/air, keeps lifetime XP. */
void survival_respawn(SurvivalState& state);

void survival_begin_tick(SurvivalState& state);

/* Applies damage. Returns false when it was ignored (dead, invulnerability).
 * Environmental damage sources that the caller ticks must be rate-limited by
 * the caller; this function applies one damage instance. */
bool survival_damage(SurvivalState& state, float amount, DamageCause cause);

void survival_add_exhaustion(SurvivalState& state, float amount);
void survival_add_xp(SurvivalState& state, uint32_t points);
uint32_t survival_xp_threshold(uint32_t level);
float survival_xp_progress(const SurvivalState& state);

/* Fall damage for a fall distance in blocks (0 at <= 3 blocks). */
float survival_fall_damage(double distance);

/* Ticks hunger, regeneration, air, environmental damage and death. */
void survival_tick(SurvivalState& state, const SurvivalContext& context);

} // namespace reim::game
