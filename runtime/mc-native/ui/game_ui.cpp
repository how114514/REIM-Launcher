#include "ui/game_ui.h"

#include "game/slot_layout.h"
#include "ui/font5x7.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace reim::ui {
namespace {

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
         (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

constexpr uint32_t kPanel = rgba(0x12, 0x16, 0x1E, 0xE8);
constexpr uint32_t kPanelEdge = rgba(0x80, 0x90, 0xA8, 0xFF);
constexpr uint32_t kText = rgba(0xE8, 0xF0, 0xFF, 0xFF);
constexpr uint32_t kTextDim = rgba(0x98, 0xA8, 0xC0, 0xFF);
constexpr uint32_t kSlotBg = rgba(0x28, 0x2C, 0x34, 0xFF);
constexpr uint32_t kSlotHot = rgba(0x70, 0x84, 0xA0, 0xFF);
constexpr uint32_t kSlotSelected = rgba(0xE8, 0xF0, 0xFF, 0xFF);
constexpr uint32_t kSlotBorder = rgba(0x10, 0x12, 0x16, 0xFF);
constexpr uint32_t kHeart = rgba(0xD0, 0x30, 0x30, 0xFF);
constexpr uint32_t kHeartBack = rgba(0x40, 0x18, 0x18, 0xFF);
constexpr uint32_t kHunger = rgba(0xB0, 0x70, 0x20, 0xFF);
constexpr uint32_t kHungerBack = rgba(0x38, 0x28, 0x18, 0xFF);
constexpr uint32_t kXpBack = rgba(0x10, 0x20, 0x10, 0xFF);
constexpr uint32_t kXp = rgba(0x40, 0xD0, 0x40, 0xFF);
constexpr uint32_t kArrow = rgba(0xC0, 0xC8, 0xD8, 0xFF);
constexpr uint32_t kOverlay = rgba(0x00, 0x00, 0x00, 0xA0);
constexpr uint32_t kButton = rgba(0x30, 0x50, 0x70, 0xFF);
constexpr uint32_t kButtonHot = rgba(0x50, 0x80, 0xB0, 0xFF);

const char* screen_title(uint32_t screen) {
  switch (screen) {
    case REIM_SCREEN_INVENTORY: return "INVENTORY";
    case REIM_SCREEN_CRAFTING_TABLE: return "CRAFTING";
    case REIM_SCREEN_FURNACE: return "FURNACE";
    case REIM_SCREEN_CHEST: return "CHEST";
    case REIM_SCREEN_PAUSE: return "PAUSED";
    case REIM_SCREEN_DEATH: return "YOU DIED";
    case REIM_SCREEN_LOADING: return "LOADING WORLD";
    default: return "";
  }
}

void add_rect(UiSpike::DrawList& out, float x, float y, float w, float h,
              uint32_t color) {
  ReimUiDraw draw{};
  draw.kind = REIM_UI_DRAW_RECT;
  draw.flags = REIM_UI_FLAG_BLEND;
  draw.x = x;
  draw.y = y;
  draw.w = w;
  draw.h = h;
  draw.color = color;
  draw.texture = REIM_UI_TEXTURE_SOLID;
  out.draws.push_back(draw);
}

void add_quad(UiSpike::DrawList& out, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1, uint32_t color,
              uint32_t texture, uint32_t flags) {
  ReimUiDraw draw{};
  draw.kind = REIM_UI_DRAW_TEXTURED_QUAD;
  draw.flags = flags;
  draw.x = x;
  draw.y = y;
  draw.w = w;
  draw.h = h;
  draw.u0 = u0;
  draw.v0 = v0;
  draw.u1 = u1;
  draw.v1 = v1;
  draw.color = color;
  draw.texture = texture;
  out.draws.push_back(draw);
}

struct Clip {
  bool valid = false;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

void add_text(UiSpike::DrawList& out, float x, float y, float scale,
              uint32_t color, std::string_view text, Clip clip = {}) {
  const float glyph_w = static_cast<float>(font5x7::kGlyphWidth) * scale;
  const float glyph_h = static_cast<float>(font5x7::kGlyphHeight) * scale;
  const float advance = static_cast<float>(font5x7::kCellWidth) * scale;
  float pen = x;
  for (char raw : text) {
    int code = static_cast<unsigned char>(raw);
    if (code < font5x7::kFirstChar || code > font5x7::kLastChar) code = '?';
    const int index = code - font5x7::kFirstChar;
    const int column = index % font5x7::kColumns;
    const int row = index / font5x7::kColumns;
    ReimUiDraw draw{};
    draw.kind = REIM_UI_DRAW_GLYPH_QUAD;
    draw.flags = REIM_UI_FLAG_BLEND | REIM_UI_FLAG_ALPHA_MASK;
    draw.x = pen;
    draw.y = y;
    draw.w = glyph_w;
    draw.h = glyph_h;
    draw.u0 = (static_cast<float>(column) * font5x7::kCellWidth) /
              static_cast<float>(font5x7::kAtlasWidth);
    draw.v0 = (static_cast<float>(row) * font5x7::kCellHeight) /
              static_cast<float>(font5x7::kAtlasHeight);
    draw.u1 = draw.u0 + static_cast<float>(font5x7::kGlyphWidth) /
                            static_cast<float>(font5x7::kAtlasWidth);
    draw.v1 = draw.v0 + static_cast<float>(font5x7::kGlyphHeight) /
                            static_cast<float>(font5x7::kAtlasHeight);
    draw.color = color;
    draw.texture = REIM_UI_TEXTURE_FONT_ATLAS;
    draw.text_offset = static_cast<uint32_t>(out.text.size());
    draw.text_length = 1;
    if (clip.valid) {
      draw.flags |= REIM_UI_FLAG_CLIP;
      draw.clip_x = clip.x;
      draw.clip_y = clip.y;
      draw.clip_w = clip.w;
      draw.clip_h = clip.h;
    }
    out.text.push_back(raw);
    out.draws.push_back(draw);
    pen += advance;
  }
}

uint32_t fallback_item_color(uint32_t item_id) {
  const uint32_t hash = item_id * 2654435761u;
  const uint8_t r = static_cast<uint8_t>(64 + (hash & 0x7F));
  const uint8_t g = static_cast<uint8_t>(64 + ((hash >> 8) & 0x7F));
  const uint8_t b = static_cast<uint8_t>(64 + ((hash >> 16) & 0x7F));
  return rgba(r, g, b, 0xFF);
}

void draw_item_icon(UiSpike::DrawList& out, const game::ItemStack& stack,
                    const game::ItemTable& items, const data::BlockTable& blocks,
                    float x, float y, float size) {
  if (stack.empty()) return;
  const game::ItemDef& def = items.def(stack.item_id);
  if (def.is_block() && def.block_id != 0) {
    const data::BlockDef& block = blocks.def(def.block_id);
    int face = 1; /* up */
    if (block.faces[face].u1 <= block.faces[face].u0) {
      face = 0;
      for (int i = 0; i < data::kFaceCount; ++i) {
        if (block.faces[i].u1 > block.faces[i].u0) {
          face = i;
          break;
        }
      }
    }
    const data::FaceTexture& uv = block.faces[face];
    if (uv.u1 > uv.u0) {
      add_quad(out, x, y, size, size, uv.u0, uv.v0, uv.u1, uv.v1, uv.tint,
               REIM_UI_TEXTURE_BLOCK_ATLAS,
               REIM_UI_FLAG_ALPHA_MASK | REIM_UI_FLAG_ITEM_ICON);
      return;
    }
  }
  /* The pre-rendered item atlas is a Converter deliverable (M3-K6); until it
   * lands non-block items use a stable color tile plus their initial. */
  add_rect(out, x, y, size, size, fallback_item_color(stack.item_id));
  const std::string display = def.display;
  if (!display.empty()) {
    add_text(out, x + size * 0.25f, y + size * 0.2f, size / 16.0f, kText,
             std::string_view(display.data(), 1));
  }
}

void draw_slot(UiSpike::DrawList& out, uint32_t slot, const game::ItemStack& stack,
               bool selected, bool hot, const game::ItemTable& items,
               const data::BlockTable& blocks) {
  const game::SlotRect rect = game::slot_rect(out.screen_id, static_cast<int>(slot));
  if (!rect.valid) return;
  add_rect(out, rect.x, rect.y, rect.w, rect.h, hot ? kSlotHot : kSlotBg);
  add_rect(out, rect.x, rect.y, rect.w, 1.0f, kSlotBorder);
  add_rect(out, rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f, kSlotBorder);
  add_rect(out, rect.x, rect.y, 1.0f, rect.h, kSlotBorder);
  add_rect(out, rect.x + rect.w - 1.0f, rect.y, 1.0f, rect.h, kSlotBorder);
  if (selected) {
    add_rect(out, rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, 1.0f,
             kSlotSelected);
    add_rect(out, rect.x - 1.0f, rect.y + rect.h, rect.w + 2.0f, 1.0f,
             kSlotSelected);
    add_rect(out, rect.x - 1.0f, rect.y, 1.0f, rect.h, kSlotSelected);
    add_rect(out, rect.x + rect.w, rect.y, 1.0f, rect.h, kSlotSelected);
  }
  if (stack.empty()) return;
  draw_item_icon(out, stack, items, blocks, rect.x + 2.0f, rect.y + 2.0f,
                 rect.w - 4.0f);
  if (stack.count > 1) {
    Clip clip;
    clip.valid = true;
    clip.x = rect.x;
    clip.y = rect.y;
    clip.w = rect.w;
    clip.h = rect.h;
    add_text(out, rect.x + 11.0f, rect.y + 12.0f, 0.8f, kText,
             std::to_string(stack.count), clip);
  }
}

} // namespace

void GameUi::build(const game::GameView& view, const game::ItemTable& items,
                   const data::BlockTable& blocks, UiSpike::DrawList& out) const {
  out.draws.clear();
  out.text.clear();
  out.slots.clear();
  out.screen_id = view.screen;
  out.revision = view.screen_revision;
  out.ui_scale = view.ui_scale;
  out.canvas_width = view.canvas_width;
  out.canvas_height = view.canvas_height;
  out.cursor_mode = view.pointer_visible ? REIM_UI_CURSOR_ARROW
                                         : REIM_UI_CURSOR_CROSS;
  out.cursor_x = view.pointer_x;
  out.cursor_y = view.pointer_y;
  out.hovered_element = view.hovered_element;
  out.hovered_slot = view.hovered_slot < 0
                         ? REIM_UI_SLOT_NONE
                         : static_cast<uint32_t>(view.hovered_slot);
  out.selected_slot = view.selected_slot;
  out.focused_element = view.hovered_element;
  out.cursor_item_present = view.cursor.empty() ? 0u : 1u;

  for (const game::UiSlot& slot : view.slots) {
    ReimInventorySlot entry{};
    entry.slot = slot.slot;
    game::to_contract(slot.item, entry.item);
    out.slots.push_back(entry);
  }

  if (view.screen == REIM_SCREEN_HUD) {
    /* Crosshair. */
    add_rect(out, 318.0f, 179.0f, 4.0f, 1.0f, kText);
    add_rect(out, 319.5f, 177.5f, 1.0f, 4.0f, kText);

    for (const game::UiSlot& slot : view.slots) {
      const bool selected = slot.slot == view.selected_slot;
      draw_slot(out, slot.slot, slot.item, selected,
                view.hovered_slot == static_cast<int>(slot.slot), items, blocks);
    }

    /* Health, hunger and XP are published by the player snapshot. Survival
     * damage is M3-F; the HUD draws whatever the Runtime owns. */
    const int hearts = static_cast<int>(std::ceil(view.health / 2.0f));
    for (int i = 0; i < 10; ++i) {
      const float x = 222.0f + static_cast<float>(i) * 10.0f;
      add_rect(out, x, 310.0f, 8.0f, 8.0f, kHeartBack);
      if (i < hearts) add_rect(out, x + 1.0f, 311.0f, 6.0f, 6.0f, kHeart);
    }
    const int food_icons = static_cast<int>(std::ceil(view.food / 2.0f));
    for (int i = 0; i < 10; ++i) {
      const float x = 424.0f + static_cast<float>(i) * 10.0f;
      add_rect(out, x, 310.0f, 8.0f, 8.0f, kHungerBack);
      if (i < food_icons) add_rect(out, x + 1.0f, 311.0f, 6.0f, 6.0f, kHunger);
    }
    add_rect(out, 222.0f, 322.0f, 196.0f, 3.0f, kXpBack);
    add_rect(out, 222.0f, 322.0f, 196.0f * std::clamp(view.xp_progress, 0.0f, 1.0f),
             3.0f, kXp);
    if (!view.held.empty()) {
      const std::string name = items.display_name(view.held.item_id);
      add_text(out, 250.0f, 298.0f, 1.0f, kText, name, Clip{true, 200.0f, 296.0f,
                                                            240.0f, 12.0f});
    }
    return;
  }

  if (view.screen == REIM_SCREEN_PAUSE) {
    add_rect(out, 0.0f, 0.0f, 640.0f, 360.0f, kOverlay);
    add_rect(out, 220.0f, 140.0f, 200.0f, 90.0f, kPanel);
    add_rect(out, 220.0f, 140.0f, 200.0f, 1.0f, kPanelEdge);
    add_rect(out, 220.0f, 229.0f, 200.0f, 1.0f, kPanelEdge);
    add_text(out, 276.0f, 156.0f, 2.0f, kText, "PAUSED");
    const game::SlotRect close = game::close_button_rect();
    add_rect(out, close.x, close.y, close.w, close.h,
             view.hovered_element == 1 ? kButtonHot : kButton);
    add_text(out, close.x + 10.0f, close.y + 6.0f, 1.0f, kText, "RESUME");
    return;
  }

  if (view.screen == REIM_SCREEN_LOADING) {
    add_rect(out, 0.0f, 0.0f, 640.0f, 360.0f, kOverlay);
    add_text(out, 250.0f, 170.0f, 2.0f, kText, "LOADING WORLD");
    return;
  }

  if (view.screen == REIM_SCREEN_DEATH) {
    add_rect(out, 0.0f, 0.0f, 640.0f, 360.0f, kOverlay);
    add_rect(out, 220.0f, 128.0f, 200.0f, 104.0f, kPanel);
    add_rect(out, 220.0f, 128.0f, 200.0f, 1.0f, kPanelEdge);
    add_rect(out, 220.0f, 231.0f, 200.0f, 1.0f, kPanelEdge);
    add_text(out, 270.0f, 146.0f, 2.0f, kHeart, "YOU DIED");
    if (!view.damage_cause.empty()) {
      add_text(out, 250.0f, 176.0f, 1.0f, kTextDim, view.damage_cause);
    }
    add_text(out, 246.0f, 198.0f, 1.0f, kText, "PRESS ENTER TO RESPAWN");
    return;
  }

  const game::UiPanel panel = game::panel_rect(view.screen);
  if (panel.visible) {
    add_rect(out, panel.x, panel.y, panel.w, panel.h, kPanel);
    add_rect(out, panel.x, panel.y, panel.w, 1.0f, kPanelEdge);
    add_rect(out, panel.x, panel.y + panel.h - 1.0f, panel.w, 1.0f, kPanelEdge);
    add_rect(out, panel.x, panel.y, 1.0f, panel.h, kPanelEdge);
    add_rect(out, panel.x + panel.w - 1.0f, panel.y, 1.0f, panel.h, kPanelEdge);
    add_text(out, panel.x + 12.0f, panel.y + 8.0f, 1.5f, kText,
             screen_title(view.screen));
  }

  for (const game::UiSlot& slot : view.slots) {
    const bool selected =
        (view.screen == REIM_SCREEN_INVENTORY &&
         slot.slot == 36u + view.selected_slot) ||
        (view.screen == REIM_SCREEN_CRAFTING_TABLE &&
         slot.slot == 37u + view.selected_slot) ||
        (view.screen == REIM_SCREEN_FURNACE &&
         slot.slot == 30u + view.selected_slot) ||
        (view.screen == REIM_SCREEN_CHEST &&
         slot.slot == 54u + view.selected_slot);
    draw_slot(out, slot.slot, slot.item, selected,
              view.hovered_slot == static_cast<int>(slot.slot), items, blocks);
  }

  if (view.screen == REIM_SCREEN_CRAFTING_TABLE) {
    add_text(out, 250.0f, 64.0f, 1.0f, kTextDim, "3X3");
  } else if (view.screen == REIM_SCREEN_INVENTORY) {
    add_text(out, 250.0f, 84.0f, 1.0f, kTextDim, "2X2");
  }

  if (view.screen == REIM_SCREEN_FURNACE && view.furnace.valid) {
    const float cook = std::clamp(
        static_cast<float>(view.furnace.cook_time) / 200.0f, 0.0f, 1.0f);
    add_rect(out, 224.0f, 112.0f, 52.0f, 4.0f, kSlotBorder);
    add_rect(out, 224.0f, 112.0f, 52.0f * cook, 4.0f, kArrow);
    const float burn = view.furnace.burn_total <= 0
                           ? 0.0f
                           : std::clamp(static_cast<float>(view.furnace.burn_time) /
                                            static_cast<float>(view.furnace.burn_total),
                                        0.0f, 1.0f);
    add_rect(out, 186.0f, 148.0f, 10.0f, 16.0f, kSlotBorder);
    add_rect(out, 187.0f, 163.0f - 14.0f * burn, 8.0f, 14.0f * burn,
             rgba(0xE0, 0x90, 0x20, 0xFF));
  }

  /* Tooltip, clipped to the panel so a long item name cannot escape it. */
  if (view.pointer_visible && !view.hovered_tooltip.empty()) {
    const float tx = std::min(view.pointer_x + 12.0f, 470.0f);
    const float ty = std::min(view.pointer_y + 8.0f, 320.0f);
    const float width = std::min(220.0f,
                                 static_cast<float>(view.hovered_tooltip.size()) *
                                         6.0f + 12.0f);
    add_rect(out, tx, ty, width, 16.0f, rgba(0x00, 0x00, 0x00, 0xE0));
    add_rect(out, tx, ty, width, 1.0f, kPanelEdge);
    Clip clip;
    clip.valid = true;
    clip.x = tx;
    clip.y = ty;
    clip.w = width;
    clip.h = 16.0f;
    add_text(out, tx + 6.0f, ty + 5.0f, 1.0f, kText, view.hovered_tooltip, clip);
  }

  if (view.pointer_visible && !view.cursor.empty()) {
    draw_item_icon(out, view.cursor, items, blocks, view.pointer_x - 8.0f,
                   view.pointer_y - 8.0f, 16.0f);
    if (view.cursor.count > 1) {
      add_text(out, view.pointer_x + 4.0f, view.pointer_y + 2.0f, 0.8f, kText,
               std::to_string(view.cursor.count));
    }
  }
}

} // namespace reim::ui
