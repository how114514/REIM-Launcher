/*
 * REIM Game Contract, version 3 (M3).
 *
 * Items, inventory/container snapshots, interaction targets and screen state.
 * Gameplay authority stays in the Runtime (M3-D01/M3-D04): the Host only
 * submits ReimActionEvent intents and reads these immutable snapshots.
 *
 * Units
 *   Item counts are whole items. Damage is the vanilla damage value for the
 *   item (0 = undamaged). Slot indices are interpreted inside the current
 *   screen_id namespace (M3_PLAN.md 7.2): the same slot number means different
 *   things in the inventory and a chest.
 *
 * Ownership
 *   Every array pointer inside a snapshot points into the Host-owned frame
 *   blob obtained by fetch_frame. It is valid until release_frame; the Host
 *   never reads it after that.
 */
#ifndef REIM_GAME_H
#define REIM_GAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- items -- */

/* item_id 0 is the empty stack. ids are assigned by the Runtime item table
 * and are stable for the lifetime of an instance; the Host resolves names
 * through the LANG/ITEM asset index, never by parsing generated data. */
typedef struct ReimItemStack {
  uint32_t struct_size;
  uint32_t item_id;
  uint32_t count;
  int32_t  damage;
  uint32_t flags;              /* REIM_ITEM_FLAG_* */
  uint32_t components_ref;     /* opaque Runtime reference; 0 = none.
                                * Unknown item components are preserved by the
                                * Runtime as original NBT; the Host must not
                                * reconstruct them. */
  uint32_t reserved[2];
} ReimItemStack;

#define REIM_ITEM_FLAG_HAS_DAMAGE    0x00000001u
#define REIM_ITEM_FLAG_UNBREAKABLE   0x00000002u
#define REIM_ITEM_FLAG_ENCHANTED     0x00000004u

#define REIM_ITEM_STACK_MIN_SIZE \
  ((uint32_t)(offsetof(ReimItemStack, reserved) + \
              sizeof(((ReimItemStack*)0)->reserved)))

/* A fixed-size inventory view carried inside the frame blob. slot_count is
 * the number of entries in slots; slots may be holes (empty items). */
typedef struct ReimInventorySlot {
  uint32_t slot;               /* screen_id-relative slot number */
  uint32_t reserved;
  ReimItemStack item;
} ReimInventorySlot;

typedef struct ReimInventorySnapshot {
  uint32_t struct_size;
  uint32_t screen_id;
  uint32_t slot_count;
  uint32_t selected_slot;      /* hotbar selection, 0..8 */
  uint32_t cursor_item_present;/* 0/1: the carried item stack is valid */
  ReimItemStack cursor_item;
  const ReimInventorySlot* slots; /* blob-owned; may be NULL when empty */
  uint32_t reserved[4];
} ReimInventorySnapshot;

#define REIM_INVENTORY_SNAPSHOT_MIN_SIZE \
  ((uint32_t)(offsetof(ReimInventorySnapshot, reserved) + \
              sizeof(((ReimInventorySnapshot*)0)->reserved)))

/* ------------------------------------------------------------- screens -- */

/* Screens are Runtime-owned state machines (M3_PLAN 8.10). The Host forwards
 * pointer/key intents; it never decides which slot was clicked. */
typedef enum ReimScreenId {
  REIM_SCREEN_NONE = 0,
  REIM_SCREEN_LOADING = 1,
  REIM_SCREEN_HUD = 2,          /* no modal screen; HUD only */
  REIM_SCREEN_INVENTORY = 3,
  REIM_SCREEN_CRAFTING_TABLE = 4,
  REIM_SCREEN_FURNACE = 5,
  REIM_SCREEN_CHEST = 6,
  REIM_SCREEN_PAUSE = 7,
  REIM_SCREEN_DEATH = 8
} ReimScreenId;

typedef enum ReimGamemode {
  REIM_GAMEMODE_SURVIVAL = 0,
  REIM_GAMEMODE_CREATIVE = 1,   /* debug configuration only in M3 */
  REIM_GAMEMODE_ADVENTURE = 2,
  REIM_GAMEMODE_SPECTATOR = 3
} ReimGamemode;

typedef enum ReimDifficulty {
  REIM_DIFFICULTY_PEACEFUL = 0,
  REIM_DIFFICULTY_EASY = 1,
  REIM_DIFFICULTY_NORMAL = 2,
  REIM_DIFFICULTY_HARD = 3
} ReimDifficulty;

/* --------------------------------------------------------- interaction -- */

typedef enum ReimInteractionType {
  REIM_INTERACTION_NONE = 0,
  REIM_INTERACTION_BLOCK = 1,
  REIM_INTERACTION_ENTITY = 2
} ReimInteractionType;

typedef enum ReimBlockFace {
  REIM_FACE_DOWN = 0,
  REIM_FACE_UP = 1,
  REIM_FACE_NORTH = 2,   /* -Z */
  REIM_FACE_SOUTH = 3,   /* +Z */
  REIM_FACE_WEST = 4,    /* -X */
  REIM_FACE_EAST = 5     /* +X */
} ReimBlockFace;

/* One immutable target snapshot per tick. positions are world blocks;
 * hit_* is the exact pick point in blocks (1 unit = 1 block). */
