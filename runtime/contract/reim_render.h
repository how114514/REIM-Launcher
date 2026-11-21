/*
 * REIM Render Contract, version 2 (M2).
 *
 * The Runtime produces an immutable frame snapshot. The Host only uploads and
 * draws the supplied meshes; it never reconstructs world state or simulates
 * gameplay. Section meshes are sent as incremental updates: a section appears
 * in a frame when it is loaded or remeshed, and in the remove list when it is
 * unloaded. Every mesh carries a monotonically increasing revision so a Host
 * can discard stale work.
 *
 * Frame ownership (v2, M2-D04)
 *   fetch_frame allocates ONE host-owned blob through ReimHostApi::alloc and
 *   copies every update/remove/vertex/index into it. The pointers inside
 *   ReimRenderFrame point into that blob, and owned_block/owned_bytes identify
 *   it. The Host must call release_frame(instance, frame_id) once it is done
 *   consuming the snapshot (after GPU upload for asynchronous renderers).
 *   The Runtime frees the blob through ReimHostApi::free inside release_frame.
 *   At most REIM_MAX_PENDING_FRAMES blobs may be outstanding; fetch_frame
 *   returns REIM_ERR_BUSY without mutating simulation state when the Host has
 *   not released an earlier frame.
 *
 * Size limits (B8)
 *   A frame may carry at most REIM_MAX_FRAME_UPDATES mesh updates, at most
 *   REIM_MAX_FRAME_REMOVES removes and at most REIM_MAX_FRAME_BYTES bytes of
 *   blob. Exceeding a limit returns REIM_ERR_LIMIT; the pending events are
 *   retained so the Host can reduce render distance and retry. Sections are
 *   never silently dropped.
 *
 * Interpolation (v2, M2-D05)
 *   camera/player are the current tick state; prev_camera/prev_player the
 *   previous published tick. tick_alpha is [0,1] between them. Positions
 *   interpolate linearly; yaw/pitch take the shortest angular path. tick_alpha
 *   is 0 while the simulation is paused or a tick has not advanced. Meshes are
 *   not interpolated; they change on tick boundaries.
 *
 * Vertex layout is packed and stable:
 *   offset 0  float position[3]  (world space, blocks; section coords + 0..16)
 *   offset 12 float normal[3]    (unit face normal, +X east / +Z south)
 *   offset 24 float uv[2]        (atlas normalized coordinates)
 *   offset 32 uint16 light       (high byte skylight 0..15, low byte blocklight 0..15)
 *   offset 34 uint16 sprite      (atlas tile index, informational)
 *   offset 36 uint32 tint        (RGBA8, r in the low byte)
 *   size 40
 *
 * An update whose vertex_count/index_count are zero replaces that (section,
 * material) with nothing, which is how the Runtime clears a material that a
 * remesh culled away. A SectionRemove drops every material of the section.
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

/* Host-side hard limits enforced by fetch_frame (v2). */
#define REIM_MAX_PENDING_FRAMES 4u
#define REIM_MAX_FRAME_UPDATES 4096u
#define REIM_MAX_FRAME_REMOVES 4096u
#define REIM_MAX_FRAME_BYTES (64ull * 1024ull * 1024ull)

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

  /* Maximum distance a third-person Host may pull the camera back along
   * -forward before it enters a colliding block (0..4 blocks). The Runtime
   * computes this because only it can query world collision. A first-person
   * Host may ignore it. */
  float third_person_distance;

  /* ---- v2 additions (M2-D04/M2-D05) ------------------------------------ */

  /* Previous published tick, for Host-side interpolation. When the simulation
   * is paused (or no tick has advanced since the last fetch) these equal the
   * current snapshots and tick_alpha is 0. */
  ReimCameraSnapshot prev_camera;
  ReimPlayerSnapshot prev_player;
  /* 0..1 between prev_* and the current snapshots. */
  float tick_alpha;

  /* Host-owned blob backing updates/removes/vertices/indices. Non-null for
   * every successful fetch (an empty frame still owns a small blob), so the
   * Host must call release_frame(instance, frame_id) for every returned frame
   * and must never free owned_block itself. */
  void* owned_block;
  uint64_t owned_bytes;
  uint32_t reserved[4];
} ReimRenderFrame;

#define REIM_RENDER_FRAME_MIN_SIZE \
  ((uint32_t)(offsetof(ReimRenderFrame, reserved) + \
              sizeof(((ReimRenderFrame*)0)->reserved)))

#ifdef __cplusplus
static_assert(sizeof(ReimVertex) == REIM_VERTEX_STRIDE,
              "ReimVertex layout changed; update the ABI documentation");
static_assert(offsetof(ReimRenderFrame, frame_id) == 8,
              "ReimRenderFrame prefix changed");
static_assert(offsetof(ReimRenderFrame, prev_camera) + sizeof(ReimCameraSnapshot) ==
                  offsetof(ReimRenderFrame, prev_player),
              "ReimRenderFrame interpolation layout changed");
static_assert(offsetof(ReimRenderFrame, owned_block) < REIM_RENDER_FRAME_MIN_SIZE,
              "ReimRenderFrame owned blob fields must precede reserved padding");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_RENDER_H */
