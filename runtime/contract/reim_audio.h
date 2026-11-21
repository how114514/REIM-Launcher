/*
 * REIM Audio Contract, version 3 (M3).
 *
 * The Runtime emits sound events; the Host decodes/plays/mixes them. Audio
 * events never change gameplay state (M3-D07). Events are aggregated per
 * frame inside the Host-owned blob; the Host may drop the oldest ambient
 * events under the per-frame limit and must count what it dropped.
 *
 * sound_id indexes the Runtime's sound index asset (SOUND_INDEX_JSON). The
 * Host resolves an id to an OGG asset through get_asset(SOUND_OGG, id, ...).
 * id 0 is silence.
 */
#ifndef REIM_AUDIO_H
#define REIM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "reim_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIM_MAX_FRAME_AUDIO_EVENTS 4096u

typedef enum ReimAudioCategory {
  REIM_AUDIO_CATEGORY_MASTER = 0,
  REIM_AUDIO_CATEGORY_MUSIC = 1,
  REIM_AUDIO_CATEGORY_BLOCKS = 2,
  REIM_AUDIO_CATEGORY_CREATURES = 3,
  REIM_AUDIO_CATEGORY_AMBIENT = 4,
  REIM_AUDIO_CATEGORY_WEATHER = 5,
  REIM_AUDIO_CATEGORY_UI = 6,
  REIM_AUDIO_CATEGORY_PLAYERS = 7
} ReimAudioCategory;

typedef struct ReimAudioEvent {
  uint32_t struct_size;
  uint32_t sound_id;      /* sound index asset id; 0 = none */
  ReimVec3 position;      /* world position; all-zero for UI/2D sounds */
  float    volume;        /* 0..1 linear gain before category volume */
  float    pitch;         /* playback rate multiplier, 1 = nominal */
  uint32_t category;      /* ReimAudioCategory */
  uint32_t flags;         /* REIM_AUDIO_FLAG_* */
  uint64_t sequence;      /* Runtime monotonic per instance */
  uint32_t reserved[4];
} ReimAudioEvent;

#define REIM_AUDIO_FLAG_LOOP 0x00000001u
#define REIM_AUDIO_FLAG_2D   0x00000002u  /* UI/music; ignore position */
#define REIM_AUDIO_FLAG_CRITICAL 0x00000004u /* never drop under pressure */

#define REIM_AUDIO_EVENT_MIN_SIZE \
  ((uint32_t)(offsetof(ReimAudioEvent, reserved) + \
              sizeof(((ReimAudioEvent*)0)->reserved)))

#ifdef __cplusplus
static_assert(offsetof(ReimAudioEvent, reserved) == 48,
              "ReimAudioEvent v3 layout changed");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_AUDIO_H */