typedef struct ReimInteractionSnapshot {
  uint32_t struct_size;
  uint32_t target_type;        /* ReimInteractionType */
  int32_t  block_x;
  int32_t  block_y;
  int32_t  block_z;
  uint32_t block_state_id;     /* Runtime state id; 0 = air/unknown */
  uint32_t block_face;         /* ReimBlockFace */
  float    hit_x, hit_y, hit_z;
  uint64_t entity_id;          /* entity target only */
  float    reach_distance;     /* metres */
  float    break_progress;     /* 0..1 held-mining progress */
  uint32_t can_break;          /* 0/1 */
  uint32_t can_place;          /* 0/1 */
  float    attack_cooldown;    /* 0..1, 1 = ready */
  uint32_t reserved[4];
} ReimInteractionSnapshot;

#define REIM_INTERACTION_SNAPSHOT_MIN_SIZE \
  ((uint32_t)(offsetof(ReimInteractionSnapshot, reserved) + \
              sizeof(((ReimInteractionSnapshot*)0)->reserved)))

/* ------------------------------------------------------------- actions -- */

/* Intents submitted by the Host (M3-D04). Runtime validates and applies them
 * on the next 20 TPS tick. Every event carries a Host-monotonic sequence. */
typedef enum ReimActionType {
  REIM_ACTIONTYPE_NONE = 0,
  REIM_ACTIONTYPE_BREAK_BLOCK = 1,
  REIM_ACTIONTYPE_PLACE_BLOCK = 2,
  REIM_ACTIONTYPE_USE_BLOCK = 3,
  REIM_ACTIONTYPE_ATTACK = 4,
  REIM_ACTIONTYPE_USE_ITEM = 5,
  REIM_ACTIONTYPE_PICK_BLOCK = 6,
  REIM_ACTIONTYPE_SELECT_SLOT = 7,
  REIM_ACTIONTYPE_SLOT_CLICK = 8,   /* a=slot, b=button, c=mods */
  REIM_ACTIONTYPE_DROP_ITEM = 9,
  REIM_ACTIONTYPE_CLOSE_SCREEN = 10,
  REIM_ACTIONTYPE_RESPAWN = 11,
  REIM_ACTIONTYPE_POINTER = 12,     /* UI pointer pos/buttons/wheel */
  REIM_ACTIONTYPE_BREAK_START = 13, /* hold left; Runtime tracks progress */
  REIM_ACTIONTYPE_BREAK_STOP = 14,
  REIM_ACTIONTYPE_USE_START = 15,   /* hold right */
  REIM_ACTIONTYPE_USE_STOP = 16,
  REIM_ACTIONTYPE_DRAG_START = 17,
  REIM_ACTIONTYPE_DRAG_TO = 18,
  REIM_ACTIONTYPE_DRAG_END = 19,
  REIM_ACTIONTYPE_WHEEL = 20,
  REIM_ACTIONTYPE_DEBUG_SET_TIME = 64,
  REIM_ACTIONTYPE_DEBUG_SET_WEATHER = 65
} ReimActionType;

/* Pointer button bits used by REIM_ACTIONTYPE_POINTER.c. */
#define REIM_POINTER_BUTTON_LEFT   0x0001u
#define REIM_POINTER_BUTTON_RIGHT  0x0002u
#define REIM_POINTER_BUTTON_MIDDLE 0x0004u
/* Modifier bits used by SLOT_CLICK.c and POINTER.c. */
#define REIM_MOD_SHIFT             0x0100u
#define REIM_MOD_CONTROL           0x0200u
#define REIM_MOD_ALT               0x0400u

typedef struct ReimActionEvent {
  uint32_t struct_size;
  uint32_t type;             /* ReimActionType */
  uint64_t sequence;         /* Host monotonic; Runtime processes in order */
  uint32_t screen_id;        /* screen the intent was made in; 0 = none */
  int32_t  a, b;             /* slot/button/mods, per type */
  uint32_t c;                /* key bits / flags */
  float    x, y, z;          /* world position when the action targets one */
  float    screen_x, screen_y; /* UI pointer in frame pixels */
  float    wheel;            /* wheel delta */
  uint32_t reserved[4];
} ReimActionEvent;

#define REIM_ACTION_EVENT_MIN_SIZE \
  ((uint32_t)(offsetof(ReimActionEvent, reserved) + \
              sizeof(((ReimActionEvent*)0)->reserved)))

/* Bounded queue (M3_PLAN 7.2). A full queue rejects the event with a log line;
 * simulation never blocks. POINTER is coalesced to the newest state. */
#define REIM_MAX_ACTION_QUEUE 1024u

/* ------------------------------------------------------------ save API -- */

typedef enum ReimSaveState {
  REIM_SAVE_IDLE = 0,
  REIM_SAVE_SAVING = 1,
  REIM_SAVE_FAILED = 2
} ReimSaveState;

/* request_save flags. */
#define REIM_SAVE_FLAG_AUTOSAVE   0x00000001u
#define REIM_SAVE_FLAG_FLUSH      0x00000002u  /* stop() semantics: must finish */

typedef struct ReimSaveStatus {
  uint32_t struct_size;
  uint32_t state;            /* ReimSaveState */
  int32_t  last_result;      /* ReimResult of the last completed save */
  uint32_t pending_writes;   /* queued region/data files */
  uint64_t last_saved_tick;  /* simulation tick of the last successful save */
  uint64_t last_saved_wall_ms; /* Host-visible timestamp; never hashed */
  char     last_error[128];  /* UTF-8, NUL terminated; "" when none */
  uint32_t reserved[4];
} ReimSaveStatus;

#define REIM_SAVE_STATUS_MIN_SIZE \
  ((uint32_t)(offsetof(ReimSaveStatus, reserved) + \
              sizeof(((ReimSaveStatus*)0)->reserved)))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REIM_GAME_H */
