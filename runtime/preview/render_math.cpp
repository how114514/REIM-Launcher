#include "render_math.h"

#include <cmath>

namespace reim::preview {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

} // namespace

ReimVec3 make_vec3(float x, float y, float z) { return ReimVec3{x, y, z}; }

float dot(ReimVec3 a, ReimVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

ReimVec3 cross(ReimVec3 a, ReimVec3 b) {
  return ReimVec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                  a.x * b.y - a.y * b.x};
}

ReimVec3 normalize(ReimVec3 v) {
  const float length = std::sqrt(dot(v, v));
  if (length <= 1e-12f) return ReimVec3{0.0f, 0.0f, 0.0f};
  return ReimVec3{v.x / length, v.y / length, v.z / length};
}

ReimVec3 forward_from_angles(float yaw_degrees, float pitch_degrees) {
  const float yaw = yaw_degrees * kDegToRad;
  const float pitch = pitch_degrees * kDegToRad;
  return normalize(ReimVec3{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                            std::cos(yaw) * std::cos(pitch)});
}

ReimVec3 right_from_yaw(float yaw_degrees) {
  const float yaw = yaw_degrees * kDegToRad;
  return ReimVec3{-std::cos(yaw), 0.0f, -std::sin(yaw)};
}

ReimVec3 up_from_angles(float yaw_degrees, float pitch_degrees) {
  return normalize(cross(right_from_yaw(yaw_degrees),
                         forward_from_angles(yaw_degrees, pitch_degrees)));
}

Mat4 perspective(float fov_y_degrees, float aspect, float z_near, float z_far) {
  Mat4 out;
  const float f = 1.0f / std::tan(fov_y_degrees * kDegToRad * 0.5f);
  const float safe_aspect = aspect > 1e-6f ? aspect : 1.0f;
  out.m[0] = f / safe_aspect;
  out.m[5] = f;
  out.m[10] = (z_far + z_near) / (z_near - z_far);
  out.m[11] = -1.0f;
  out.m[14] = (2.0f * z_far * z_near) / (z_near - z_far);
  out.m[15] = 0.0f;
  return out;
}

Mat4 view_from_angles(ReimVec3 eye, float yaw_degrees, float pitch_degrees) {
  const ReimVec3 f = forward_from_angles(yaw_degrees, pitch_degrees);
  const ReimVec3 r = right_from_yaw(yaw_degrees);
  const ReimVec3 u = normalize(cross(r, f));

  Mat4 out;
  out.m[0] = r.x;
  out.m[4] = r.y;
  out.m[8] = r.z;
  out.m[12] = -dot(r, eye);
  out.m[1] = u.x;
  out.m[5] = u.y;
  out.m[9] = u.z;
  out.m[13] = -dot(u, eye);
  out.m[2] = -f.x;
  out.m[6] = -f.y;
  out.m[10] = -f.z;
  out.m[14] = dot(f, eye);
  return out;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 out;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a.m[k * 4 + row] * b.m[column * 4 + k];
      }
      out.m[column * 4 + row] = sum;
    }
  }
  return out;
}

ReimVec3 transform_point(const Mat4& m, ReimVec3 p) {
  return ReimVec3{m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                  m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                  m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

Frustum frustum_from_view_proj(const Mat4& view_proj) {
  const float* m = view_proj.m;
  const float row0[4] = {m[0], m[4], m[8], m[12]};
  const float row1[4] = {m[1], m[5], m[9], m[13]};
  const float row2[4] = {m[2], m[6], m[10], m[14]};
  const float row3[4] = {m[3], m[7], m[11], m[15]};

  Frustum frustum;
  for (int i = 0; i < 4; ++i) {
    frustum.planes[0][i] = row3[i] + row0[i];  // left
    frustum.planes[1][i] = row3[i] - row0[i];  // right
    frustum.planes[2][i] = row3[i] + row1[i];  // bottom
    frustum.planes[3][i] = row3[i] - row1[i];  // top
    frustum.planes[4][i] = row3[i] + row2[i];  // near
    frustum.planes[5][i] = row3[i] - row2[i];  // far
  }
  for (auto& plane : frustum.planes) {
    const float length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] +
                                   plane[2] * plane[2]);
    if (length > 1e-12f) {
      plane[0] /= length;
      plane[1] /= length;
      plane[2] /= length;
      plane[3] /= length;
    }
  }
  return frustum;
}

bool point_in_frustum(const Frustum& frustum, ReimVec3 point) {
  for (const auto& plane : frustum.planes) {
    if (plane[0] * point.x + plane[1] * point.y + plane[2] * point.z + plane[3] <
        0.0f) {
      return false;
    }
  }
  return true;
}

bool box_in_frustum(const Frustum& frustum, const ReimBounds& bounds) {
  for (const auto& plane : frustum.planes) {
    // Positive vertex: the box corner furthest along the plane normal.
    const ReimVec3 positive{
        plane[0] >= 0.0f ? bounds.max.x : bounds.min.x,
        plane[1] >= 0.0f ? bounds.max.y : bounds.min.y,
        plane[2] >= 0.0f ? bounds.max.z : bounds.min.z};
    if (plane[0] * positive.x + plane[1] * positive.y + plane[2] * positive.z +
            plane[3] <
        0.0f) {
      return false;
    }
  }
  return true;
}

ReimVec3 third_person_eye(ReimVec3 eye, float yaw_degrees, float pitch_degrees,
                          float distance) {
  const ReimVec3 forward = forward_from_angles(yaw_degrees, pitch_degrees);
  return ReimVec3{eye.x - forward.x * distance, eye.y - forward.y * distance,
                  eye.z - forward.z * distance};
}

float distance_squared(ReimVec3 a, ReimVec3 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

} // namespace reim::preview
