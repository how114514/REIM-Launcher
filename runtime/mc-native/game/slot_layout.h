#pragma once

#include <cstdint>

namespace reim::game {

/* The Runtime's single UI layout. Both the draw-list builder and hit testing
 * use these functions, so Preview/UE cannot disagree about which slot a
 * pointer is over (M3-H6). Coordinates are logical pixels inside a 640x360
 * virtual screen centered in the Host canvas. */
struct SlotRect {
  bool valid = false;
  float x = 0.0f;
  float y = 0.0f;
  float w = 20.0f;
  float h = 20.0f;
};

struct UiLayout {
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float logical_width = 640.0f;
  float logical_height = 360.0f;
};

struct UiPanel {
  bool visible = false;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

UiPanel panel_rect(uint32_t screen);

UiLayout ui_layout(uint32_t canvas_width, uint32_t canvas_height, uint32_t scale);

/* Valid slot numbers for a screen, ascending. */
void screen_slot_numbers(uint32_t screen, int* out, int capacity, int& count);

SlotRect slot_rect(uint32_t screen, int slot);

/* Physical pointer -> logical position. */
float logical_x(float physical_x, const UiLayout& layout, uint32_t scale);
float logical_y(float physical_y, const UiLayout& layout, uint32_t scale);

/* -1 when the point is not over a slot of that screen. */
int slot_at(uint32_t screen, float logical_x, float logical_y);

/* Pause/close button rectangle (logical), used by the pause screen. */
SlotRect close_button_rect();

} // namespace reim::game
