/*
 * REIM Runtime <-> Host C ABI, version 2 (M2).
 *
 * Scope
 *   This header is ONLY the boundary between a Runtime Host (REIMPreview.exe,
 *   reim-headless.exe, the UE runtime) and the version-specific native gameplay
 *   DLL (mc_26.3.dll). It is not a Launcher/Converter protocol.
 *
 * Threading
 *   create/destroy/start/stop  : control thread, not concurrent with each other.
 *                                destroy() also frees any frame blobs the Host
 *                                has not released yet.
 *   fetch_frame                : Host game/render thread only; never concurrent.
 *   release_frame              : any thread (GT or RT); must not block the
 *                                simulation. Releasing an unknown or already
 *                                released frame_id is a logged no-op.
 *   submit_input               : any thread; events are queued and applied on
 *                                the next 20 TPS simulation tick.
 *   get_runtime_info/get_asset : any thread for the lifetime of the instance;
 *                                returned memory is read-only and owned by the
 *                                Runtime (valid until destroy).
 *   Simulation runs on a DLL-internal thread at a fixed 20 TPS and is never
 *   driven by the Host frame rate.
 *
 * Host callbacks
 *   ReimHostApi::alloc/free are REQUIRED in v2 and must be thread safe. alloc
 *   returns memory aligned to at least the requested alignment; free accepts
 *   pointers produced by alloc. Neither may call back into the Runtime or throw.
 *   The log callback is optional; its strings are valid only for the call.
 *
 * Units and coordinates
 *   1 unit = 1 block = 1 metre. Right-handed, +Y up, +X east, +Z south.
 *   yaw is in degrees, 0 faces +Z and increases clockwise when viewed from
 *   above. pitch is in degrees, positive looks down, clamped to [-90, 90].
 *   Camera FOV is vertical, in degrees.
 *
 * Ownership / lifetime
 *   The Runtime copies each frame into a single host-owned blob obtained from
 *   ReimHostApi::alloc (see reim_render.h). The Host releases it with
 *   release_frame, which calls ReimHostApi::free. Up to
 *   REIM_MAX_PENDING_FRAMES blobs may be outstanding. Strings crossing this ABI
 *   are UTF-16 on Windows paths and UTF-8 for logs. No C++ objects, STL
 *   containers or C++ exceptions cross this boundary.
 *
 * Errors
 *   All entry points report failure by return code and by the Host log callback.
 *   The DLL is built with C++ exception handling disabled; exceptions never
 *   cross the ABI.
 */
#ifndef REIM_ABI_H
#define REIM_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "reim_render.h"

#define REIM_ABI_VERSION 2u

#if defined(_WIN32)
#  if defined(REIM_BUILD_DLL)
#    define REIM_EXPORT __declspec(dllexport)
#  else
#    define REIM_EXPORT __declspec(dllimport)
#  endif
#  define REIM_CALL __cdecl
#else
#  define REIM_EXPORT __attribute__((visibility("default")))
#  define REIM_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ReimResult {
  REIM_OK = 0,
  REIM_ERR_INVALID_ARGUMENT = 1,
  REIM_ERR_ABI_MISMATCH = 2,
  REIM_ERR_NOT_FOUND = 3,
  REIM_ERR_IO = 4,
  REIM_ERR_PARSE = 5,
  REIM_ERR_VERSION_MISMATCH = 6,
  REIM_ERR_NO_WORLD = 7,
  REIM_ERR_NOT_STARTED = 8,
  REIM_ERR_ALREADY_STARTED = 9,
  REIM_ERR_INTERNAL = 10,
  /* v2: the Host has too many unreleased frame blobs. Nothing was allocated or
   * consumed; retry after release_frame. Simulation keeps running. */
  REIM_ERR_BUSY = 11,
  /* v2: requested profile/capability (for example render_level != 0) is not
   * supported by this build. */
  REIM_ERR_CAPABILITY = 12,
  /* v2: a frame exceeded REIM_MAX_FRAME_UPDATES/REMOVES/BYTES. Pending events
   * are retained; reduce render distance and retry. */
  REIM_ERR_LIMIT = 13
} ReimResult;

/* ReimRuntimeInfo.capabilities bits. */
#define REIM_CAP_FRAME_BLOB 0x00000001u      /* v2 host-owned frame blob */
#define REIM_CAP_INTERPOLATION 0x00000002u   /* prev_* + tick_alpha */
#define REIM_CAP_ATLAS_ASSET 0x00000004u     /* get_asset atlas PNG/meta */
#define REIM_CAP_R0 0x00000008u              /* render_level 0 supported */

