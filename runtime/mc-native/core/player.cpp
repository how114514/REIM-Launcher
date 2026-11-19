#include "core/player.h"

#include <algorithm>
#include <cmath>

namespace reim::core {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-9;
constexpr double kGravity = 0.08;
constexpr double kWaterGravity = 0.02;
constexpr double kVerticalDrag = 0.98;
constexpr double kWaterVerticalDrag = 0.8;
constexpr double kTerminalVelocity = -3.92;
constexpr double kJumpVelocity = 0.42;
constexpr double kWalkSpeed = 0.21585;  // blocks per tick
constexpr double kSprintSpeed = 0.281;
constexpr double kSneakSpeed = 0.0649;

bool block_collides(const data::BlockTable& blocks, BlockIdQuery query,
                    const void* context, int x, int y, int z) {
  return blocks.def(query(context, x, y, z)).collision;
}

bool is_water(const data::BlockTable& blocks, BlockIdQuery query,
              const void* context, int x, int y, int z) {
  return blocks.def(query(context, x, y, z)).name == "minecraft:water";
}

struct Box {
  int x0, x1, y0, y1, z0, z1;
};

Box player_box(double x, double y, double z) {
  Box box;
  box.x0 = static_cast<int>(std::floor(x - kPlayerHalfWidth));
  box.x1 = static_cast<int>(std::floor(x + kPlayerHalfWidth - kEpsilon));
  box.y0 = static_cast<int>(std::floor(y));
  box.y1 = static_cast<int>(std::floor(y + kPlayerHeight - kEpsilon));
  box.z0 = static_cast<int>(std::floor(z - kPlayerHalfWidth));
  box.z1 = static_cast<int>(std::floor(z + kPlayerHalfWidth - kEpsilon));
  return box;
}

/* Axis-separated movement. Each axis moves, then snaps out of the first
 * colliding block band; motion never exceeds one block per tick. */
bool move_y(Player& player, double delta, const data::BlockTable& blocks,
            BlockIdQuery query, const void* context) {
  if (delta == 0.0) return false;
  player.y += delta;
  const Box box = player_box(player.x, player.y, player.z);
  bool hit = false;
  double snap = player.y;
  for (int y = box.y0; y <= box.y1; ++y) {
    for (int x = box.x0; x <= box.x1; ++x) {
      for (int z = box.z0; z <= box.z1; ++z) {
        if (!block_collides(blocks, query, context, x, y, z)) continue;
        hit = true;
        if (delta < 0.0) {
          snap = std::max(snap, static_cast<double>(y + 1));
        } else {
          snap = std::min(snap, static_cast<double>(y) - kPlayerHeight);
        }
      }
    }
  }
  if (hit) player.y = snap;
  return hit;
}

bool move_horizontal(Player& player, double dx, double dz,
                     const data::BlockTable& blocks, BlockIdQuery query,
                     const void* context) {
  bool hit = false;
  if (dx != 0.0) {
    player.x += dx;
    const Box box = player_box(player.x, player.y, player.z);
    double snap = player.x;
    for (int x = box.x0; x <= box.x1; ++x) {
      for (int y = box.y0; y <= box.y1; ++y) {
        for (int z = box.z0; z <= box.z1; ++z) {
          if (!block_collides(blocks, query, context, x, y, z)) continue;
          hit = true;
          if (dx > 0.0) {
            snap = std::min(snap, static_cast<double>(x) - kPlayerHalfWidth);
          } else {
            snap = std::max(snap, static_cast<double>(x + 1) + kPlayerHalfWidth);
          }
        }
      }
    }
    if (hit) player.x = snap;
  }
  if (dz != 0.0) {
    player.z += dz;
    const Box box = player_box(player.x, player.y, player.z);
    double snap = player.z;
    for (int x = box.x0; x <= box.x1; ++x) {
      for (int y = box.y0; y <= box.y1; ++y) {
        for (int z = box.z0; z <= box.z1; ++z) {
          if (!block_collides(blocks, query, context, x, y, z)) continue;
          hit = true;
          if (dz > 0.0) {
            snap = std::min(snap, static_cast<double>(z) - kPlayerHalfWidth);
          } else {
            snap = std::max(snap, static_cast<double>(z + 1) + kPlayerHalfWidth);
          }
        }
      }
    }
    if (hit) player.z = snap;
  }
  return hit;
}

} // namespace

void apply_look(Player& player, float delta_yaw, float delta_pitch) {
  player.yaw += delta_yaw;
  player.pitch = std::clamp(player.pitch + delta_pitch, -90.0f, 90.0f);
  player.yaw = std::fmod(player.yaw, 360.0f);
  if (player.yaw < 0.0f) player.yaw += 360.0f;
}

void apply_mouse(Player& player, float dx, float dy, float sensitivity, bool invert) {
  apply_look(player, dx * sensitivity, (invert ? -dy : dy) * sensitivity);
}

