/*
 * REIM UI Contract, version 3 (M3).
 *
 * The Runtime owns every modal screen, its layout, its hit testing and its
 * inventory semantics (M3-D05). It publishes an immutable, declarative 2D
 * display list; the Host only draws the supplied items, forwards pointer/key
 * intents and draws a cursor. No slot mapping, layout, translation or
 * inventory rule exists on the Host.
 *
 * Coordinates
 *   x/y/w/h and clip_* are in Host frame pixels at ui_scale 1, top-left
 *   origin, +y down. The Host multiplies by ui_scale for final placement
 *   unless it renders into an equivalent virtual canvas. Text is already
 *   resolved into glyph quads by the Runtime; text_bytes is the UTF-8 source
 *   kept for accessibility/diagnostics (never re-laid out by the Host).
 *
 * Texture ids
 *   draw.texture selects a Runtime texture (see ReimUiTexture). UVs are
 *   normalized 0..1. Solid rectangles ignore UVs.
 *
 * Ownership
 *   draws/text/inventory arrays live in the Host-owned frame blob and are
 *   valid until release_frame.
 */
#ifndef REIM_UI_H
#define REIM_UI_H

#include <stddef.h>
#include <stdint.h>

#include "reim_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIM_MAX_UI_DRAWS 8192u
#define REIM_MAX_UI_TEXT_BYTES (1024u * 1024u)

typedef enum ReimUiDrawKind {
  REIM_UI_DRAW_RECT = 1,
  REIM_UI_DRAW_TEXTURED_QUAD = 2,
  REIM_UI_DRAW_GLYPH_QUAD = 3
} ReimUiDrawKind;

typedef enum ReimUiDrawFlags {
  REIM_UI_FLAG_BLEND = 0x00000001u,
  REIM_UI_FLAG_ALPHA_MASK = 0x00000002u, /* glyphs / cutout textures */
  REIM_UI_FLAG_CLIP = 0x00000004u,       /* clip_* is valid */
  REIM_UI_FLAG_ITEM_ICON = 0x00000008u   /* textured quad computed by Runtime */
} ReimUiDrawFlags;

typedef enum ReimUiTexture {
  REIM_UI_TEXTURE_SOLID = 0,    /* RECT only */
  REIM_UI_TEXTURE_BLOCK_ATLAS = 1,
  REIM_UI_TEXTURE_FONT_ATLAS = 2,
  REIM_UI_TEXTURE_UI_ATLAS = 3,
  REIM_UI_TEXTURE_ITEM_ATLAS = 4
} ReimUiTexture;

typedef enum ReimUiCursorMode {
  REIM_UI_CURSOR_HIDDEN = 0,
  REIM_UI_CURSOR_ARROW = 1,
  REIM_UI_CURSOR_TEXT = 2,
  REIM_UI_CURSOR_HAND = 3,
  REIM_UI_CURSOR_CROSS = 4
} ReimUiCursorMode;

typedef struct ReimUiDraw {
  uint32_t kind;         /* ReimUiDrawKind */
  uint32_t flags;        /* ReimUiDrawFlags */
  float    x, y, w, h;
  float    u0, v0, u1, v1;
  float    clip_x, clip_y, clip_w, clip_h; /* w/h <= 0 means no clip */
  uint32_t color;        /* RGBA8 tint; r in the low byte */
  uint32_t texture;      /* ReimUiTexture */
  uint32_t text_offset;  /* byte offset into ReimUiFrame.text */
  uint32_t text_length;
  uint32_t reserved[2];
} ReimUiDraw;

#define REIM_UI_DRAW_V3_SIZE sizeof(ReimUiDraw)

/* A Runtime element id for hover/focus diagnostics (not a Host widget). */
typedef struct ReimUiFrame {
  uint32_t struct_size;
  uint32_t screen_id;        /* ReimScreenId */
  uint32_t screen_revision;  /* monotonic per screen transition */
  uint32_t ui_scale;         /* integer layout scale, >= 1 */
  uint32_t canvas_width;     /* pixels the Runtime laid out for */
  uint32_t canvas_height;
  uint32_t cursor_mode;      /* ReimUiCursorMode */
  float    cursor_x, cursor_y; /* pointer in frame pixels (Runtime hit state) */
  uint32_t draw_count;
  const ReimUiDraw* draws;   /* blob-owned */
  uint32_t text_bytes;
  const char* text;          /* blob-owned UTF-8, not NUL terminated */
  ReimInventorySnapshot inventory; /* blob-owned slot array */
  uint32_t hovered_element;  /* Runtime element id, 0 = none */
  uint32_t hovered_slot;     /* slot under the pointer, 0xFFFFFFFF = none */
  uint32_t focused_element;
  uint32_t reserved[8];
} ReimUiFrame;

#define REIM_UI_SLOT_NONE 0xFFFFFFFFu

#define REIM_UI_FRAME_MIN_SIZE \
  ((uint32_t)(offsetof(ReimUiFrame, reserved) + \
              sizeof(((ReimUiFrame*)0)->reserved)))

/* Every draw is 16-byte aligned inside the frame blob. */
#define REIM_UI_DRAW_ALIGN 16u

#ifdef __cplusplus
static_assert(offsetof(ReimUiDraw, x) == 8, "ReimUiDraw v3 layout changed");
static_assert(offsetof(ReimUiDraw, reserved) == 72,
              "ReimUiDraw v3 layout changed");
static_assert(offsetof(ReimUiFrame, inventory) + sizeof(ReimInventorySnapshot) ==
                  offsetof(ReimUiFrame, hovered_element),
              "ReimUiFrame v3 layout changed");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_UI_H */
