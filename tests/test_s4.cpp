#include "catch.hpp"

#include "render_math.h"

#include <cmath>

namespace {

using reim::preview::Mat4;
using ReimVec3 = ::ReimVec3;

Mat4 camera_view_proj(float yaw, float pitch) {
  const Mat4 projection = reim::preview::perspective(70.0f, 16.0f / 9.0f, 0.05f, 256.0f);
  const Mat4 view = reim::preview::view_from_angles(ReimVec3{0.0f, 0.0f, 0.0f}, yaw, pitch);
  return reim::preview::multiply(projection, view);
}

ReimVec3 ndc(const Mat4& m, ReimVec3 p) {
  const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
  const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
  const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
  const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
  return ReimVec3{x / w, y / w, z / w};
}

ReimBounds box(float x, float y, float z, float size) {
  ReimBounds bounds{};
  bounds.min = ReimVec3{x, y, z};
  bounds.max = ReimVec3{x + size, y + size, z + size};
  return bounds;
}

} // namespace

TEST_CASE("S4 camera basis follows the ABI yaw/pitch contract") {
  const ReimVec3 forward = reim::preview::forward_from_angles(0.0f, 0.0f);
  CHECK(std::fabs(forward.x) < 1e-5f);
  CHECK(std::fabs(forward.y) < 1e-5f);
  CHECK(std::fabs(forward.z - 1.0f) < 1e-5f);

  const ReimVec3 west = reim::preview::forward_from_angles(90.0f, 0.0f);
  CHECK(std::fabs(west.x + 1.0f) < 1e-5f);
  CHECK(std::fabs(west.z) < 1e-5f);

  const ReimVec3 down = reim::preview::forward_from_angles(0.0f, 90.0f);
  CHECK(std::fabs(down.y + 1.0f) < 1e-5f);

  const ReimVec3 right = reim::preview::right_from_yaw(0.0f);
  CHECK(std::fabs(right.x + 1.0f) < 1e-5f);
  const ReimVec3 up = reim::preview::up_from_angles(0.0f, 0.0f);
  CHECK(std::fabs(up.y - 1.0f) < 1e-5f);
}

TEST_CASE("S4 view matrix places the eye at the origin looking down -Z") {
  const ReimVec3 eye{10.0f, 5.0f, -3.0f};
  const Mat4 view = reim::preview::view_from_angles(eye, 0.0f, 0.0f);
  const ReimVec3 origin = reim::preview::transform_point(view, eye);
  CHECK(std::fabs(origin.x) < 1e-4f);
  CHECK(std::fabs(origin.y) < 1e-4f);
  CHECK(std::fabs(origin.z) < 1e-4f);

  const ReimVec3 ahead =
      reim::preview::transform_point(view, ReimVec3{eye.x, eye.y, eye.z + 10.0f});
  CHECK(std::fabs(ahead.z + 10.0f) < 1e-4f);
}

TEST_CASE("S4 perspective maps the near and far planes to clip -1/+1") {
  const Mat4 projection = reim::preview::perspective(70.0f, 1.0f, 0.05f, 256.0f);
  const ReimVec3 near_point = ndc(projection, ReimVec3{0.0f, 0.0f, -0.05f});
  const ReimVec3 far_point = ndc(projection, ReimVec3{0.0f, 0.0f, -256.0f});
  CHECK(std::fabs(near_point.z + 1.0f) < 1e-3f);
  CHECK(std::fabs(far_point.z - 1.0f) < 1e-3f);
}

TEST_CASE("S4 frustum accepts a box ahead and rejects behind or far aside") {
  const Mat4 view_proj = camera_view_proj(0.0f, 0.0f);
  const reim::preview::Frustum frustum =
      reim::preview::frustum_from_view_proj(view_proj);

  CHECK(reim::preview::box_in_frustum(frustum, box(-1.0f, -1.0f, 5.0f, 2.0f)));
  CHECK_FALSE(reim::preview::box_in_frustum(frustum, box(-1.0f, -1.0f, -10.0f, 2.0f)));
  CHECK_FALSE(reim::preview::box_in_frustum(frustum, box(100.0f, -1.0f, 5.0f, 2.0f)));
  CHECK_FALSE(reim::preview::box_in_frustum(frustum, box(-1.0f, 200.0f, 5.0f, 2.0f)));
}

TEST_CASE("S4 frustum rotates with yaw") {
  const ReimBounds ahead = box(-1.0f, -1.0f, 5.0f, 2.0f);
  CHECK(reim::preview::box_in_frustum(
      reim::preview::frustum_from_view_proj(camera_view_proj(0.0f, 0.0f)), ahead));
  // yaw 90 faces -X, so the +Z box is now to the right and outside.
  CHECK_FALSE(reim::preview::box_in_frustum(
      reim::preview::frustum_from_view_proj(camera_view_proj(90.0f, 0.0f)), ahead));
}

TEST_CASE("S4 third-person eye sits behind the first-person eye") {
  const ReimVec3 eye{2.0f, 70.0f, 3.0f};
  const ReimVec3 third = reim::preview::third_person_eye(eye, 0.0f, 0.0f, 4.0f);
  CHECK(std::fabs(third.x - eye.x) < 1e-5f);
  CHECK(std::fabs(third.z - (eye.z - 4.0f)) < 1e-5f);
  CHECK(std::fabs(third.y - eye.y) < 1e-5f);

  const ReimVec3 down = reim::preview::third_person_eye(eye, 0.0f, 30.0f, 2.0f);
  CHECK(down.y > eye.y);  // pulled back opposite the downward view
}
