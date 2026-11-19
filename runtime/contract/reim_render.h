/*
 * REIM Render Contract (M1).
 *
 * The Runtime produces an immutable frame snapshot. The Host only uploads and
 * draws the supplied meshes; it never reconstructs world state or simulates
 * gameplay. Section meshes are sent as incremental updates: a section appears
 * in a frame when it is loaded or remeshed, and in the remove list when it is
 * unloaded. Every mesh carries a monotonically increasing revision so a Host
 * can discard stale work.
 *
 * Vertex layout is packed and stable:
 *   offset 0  float position[3]
 *   offset 12 float normal[3]
 *   offset 24 float uv[2]        (atlas normalized coordinates)
 *   offset 32 uint16 light       (high byte skylight 0..15, low byte blocklight 0..15)
 *   offset 34 uint16 sprite      (atlas tile index, informational)
 *   offset 36 uint32 tint        (RGBA8, r in the low byte)
 *   size 40
 */
#ifndef REIM_RENDER_H
#define REIM_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ReimVec3 {
  float x, y, z;
} ReimVec3;

typedef struct ReimBounds {
  ReimVec3 min;
  ReimVec3 max;
} ReimBounds;

typedef struct ReimVertex {
  float position[3];
  float normal[3];
  float uv[2];
  uint16_t light;
  uint16_t sprite;
  uint32_t tint;
} ReimVertex;

#define REIM_VERTEX_STRIDE 40u

typedef enum ReimMaterial {
  REIM_MATERIAL_OPAQUE = 0,
  REIM_MATERIAL_TRANSPARENT = 1
} ReimMaterial;

typedef struct ReimCameraSnapshot {
  ReimVec3 position;     /* eye position */
  float yaw;             /* degrees, 0 = +Z, clockwise */
  float pitch;           /* degrees, positive down */
  float fov_y_degrees;
  float near_z;
  float far_z;
  uint32_t dimension;    /* 0 = overworld in M1 */
} ReimCameraSnapshot;

typedef enum ReimPlayerFlags {
  REIM_PLAYER_ON_GROUND = 0x1u,
  REIM_PLAYER_IN_WATER = 0x2u,
  REIM_PLAYER_SPRINTING = 0x4u,
  REIM_PLAYER_FLYING = 0x8u
} ReimPlayerFlags;

typedef struct ReimPlayerSnapshot {
  ReimVec3 position;     /* feet (AABB centre at the bottom) */
  ReimVec3 velocity;     /* blocks per tick */
  float yaw;
  float pitch;
  float eye_height;
  float half_width;      /* 0.3 */
  float height;          /* 1.8 */
  uint32_t flags;
} ReimPlayerSnapshot;

typedef struct ReimEnvironmentSnapshot {
  uint32_t sky_color;    /* 0xRRGGBB */
  uint32_t fog_color;    /* 0xRRGGBB */
  float fog_start;
  float fog_end;
} ReimEnvironmentSnapshot;

typedef struct ReimSectionMeshUpdate {
  int32_t chunk_x;
  int32_t chunk_z;
  int32_t section_y;
  uint32_t revision;
  uint32_t material;
  ReimBounds bounds;
  uint32_t vertex_count;
  uint32_t vertex_stride;
  uint32_t index_count;
  const ReimVertex* vertices;
  const uint32_t* indices;
} ReimSectionMeshUpdate;

typedef struct ReimSectionRemove {
  int32_t chunk_x;
  int32_t chunk_z;
  int32_t section_y;
} ReimSectionRemove;

typedef struct ReimRenderFrame {
  uint32_t struct_size;
  uint32_t abi_version;

  uint64_t frame_id;
  uint64_t tick;
  float partial_tick;

  /* 0 while the initial spawn-area load is still running. */
  uint32_t ready;
  uint32_t loaded_chunks;
  uint32_t total_chunks;
  uint32_t loading_percent;

  ReimCameraSnapshot camera;
  ReimPlayerSnapshot player;
  ReimEnvironmentSnapshot environment;

  uint32_t update_count;
  uint32_t remove_count;
  const ReimSectionMeshUpdate* updates;
  const ReimSectionRemove* removes;

  uint32_t loaded_sections;
  uint32_t mesh_sections;
  uint64_t block_count;

  /* Measured simulation rate over the last second (informational). */
  float measured_tps;
  float measured_frame_ms;
} ReimRenderFrame;

#define REIM_RENDER_FRAME_MIN_SIZE \
  ((uint32_t)(offsetof(ReimRenderFrame, measured_frame_ms) + sizeof(float)))

#ifdef __cplusplus
static_assert(sizeof(ReimVertex) == REIM_VERTEX_STRIDE,
              "ReimVertex layout changed; update the ABI documentation");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_RENDER_H */
