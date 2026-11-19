#pragma once

#include "data/block_table.h"

#include <cstdint>

namespace reim::core {

constexpr double kPlayerHalfWidth = 0.3;
constexpr double kPlayerHeight = 1.8;
constexpr double kPlayerEyeHeight = 1.62;

/* Position is the feet centre; velocity is in blocks per tick. */
struct Player {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double vx = 0.0;
  double vy = 0.0;
  double vz = 0.0;
  float yaw = 0.0f;   // degrees, 0 = +Z, clockwise
  float pitch = 0.0f; // degrees, positive down
  bool on_ground = false;
  bool in_water = false;
  uint32_t flags = 0;
};

struct InputState {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool jump_held = false;
  bool sneak = false;
  bool sprint = false;
  /* Arrow keys are optional look controls for hosts without mouse capture. */
  bool look_up = false;
  bool look_down = false;
  bool look_left = false;
  bool look_right = false;
  /* Set on a press edge and consumed by the next tick. */
  bool jump_requested = false;
};

using BlockIdQuery = uint32_t (*)(const void* context, int x, int y, int z);
using CollisionQuery = bool (*)(const void* context, int x, int y, int z);

/* Mouse look: dx/dy are pixels since the previous event, sensitivity is
 * degrees per pixel. Positive dy looks down unless `invert` is set. */
void apply_mouse(Player& player, float dx, float dy, float sensitivity, bool invert);

/* Applies a look delta and keeps pitch in [-90, 90] and yaw in [0, 360). */
void apply_look(Player& player, float delta_yaw, float delta_pitch);

/* Distance a camera can be pulled straight back from the eye before the first
 * colliding block, up to `max_distance`. Used by third-person Hosts; a value
 * of 0 means the eye itself is inside a block. */
float camera_backoff_distance(double eye_x, double eye_y, double eye_z,
                              float yaw_degrees, float pitch_degrees,
                              float max_distance, CollisionQuery query,
                              const void* context);

/* Advances the player by one 20 TPS tick: input, gravity, jump, axis-separated
 * AABB collision against full-cube `collision` blocks. */
void player_step(Player& player, InputState& input, BlockIdQuery query,
                 const void* context, const data::BlockTable& blocks);

} // namespace reim::core
