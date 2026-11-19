/*
 * Camera, projection and frustum math for REIMPreview.
 *
 * Pure functions only (no GL), so the host math is unit-tested by m1-tests.
 * Coordinate contract matches runtime/contract/reim_abi.h: right-handed,
 * +Y up, +X east, +Z south; yaw 0 faces +Z and increases clockwise seen from
 * above; pitch is positive looking down. Matrices are column-major, as
 * consumed by glUniformMatrix4fv with transpose = GL_FALSE.
 */
#pragma once

#include "reim_render.h"

namespace reim::preview {

struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

ReimVec3 make_vec3(float x, float y, float z);
float dot(ReimVec3 a, ReimVec3 b);
ReimVec3 cross(ReimVec3 a, ReimVec3 b);
ReimVec3 normalize(ReimVec3 v);

/* Unit view direction for the camera angles. */
ReimVec3 forward_from_angles(float yaw_degrees, float pitch_degrees);
/* Horizontal right vector (independent of pitch). */
ReimVec3 right_from_yaw(float yaw_degrees);
/* Unit up vector for the current view direction. */
ReimVec3 up_from_angles(float yaw_degrees, float pitch_degrees);

Mat4 perspective(float fov_y_degrees, float aspect, float z_near, float z_far);
/* World -> view rotation/translation for an eye at angles. */
Mat4 view_from_angles(ReimVec3 eye, float yaw_degrees, float pitch_degrees);
Mat4 multiply(const Mat4& a, const Mat4& b);
ReimVec3 transform_point(const Mat4& m, ReimVec3 p);

struct Frustum {
  /* Six planes, inside when dot(n, p) + d >= 0. xyz normalized. */
  float planes[6][4] = {};
};

Frustum frustum_from_view_proj(const Mat4& view_proj);
bool box_in_frustum(const Frustum& frustum, const ReimBounds& bounds);
bool point_in_frustum(const Frustum& frustum, ReimVec3 point);

/* Third-person eye: the first-person eye pulled back along the view. */
ReimVec3 third_person_eye(ReimVec3 eye, float yaw_degrees, float pitch_degrees,
                          float distance);
float distance_squared(ReimVec3 a, ReimVec3 b);

} // namespace reim::preview