typedef enum ReimLogLevel {
  REIM_LOG_TRACE = 0,
  REIM_LOG_DEBUG = 1,
  REIM_LOG_INFO = 2,
  REIM_LOG_WARN = 3,
  REIM_LOG_ERROR = 4
} ReimLogLevel;

typedef enum ReimInputType {
  REIM_INPUT_KEY = 1,
  REIM_INPUT_MOUSE_MOTION = 2,
  REIM_INPUT_MOUSE_BUTTON = 3
} ReimInputType;

typedef enum ReimInputAction {
  REIM_ACTION_RELEASE = 0,
  REIM_ACTION_PRESS = 1,
  REIM_ACTION_REPEAT = 2
} ReimInputAction;

/* Host-defined key codes. The Host maps its native keys to these values. */
typedef enum ReimKey {
  REIM_KEY_UNKNOWN = 0,
  REIM_KEY_W = 1,
  REIM_KEY_A = 2,
  REIM_KEY_S = 3,
  REIM_KEY_D = 4,
  REIM_KEY_SPACE = 5,
  REIM_KEY_LEFT_SHIFT = 6,
  REIM_KEY_LEFT_CONTROL = 7,
  REIM_KEY_ESCAPE = 8,
  REIM_KEY_F1 = 9,
  REIM_KEY_F3 = 10,
  REIM_KEY_F5 = 11,
  REIM_KEY_UP = 12,
  REIM_KEY_DOWN = 13,
  REIM_KEY_LEFT = 14,
  REIM_KEY_RIGHT = 15
} ReimKey;

typedef struct ReimHostApi {
  uint32_t struct_size;
  uint32_t abi_version;

  void* (REIM_CALL* alloc)(size_t size, size_t align);
  void  (REIM_CALL* free)(void* ptr);
  /* category/utf8 are UTF-8 and valid only for the duration of the call. */
  void  (REIM_CALL* log)(int32_t level, const char* category, const char* utf8);
} ReimHostApi;

typedef struct ReimInstanceConfig {
  uint32_t struct_size;
  uint32_t abi_version;

  /* Absolute path to the 26.3 save directory (read-only for M1). */
  const wchar_t* world_path;
  /* Absolute path to generated/26.3 (blocks.json, textures.json, atlas.png). */
  const wchar_t* generated_path;
  /* Optional user Minecraft directory; recorded in diagnostics, not required. */
  const wchar_t* minecraft_dir;

  /* Chunk radius around the player, default 6. */
  uint32_t render_distance;
  /* Bit flags, reserved for M1 (must be 0). */
  uint32_t flags;
  /* Degrees per mouse pixel, default 0.15. */
  float mouse_sensitivity;
  /* Non-zero y inverts pitch input. */
  uint32_t invert_y;

  /* Optional spawn override. When flags has REIM_CONFIG_FLAG_SPAWN_OVERRIDE,
   * this foot position replaces Data.spawn. This exists for diagnostics and
   * automated runs; normal runs use the real level.dat spawn. */
  float spawn_x;
  float spawn_y;
  float spawn_z;
  uint32_t reserved[4];

  /* ---- v2 additions ---------------------------------------------------- */

  /* Host instance directory (contains instance.json and logs). Diagnostics
   * only; the Runtime does not read Host configuration files. */
  const wchar_t* instance_dir;
  /* NativeVersion directory the Host loaded the DLL from. Diagnostics and log
   * context; version validation of native_version.json/ue_adapter.json is a UE
   * Host responsibility (M2-E6). Optional for Preview/headless. */
  const wchar_t* native_version_path;
  /* Requested render level. Only 0 (R0) is supported in M2; any other value
   * makes create() fail with REIM_ERR_CAPABILITY. */
  uint32_t render_level;
  /* Optional spawn orientation for diagnostics/screenshots, in degrees using
   * the Runtime convention. Applied only with REIM_CONFIG_FLAG_SPAWN_OVERRIDE. */
  float spawn_yaw;
  float spawn_pitch;
  uint32_t reserved2[4];
} ReimInstanceConfig;

#define REIM_CONFIG_FLAG_SPAWN_OVERRIDE 0x00000001u

typedef struct ReimInputEvent {
  uint32_t struct_size;
  uint32_t type;
  int32_t  key;    /* ReimKey for KEY/MOUSE_BUTTON */
  int32_t  action; /* ReimInputAction */
  int32_t  mods;
  float    dx;     /* MOUSE_MOTION: horizontal pixels since last event */
  float    dy;     /* MOUSE_MOTION: vertical pixels since last event */
} ReimInputEvent;

/* v1 prefix size; kept so callers can sanity-check older layouts. */
#define REIM_INSTANCE_CONFIG_V1_SIZE \
  ((uint32_t)(offsetof(ReimInstanceConfig, reserved) + sizeof(((ReimInstanceConfig*)0)->reserved)))
