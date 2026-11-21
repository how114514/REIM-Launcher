/*
 * REIMBridge coordinate/unit conversion (M2-D06, M2-B7).
 *
 * This is the ONLY place where Minecraft Runtime coordinates are converted to
 * Unreal coordinates. The header is intentionally UE-free so the Catch2 tests
 * exercise exactly the same code the UE module compiles.
 *
 * Runtime contract (reim_abi.h): right-handed, +Y up, +X east, +Z south,
 * 1 unit = 1 block = 1 metre, yaw degrees 0 = +Z clockwise from above,
 * pitch positive down, camera FOV vertical.
 *
 * UE: left-handed, X forward, Y right, Z up, 1 uu = 1 cm.
 *
 *   UE_X =  MC_Z * 100
 *   UE_Y = -MC_X * 100
 *   UE_Z =  MC_Y * 100
 *   yaw_ue   = yaw_mc
 *   pitch_ue = -pitch_mc
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace reim::bridge {

constexpr double kUeUnitsPerMetre = 100.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/* Runtime metres/right-handed -> UE centimetres/left-handed. Used for
 * positions, vertex positions and normals (normals are not scaled). */
inline Vec3 PositionMetresToUe(double mc_x, double mc_y, double mc_z) {
  return Vec3{mc_z * kUeUnitsPerMetre, -mc_x * kUeUnitsPerMetre,
              mc_y * kUeUnitsPerMetre};
}

inline Vec3 NormalToUe(double mc_x, double mc_y, double mc_z) {
  return Vec3{mc_z, -mc_x, mc_y};
}

/* Runtime yaw/pitch -> UE yaw/pitch in degrees. UE yaw is clockwise from +X
 * (forward), which matches the Runtime's +Z-forward convention after the axis
 * swap; UE pitch is positive up, so the Runtime's positive-down pitch flips. */
inline void LookToUe(float mc_yaw_degrees, float mc_pitch_degrees,
                     float& out_yaw_degrees, float& out_pitch_degrees) {
  out_yaw_degrees = mc_yaw_degrees;
  out_pitch_degrees = -mc_pitch_degrees;
}

/* Unreal's SetFieldOfView takes horizontal FOV. The Runtime supplies vertical
 * FOV; convert with the current viewport aspect ratio (width / height). */
inline double VerticalToHorizontalFovDegrees(double vertical_fov_degrees,
                                             double aspect_ratio) {
  if (!(aspect_ratio > 0.0)) return vertical_fov_degrees;
  const double half_v = vertical_fov_degrees * 0.5 * kDegreesToRadians;
  const double half_h = std::atan(std::tan(half_v) * aspect_ratio);
  return half_h * 2.0 * kRadiansToDegrees;
}

/* Runtime clip planes are in metres; UE works in centimetres. */
inline double ClipPlaneMetresToUe(double metres) {
  return metres * kUeUnitsPerMetre;
}

/* R0 near/far contract: 5 uu near, (render_distance + 1) * 16 blocks far. */
constexpr double kR0NearUe = 5.0;

inline double R0FarUe(uint32_t render_distance) {
  return ClipPlaneMetresToUe(static_cast<double>(render_distance + 1u) * 16.0);
}

/* Shortest-path angle interpolation for yaw/pitch. Runtime yaw may cross
 * 0/360 between ticks; the Host must not spin the long way around. */
inline float InterpolateAngleDegrees(float from_degrees, float to_degrees,
                                     float alpha) {
  float delta = std::fmod(to_degrees - from_degrees, 360.0f);
  if (delta > 180.0f) delta -= 360.0f;
  if (delta < -180.0f) delta += 360.0f;
  return from_degrees + delta * alpha;
}

inline float InterpolateLinear(float from, float to, float alpha) {
  return from + (to - from) * alpha;
}

inline Vec3 InterpolatePosition(const Vec3& from, const Vec3& to, float alpha) {
  return Vec3{InterpolateLinear(static_cast<float>(from.x),
                                static_cast<float>(to.x), alpha),
              InterpolateLinear(static_cast<float>(from.y),
                                static_cast<float>(to.y), alpha),
              InterpolateLinear(static_cast<float>(from.z),
                                static_cast<float>(to.z), alpha)};
}

} // namespace reim::bridge
