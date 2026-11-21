#pragma once

#include "reim_ui.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reim::ui {

/*
 * A5 UI contract spike: one Runtime-laid-out screen that exercises every v3
 * draw kind (solid rect, textured quad, glyph quads), a clip rectangle, a
 * hover/click button, a 9-slot hotbar with Runtime hit testing and an empty
 * 36-slot inventory snapshot. The Host only draws the list; layout, hover and
 * slot selection stay here.
 *
 * Coordinates are logical pixels; the Host multiplies by ui_scale. Pointer
 * input arrives in physical pixels and is divided by the scale here. The
 * module is simulation-thread owned; fetch_frame copies the result into the
 * frame blob.
 */
class UiSpike {
 public:
  UiSpike();

  void set_canvas(uint32_t width, uint32_t height, uint32_t scale);
  void set_screen(uint32_t screen_id);
  uint32_t screen_id() const { return screen_id_; }

  /* Returns true when a UI element consumed the press (button/hotbar). */
  bool pointer(float screen_x, float screen_y, uint32_t buttons,
               uint32_t previous_buttons);
  bool slot_click(int32_t slot, int32_t button);
  void close_screen();

  bool button_on() const { return button_on_; }
  uint32_t hovered_element() const { return hovered_element_; }
  uint32_t hovered_slot() const { return hovered_slot_; }
  uint32_t selected_slot() const { return selected_slot_; }
  uint32_t revision() const { return revision_; }
  float pointer_x() const { return pointer_x_; }
  float pointer_y() const { return pointer_y_; }
  uint32_t canvas_width() const { return canvas_width_; }
  uint32_t canvas_height() const { return canvas_height_; }
  uint32_t ui_scale() const { return ui_scale_; }

  struct DrawList {
    std::vector<ReimUiDraw> draws;
    std::string text;
    std::vector<ReimInventorySlot> slots;
    uint32_t screen_id = REIM_SCREEN_HUD;
    uint32_t revision = 1;
    uint32_t ui_scale = 2;
    uint32_t canvas_width = 1280;
    uint32_t canvas_height = 720;
    uint32_t cursor_mode = REIM_UI_CURSOR_CROSS;
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    uint32_t hovered_element = 0;
    uint32_t hovered_slot = REIM_UI_SLOT_NONE;
    uint32_t focused_element = 0;
    uint32_t selected_slot = 0;
    uint32_t cursor_item_present = 0;
  };

  /* Layouts the current screen. */
  void build(DrawList& out) const;

  /* Synthesized assets, stable for the instance lifetime. */
  const std::vector<uint8_t>& font_png() const { return font_png_; }
  const std::string& font_meta() const { return font_meta_; }

 private:
  void add_text(DrawList& out, float x, float y, float scale, uint32_t color,
                const char* text) const;
  void add_rect(DrawList& out, float x, float y, float w, float h,
                uint32_t color) const;
  void add_quad(DrawList& out, float x, float y, float w, float h, float u0,
                float v0, float u1, float v1, uint32_t color, uint32_t texture,
                uint32_t flags) const;
  void build_font();

  uint32_t canvas_width_ = 1280;
  uint32_t canvas_height_ = 720;
  uint32_t ui_scale_ = 2;
  uint32_t screen_id_ = REIM_SCREEN_HUD;
  uint32_t revision_ = 1;
  uint32_t selected_slot_ = 0;
  uint32_t hovered_element_ = 0;
  uint32_t hovered_slot_ = REIM_UI_SLOT_NONE;
  float pointer_x_ = 0.0f;
  float pointer_y_ = 0.0f;
  bool button_on_ = false;

  std::vector<uint8_t> font_png_;
  std::string font_meta_;
};

} // namespace reim::ui