/* v2 required size: every field through the last reserved slot must be known. */
#define REIM_INSTANCE_CONFIG_MIN_SIZE \
  ((uint32_t)(offsetof(ReimInstanceConfig, reserved2) + \
              sizeof(((ReimInstanceConfig*)0)->reserved2)))
#define REIM_INPUT_EVENT_MIN_SIZE \
  ((uint32_t)(offsetof(ReimInputEvent, dy) + sizeof(float)))

typedef struct ReimInstance ReimInstance;

/* v2 runtime identity/capabilities, validated by the UE Host before world
 * creation (M2-B5/M2-E6). */
typedef struct ReimRuntimeInfo {
  uint32_t struct_size;
  uint32_t abi_version;
  char     profile[16];        /* "26.3" */
  char     build_id[64];       /* Runtime build version string */
  uint64_t manifest_hash;      /* stable hash of the accepted generated manifest */
  uint32_t capabilities;       /* REIM_CAP_* bits */
  uint32_t render_level;       /* 0 = R0 */
  uint32_t dll_abi_version;    /* ABI of the loaded DLL image */
  uint32_t reserved[7];
} ReimRuntimeInfo;

typedef enum ReimAssetKind {
  REIM_ASSET_ATLAS_PNG = 1,   /* PNG bytes, as stored in generated/atlas.png */
  REIM_ASSET_ATLAS_META = 2   /* UTF-8 JSON: width/height/tile/sampling/format/hash */
} ReimAssetKind;

#define REIM_RUNTIME_INFO_MIN_SIZE \
  ((uint32_t)(offsetof(ReimRuntimeInfo, reserved) + \
              sizeof(((ReimRuntimeInfo*)0)->reserved)))

typedef struct ReimRuntimeApi {
  uint32_t struct_size;
  uint32_t abi_version;

  /* create validates generated data (manifest/ABI/profile) and the config.
   * render_level != 0 fails with REIM_ERR_CAPABILITY. host with null
   * alloc/free fails with REIM_ERR_INVALID_ARGUMENT. */
  ReimInstance* (REIM_CALL* create)(const ReimInstanceConfig* config,
                                    const ReimHostApi* host,
                                    int32_t* out_error);
  void (REIM_CALL* destroy)(ReimInstance* instance);

  int32_t (REIM_CALL* start)(ReimInstance* instance);
  void    (REIM_CALL* stop)(ReimInstance* instance);

  /* Fills out with the latest snapshot copied into one host-owned blob.
   * Returns REIM_OK, REIM_ERR_BUSY (too many unreleased frames),
   * REIM_ERR_LIMIT (frame too large) or another error. A frame before initial
   * load completes has ready == 0. */
  int32_t (REIM_CALL* fetch_frame)(ReimInstance* instance, ReimRenderFrame* out);

  /* Queued input, applied on the next simulation tick. */
  void (REIM_CALL* submit_input)(ReimInstance* instance, const ReimInputEvent* event);

  /* Pause/resume the fixed-step simulation; rendering continues. */
  void (REIM_CALL* set_simulation_paused)(ReimInstance* instance, uint8_t paused);

  /* M1 diagnostics (optional for other hosts): returns UTF-8 JSON status text
   * into the caller buffer. Returns bytes written or a negative error code. */
  int32_t (REIM_CALL* get_status_json)(ReimInstance* instance, char* buffer,
                                       uint32_t buffer_size);

  /* ---- v2 additions ---------------------------------------------------- */

  /* Returns the Host-owned blob of the matching fetch_frame to the Runtime.
   * Any thread; unknown/duplicate ids are a logged no-op. */
  void (REIM_CALL* release_frame)(ReimInstance* instance, uint64_t frame_id);

  /* Fills the caller-provided ReimRuntimeInfo (struct_size checked). */
  int32_t (REIM_CALL* get_runtime_info)(ReimInstance* instance,
                                        ReimRuntimeInfo* out);

  /* Returns read-only asset memory owned by the Runtime, valid until destroy.
   * out_revision changes when the instance's asset content changes. */
  int32_t (REIM_CALL* get_asset)(ReimInstance* instance, uint32_t kind,
                                 const void** out_data, uint64_t* out_size,
                                 uint64_t* out_revision);
} ReimRuntimeApi;

/* Returns nullptr when abi_version does not match REIM_ABI_VERSION.
 * The returned table is owned by the DLL and remains valid for the process. */
REIM_EXPORT const ReimRuntimeApi* REIM_CALL reim_get_api(uint32_t abi_version);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_ABI_H */