float camera_backoff_distance(double eye_x, double eye_y, double eye_z,
                              float yaw_degrees, float pitch_degrees,
                              float max_distance, CollisionQuery query,
                              const void* context) {
  if (query == nullptr) return std::max(0.0f, max_distance);
  const double yaw = static_cast<double>(yaw_degrees) * kPi / 180.0;
  const double pitch = static_cast<double>(pitch_degrees) * kPi / 180.0;
  // Camera looks along forward, so a third-person eye is pulled along -forward.
  const double forward_x = -std::sin(yaw) * std::cos(pitch);
  const double forward_y = -std::sin(pitch);
  const double forward_z = std::cos(yaw) * std::cos(pitch);
  const double limit = std::max(0.0, static_cast<double>(max_distance));
  constexpr double kStep = 0.05;
  constexpr double kMargin = 0.2;
  for (double distance = 0.0; distance <= limit + 1e-9; distance += kStep) {
    const int x = static_cast<int>(
        std::floor(eye_x - forward_x * distance));
    const int y = static_cast<int>(
        std::floor(eye_y - forward_y * distance));
    const int z = static_cast<int>(
        std::floor(eye_z - forward_z * distance));
    if (query(context, x, y, z)) {
      return static_cast<float>(std::max(0.0, distance - kMargin));
    }
  }
  return static_cast<float>(limit);
}

void player_step(Player& player, InputState& input, BlockIdQuery query,
                 const void* context, const data::BlockTable& blocks) {
  player.flags = 0;

  // Arrow-key look runs at a fixed rate per tick so it is independent of the
  // Host frame rate. 5 degrees per tick is 100 degrees per second at 20 TPS.
  constexpr float kArrowLookPerTick = 5.0f;
  float look_yaw = 0.0f;
  float look_pitch = 0.0f;
  if (input.look_left) look_yaw -= kArrowLookPerTick;
  if (input.look_right) look_yaw += kArrowLookPerTick;
  if (input.look_up) look_pitch -= kArrowLookPerTick;
  if (input.look_down) look_pitch += kArrowLookPerTick;
  if (look_yaw != 0.0f || look_pitch != 0.0f) {
    apply_look(player, look_yaw, look_pitch);
  }

  player.in_water =
      is_water(blocks, query, context, static_cast<int>(std::floor(player.x)),
               static_cast<int>(std::floor(player.y + 0.2)),
               static_cast<int>(std::floor(player.z)));
  if (player.in_water) player.flags |= 0x2u;

  const double forward = (input.forward ? 1.0 : 0.0) - (input.back ? 1.0 : 0.0);
  const double strafe = (input.right ? 1.0 : 0.0) - (input.left ? 1.0 : 0.0);
  const double input_length = std::sqrt(forward * forward + strafe * strafe);
  const double yaw_radians = static_cast<double>(player.yaw) * kPi / 180.0;
  const double forward_x = -std::sin(yaw_radians);
  const double forward_z = std::cos(yaw_radians);
  const double right_x = -std::cos(yaw_radians);
  const double right_z = -std::sin(yaw_radians);

  double speed = input.sneak ? kSneakSpeed : (input.sprint ? kSprintSpeed : kWalkSpeed);
  if (player.in_water) speed *= 0.5;
  double target_x = 0.0;
  double target_z = 0.0;
  if (input_length > 0.0) {
    const double dir_x = (forward_x * forward + right_x * strafe) / input_length;
    const double dir_z = (forward_z * forward + right_z * strafe) / input_length;
    target_x = dir_x * speed;
    target_z = dir_z * speed;
  }
  const double acceleration = player.on_ground ? 0.5 : 0.12;
  player.vx += (target_x - player.vx) * acceleration;
  player.vz += (target_z - player.vz) * acceleration;
  if (input_length == 0.0 && player.on_ground) {
    player.vx *= 0.6;
    player.vz *= 0.6;
  }

  if (input.jump_requested && player.on_ground && !player.in_water) {
    player.vy = kJumpVelocity;
    player.on_ground = false;
  }
  input.jump_requested = false;

  if (player.in_water) {
    if (input.jump_held) player.vy = std::min(0.15, player.vy + 0.05);
    player.vy -= kWaterGravity;
    player.vy *= kWaterVerticalDrag;
  } else {
    player.vy -= kGravity;
    player.vy *= kVerticalDrag;
  }
  player.vy = std::max(player.vy, kTerminalVelocity);

  const bool vertical_hit = move_y(player, player.vy, blocks, query, context);
  if (vertical_hit) {
    if (player.vy < 0.0) player.on_ground = true;
    player.vy = 0.0;
  } else {
    player.on_ground = false;
  }

  // Each axis is resolved independently, which lets the player slide along a
  // wall instead of stopping on it.
  const bool horizontal_hit =
      move_horizontal(player, player.vx, player.vz, blocks, query, context);
  (void)horizontal_hit;

  // A grounded player keeps standing even when the exact contact leaves a
  // tiny gap after snapping.
  if (!player.on_ground) {
    const int foot_x = static_cast<int>(std::floor(player.x));
    const int foot_z = static_cast<int>(std::floor(player.z));
    const int foot_y = static_cast<int>(std::floor(player.y - 0.05));
    if (block_collides(blocks, query, context, foot_x, foot_y, foot_z)) {
      player.on_ground = true;
    }
  }
  if (player.on_ground) player.flags |= 0x1u;
  if (input.sprint) player.flags |= 0x4u;
}

} // namespace reim::core
