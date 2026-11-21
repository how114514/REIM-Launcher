#include "game/slot_layout.h"

#include "reim_game.h"

namespace reim::game {
namespace {

constexpr float kVirtualWidth = 640.0f;
constexpr float kVirtualHeight = 360.0f;
constexpr float kSlot = 20.0f;
constexpr float kMainPitch = 22.0f;

SlotRect make(float x, float y, float w = kSlot, float h = kSlot) {
  SlotRect rect;
  rect.valid = true;
  rect.x = x;
  rect.y = y;
  rect.w = w;
  rect.h = h;
  return rect;
}

/* Container screen geometry shared by the inventory/crafting/furnace/chest
 * panels. All values are logical pixels at ui_scale 1. */
struct Panel {
  float x;
  float y;
  float w;
  float h;
};

Panel panel_for(uint32_t screen) {
  switch (screen) {
    case REIM_SCREEN_INVENTORY: return {150.0f, 42.0f, 340.0f, 250.0f};
    case REIM_SCREEN_CRAFTING_TABLE: return {140.0f, 26.0f, 360.0f, 286.0f};
    case REIM_SCREEN_FURNACE: return {150.0f, 50.0f, 340.0f, 254.0f};
    case REIM_SCREEN_CHEST: return {140.0f, 18.0f, 360.0f, 326.0f};
    default: return {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

void main_inventory_origin(uint32_t screen, float& x, float& y) {
  switch (screen) {
    case REIM_SCREEN_INVENTORY: x = 176.0f; y = 142.0f; break;
    case REIM_SCREEN_CRAFTING_TABLE: x = 176.0f; y = 152.0f; break;
    case REIM_SCREEN_FURNACE: x = 176.0f; y = 176.0f; break;
    case REIM_SCREEN_CHEST: x = 176.0f; y = 154.0f; break;
    default: x = 0.0f; y = 0.0f; break;
  }
}

void hotbar_origin(uint32_t screen, float& x, float& y) {
  main_inventory_origin(screen, x, y);
  y += 3.0f * kMainPitch + 8.0f;
}

SlotRect main_slot(uint32_t screen, int index, bool hotbar) {
  float x = 0.0f;
  float y = 0.0f;
  if (hotbar) {
    hotbar_origin(screen, x, y);
    return make(x + static_cast<float>(index) * kMainPitch, y);
  }
  main_inventory_origin(screen, x, y);
  const int row = index / 9;
  const int column = index % 9;
  return make(x + static_cast<float>(column) * kMainPitch,
              y + static_cast<float>(row) * kMainPitch);
}

} // namespace

UiLayout ui_layout(uint32_t canvas_width, uint32_t canvas_height, uint32_t scale) {
  if (scale < 1) scale = 1;
  UiLayout layout;
  layout.logical_width = static_cast<float>(canvas_width) / static_cast<float>(scale);
  layout.logical_height = static_cast<float>(canvas_height) / static_cast<float>(scale);
  layout.origin_x = (layout.logical_width - kVirtualWidth) * 0.5f;
  layout.origin_y = (layout.logical_height - kVirtualHeight) * 0.5f;
  return layout;
}

float logical_x(float physical_x, const UiLayout& layout, uint32_t scale) {
  if (scale < 1) scale = 1;
  return physical_x / static_cast<float>(scale) - layout.origin_x;
}

float logical_y(float physical_y, const UiLayout& layout, uint32_t scale) {
  if (scale < 1) scale = 1;
  return physical_y / static_cast<float>(scale) - layout.origin_y;
}

void screen_slot_numbers(uint32_t screen, int* out, int capacity, int& count) {
  count = 0;
  auto push = [&](int slot) {
    if (count < capacity) out[count] = slot;
    ++count;
  };
  switch (screen) {
    case REIM_SCREEN_HUD:
      for (int i = 0; i < 9; ++i) push(i);
      break;
    case REIM_SCREEN_INVENTORY:
      push(0);
      for (int i = 1; i <= 4; ++i) push(i);
      for (int i = 9; i <= 35; ++i) push(i);
      for (int i = 36; i <= 44; ++i) push(i);
      break;
    case REIM_SCREEN_CRAFTING_TABLE:
      push(0);
      for (int i = 1; i <= 9; ++i) push(i);
      for (int i = 10; i <= 36; ++i) push(i);
      for (int i = 37; i <= 45; ++i) push(i);
      break;
    case REIM_SCREEN_FURNACE:
      push(0);
      push(1);
      push(2);
      for (int i = 3; i <= 29; ++i) push(i);
      for (int i = 30; i <= 38; ++i) push(i);
      break;
    case REIM_SCREEN_CHEST:
      for (int i = 0; i <= 26; ++i) push(i);
      for (int i = 27; i <= 53; ++i) push(i);
      for (int i = 54; i <= 62; ++i) push(i);
      break;
    default:
      break;
  }
}

SlotRect slot_rect(uint32_t screen, int slot) {
  if (slot < 0) return {};
  if (screen == REIM_SCREEN_HUD) {
    if (slot > 8) return {};
    return make(222.0f + static_cast<float>(slot) * kMainPitch, 326.0f);
  }
  switch (screen) {
    case REIM_SCREEN_INVENTORY:
      if (slot == 0) return make(250.0f, 96.0f);
      if (slot >= 1 && slot <= 4) {
        const int row = (slot - 1) / 2;
        const int column = (slot - 1) % 2;
        return make(176.0f + static_cast<float>(column) * 24.0f,
                    84.0f + static_cast<float>(row) * 24.0f);
      }
      if (slot >= 9 && slot <= 35) return main_slot(screen, slot - 9, false);
      if (slot >= 36 && slot <= 44) return main_slot(screen, slot - 36, true);
      return {};
    case REIM_SCREEN_CRAFTING_TABLE:
      if (slot == 0) return make(286.0f, 82.0f);
      if (slot >= 1 && slot <= 9) {
        const int index = slot - 1;
        const int row = index / 3;
        const int column = index % 3;
        return make(176.0f + static_cast<float>(column) * 24.0f,
                    64.0f + static_cast<float>(row) * 24.0f);
      }
      if (slot >= 10 && slot <= 36) return main_slot(screen, slot - 10, false);
      if (slot >= 37 && slot <= 45) return main_slot(screen, slot - 37, true);
      return {};
    case REIM_SCREEN_FURNACE:
      if (slot == 0) return make(180.0f, 92.0f);
      if (slot == 1) return make(180.0f, 142.0f);
      if (slot == 2) return make(280.0f, 108.0f);
      if (slot >= 3 && slot <= 29) return main_slot(screen, slot - 3, false);
      if (slot >= 30 && slot <= 38) return main_slot(screen, slot - 30, true);
      return {};
    case REIM_SCREEN_CHEST:
      if (slot >= 0 && slot <= 26) {
        const int row = slot / 9;
        const int column = slot % 9;
        return make(176.0f + static_cast<float>(column) * kMainPitch,
                    52.0f + static_cast<float>(row) * kMainPitch);
      }
      if (slot >= 27 && slot <= 53) return main_slot(screen, slot - 27, false);
      if (slot >= 54 && slot <= 62) return main_slot(screen, slot - 54, true);
      return {};
    default:
      return {};
  }
}

int slot_at(uint32_t screen, float x, float y) {
  int slots[64] = {};
  int count = 0;
  screen_slot_numbers(screen, slots, 64, count);
  for (int i = 0; i < count && i < 64; ++i) {
    const SlotRect rect = slot_rect(screen, slots[i]);
    if (!rect.valid) continue;
    if (x >= rect.x && x < rect.x + rect.w && y >= rect.y &&
        y < rect.y + rect.h) {
      return slots[i];
    }
  }
  return -1;
}

SlotRect close_button_rect() { return make(270.0f, 190.0f, 100.0f, 20.0f); }

UiPanel panel_rect(uint32_t screen) {
  const Panel panel = panel_for(screen);
  UiPanel out;
  out.visible = panel.w > 0.0f;
  out.x = panel.x;
  out.y = panel.y;
  out.w = panel.w;
  out.h = panel.h;
  return out;
}

} // namespace reim::game
