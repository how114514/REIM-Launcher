#include "game/survival.h"

#include <algorithm>
#include <cmath>

namespace reim::game {
namespace {

constexpr float kMaxHealth = 20.0f;
constexpr float kMaxAir = 300.0f;
constexpr float kExhaustionPerSprintTick = 0.1f;
constexpr float kExhaustionPerWalkTick = 0.01f;

/* Easy/Normal/Hard multipliers for player damage (M3-F2). Peaceful mobs do
 * not attack at all; the spawner is disabled there as well. */
float difficulty_scale(uint32_t difficulty) {
  switch (difficulty) {
    case 0: return 0.0f;
    case 1: return 0.5f;
    case 3: return 1.5f;
    default: return 1.0f;
  }
}

void apply_hunger(SurvivalState& state) {
  while (state.exhaustion >= 4.0f) {
    state.exhaustion -= 4.0f;
    if (state.saturation > 0.0f) {
      state.saturation = std::max(0.0f, state.saturation - 1.0f);
    } else {
      state.food = std::max(0.0f, state.food - 1.0f);
    }
  }
  state.saturation = std::min(state.saturation, state.food);
}

} // namespace

const char* damage_cause_name(DamageCause cause) {
  switch (cause) {
    case DamageCause::fall: return "fall";
    case DamageCause::drown: return "drown";
    case DamageCause::lava: return "lava";
    case DamageCause::fire: return "fire";
    case DamageCause::suffocation: return "suffocation";
    case DamageCause::mob: return "mob";
    case DamageCause::void_: return "void";
    case DamageCause::starve: return "starve";
    case DamageCause::explosion: return "explosion";
    case DamageCause::player: return "player";
    case DamageCause::unknown: return "unknown";
    case DamageCause::none:
    default: return "none";
  }
}

void survival_reset(SurvivalState& state, bool fresh_spawn) {
  const uint32_t xp_level = fresh_spawn ? 0u : state.xp_level;
  const uint32_t xp_total = fresh_spawn ? 0u : state.xp_total;
  const float xp_in_level = fresh_spawn ? 0.0f : state.xp_in_level;
  const uint32_t deaths = state.deaths;
  state = SurvivalState{};
  state.xp_level = xp_level;
  state.xp_total = xp_total;
  state.xp_in_level = xp_in_level;
  state.deaths = deaths;
  state.health = state.max_health;
  state.food = 20.0f;
  state.saturation = 5.0f;
  state.air = state.max_air;
  state.was_on_ground = true;
}

void survival_respawn(SurvivalState& state) {
  const uint32_t xp_level = state.xp_level;
  const uint32_t xp_total = state.xp_total;
  const float xp_in_level = state.xp_in_level;
  const uint32_t deaths = state.deaths;
  const float damage_taken = state.damage_taken;
  state = SurvivalState{};
  state.xp_level = xp_level;
  state.xp_total = xp_total;
  state.xp_in_level = xp_in_level;
  state.deaths = deaths;
  state.damage_taken = damage_taken;
  state.health = state.max_health;
  state.food = 20.0f;
  state.saturation = 5.0f;
  state.air = state.max_air;
  state.invuln = 20;
  state.was_on_ground = true;
}

void survival_begin_tick(SurvivalState& state) {
  state.hurt_this_tick = false;
  state.died_this_tick = false;
}

uint32_t survival_xp_threshold(uint32_t level) {
  return 7u + level * 3u;
}

void survival_add_xp(SurvivalState& state, uint32_t points) {
  if (points == 0) return;
  state.xp_total += points;
  state.xp_in_level += static_cast<float>(points);
  /* Vanilla-like approximate curve; the exact 26.3 formula is not part of the
   * M3 scope (docs/M3_PLAN.md 1.3). */
  for (int guard = 0; guard < 10000; ++guard) {
    const uint32_t threshold = survival_xp_threshold(state.xp_level);
    if (state.xp_in_level < static_cast<float>(threshold)) break;
    state.xp_in_level -= static_cast<float>(threshold);
    ++state.xp_level;
  }
}

float survival_xp_progress(const SurvivalState& state) {
  const uint32_t threshold = survival_xp_threshold(state.xp_level);
  if (threshold == 0) return 0.0f;
  return std::clamp(state.xp_in_level / static_cast<float>(threshold), 0.0f, 1.0f);
}

float survival_fall_damage(double distance) {
  if (distance <= 3.0) return 0.0f;
  return static_cast<float>(std::floor(distance - 3.0));
}

bool survival_damage(SurvivalState& state, float amount, DamageCause cause) {
  if (state.dead || amount <= 0.0f) return false;
  if (state.invuln > 0 && cause != DamageCause::void_) return false;
  state.health -= amount;
  state.damage_taken += amount;
  state.hurt_time = 10;
  state.invuln = 10;
  state.last_damage = cause;
  state.hurt_this_tick = true;
  if (state.health <= 0.0f) {
    state.health = 0.0f;
    state.dead = true;
    state.died_this_tick = true;
    ++state.deaths;
  }
  return true;
}

void survival_add_exhaustion(SurvivalState& state, float amount) {
  if (state.dead || amount <= 0.0f) return;
  state.exhaustion = std::min(state.exhaustion + amount, 40.0f);
}

void survival_tick(SurvivalState& state, const SurvivalContext& ctx) {
  if (state.dead) {
    if (state.invuln > 0) --state.invuln;
    if (state.hurt_time > 0) --state.hurt_time;
    return;
  }
  if (state.invuln > 0) --state.invuln;
  if (state.hurt_time > 0) --state.hurt_time;

  /* Exhaustion from movement. */
  if (ctx.moving) {
    survival_add_exhaustion(state, ctx.sprinting ? kExhaustionPerSprintTick
                                                 : kExhaustionPerWalkTick);
  }
  apply_hunger(state);

  /* Natural regeneration (food >= 18). */
  if (state.food >= 18.0f && state.health < state.max_health) {
    if (++state.regen_timer >= 80) {
      state.regen_timer = 0;
      state.health = std::min(state.max_health, state.health + 1.0f);
      survival_add_exhaustion(state, 6.0f);
    }
  } else {
    state.regen_timer = 0;
  }

  /* Starvation. */
  if (state.food <= 0.0f) {
    ++state.starvation_ticks;
    const uint32_t period = ctx.difficulty >= 3 ? 40u
                           : ctx.difficulty == 2 ? 80u
                           : ctx.difficulty == 1 ? 160u
                                                 : 0u;
    if (period != 0u) {
      if (++state.starve_timer >= period) {
        state.starve_timer = 0;
        survival_damage(state, 1.0f, DamageCause::starve);
      }
    } else {
      state.starve_timer = 0;
    }
  } else {
    state.starve_timer = 0;
  }

  /* Air / drowning. */
  if (ctx.in_water) {
    state.air = std::max(0.0f, state.air - 1.0f);
    if (state.air <= 0.0f) {
      if (++state.drown_timer >= 20) {
        state.drown_timer = 0;
        survival_damage(state, 2.0f, DamageCause::drown);
      }
    }
  } else {
    state.air = state.max_air;
    state.drown_timer = 0;
  }

  /* Lava, fire and suffocation. */
  if (ctx.lava_at_feet) {
    state.fire_ticks = std::max<uint32_t>(state.fire_ticks, 300);
    if (++state.lava_timer >= 10) {
      state.lava_timer = 0;
      survival_damage(state, 4.0f, DamageCause::lava);
    }
  } else if (ctx.fire_at_feet) {
    state.fire_ticks = std::max<uint32_t>(state.fire_ticks, 160);
  }
  if (ctx.in_water) {
    state.fire_ticks = 0;
    state.fire_timer = 0;
  } else if (state.fire_ticks > 0) {
    --state.fire_ticks;
    if (++state.fire_timer >= 20) {
      state.fire_timer = 0;
      survival_damage(state, 1.0f, DamageCause::fire);
    }
  }
  if (ctx.suffocating) {
    if (++state.suffocate_timer >= 20) {
      state.suffocate_timer = 0;
      survival_damage(state, 1.0f, DamageCause::suffocation);
    }
  } else {
    state.suffocate_timer = 0;
  }
  if (ctx.y < -64.0) {
    if (++state.void_timer >= 10) {
      state.void_timer = 0;
      survival_damage(state, 4.0f, DamageCause::void_);
    }
  } else {
    state.void_timer = 0;
  }

  /* Fall damage: accumulate while descending, apply on landing. */
  if (ctx.on_ground) {
    if (!state.was_on_ground) {
      const float damage = survival_fall_damage(state.fall_distance);
      if (damage > 0.0f) survival_damage(state, damage, DamageCause::fall);
    }
    state.fall_distance = 0.0;
  } else if (!ctx.in_water && ctx.vy < 0.0) {
    state.fall_distance += -ctx.vy;
  }
  state.was_on_ground = ctx.on_ground;

  state.saturation = std::clamp(state.saturation, 0.0f, state.food);
  state.food = std::clamp(state.food, 0.0f, 20.0f);
  state.air = std::clamp(state.air, 0.0f, state.max_air);
}

} // namespace reim::game
