#include "ui/ui_spike.h"

#include "ui/font5x7.h"
#include "util/png.h"

#include <algorithm>
#include <cmath>

#include "json.hpp"

namespace reim::ui {
namespace {

constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
         (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

constexpr uint32_t kPanel = rgba(0x10, 0x18, 0x28, 0xD8);
constexpr uint32_t kPanelEdge = rgba(0x60, 0x90, 0xC0, 0xFF);
constexpr uint32_t kText = rgba(0xE8, 0xF0, 0xFF, 0xFF);
constexpr uint32_t kTextDim = rgba(0x98, 0xB0, 0xD0, 0xFF);
constexpr uint32_t kButton = rgba(0x30, 0x60, 0x90, 0xFF);
constexpr uint32_t kButtonHot = rgba(0x50, 0x90, 0xD0, 0xFF);
constexpr uint32_t kButtonOn = rgba(0x40, 0xC0, 0x70, 0xFF);
constexpr uint32_t kSlot = rgba(0x20, 0x28, 0x38, 0xFF);
constexpr uint32_t kSlotHot = rgba(0x80, 0x90, 0xA8, 0xFF);
constexpr uint32_t kSlotSelected = rgba(0xE0, 0xE8, 0xFF, 0xFF);

/* Element ids for hover/focus diagnostics (Runtime-owned, not Host widgets). */
constexpr uint32_t kElementButton = 1;
constexpr uint32_t kElementHotbar0 = 100;

/* Logical layout at ui_scale 1. The canvas default is 1280x720 at scale 2,
 * i.e. 640x360 logical pixels. Elements are clamped to the canvas height. */
constexpr float kPanelX = 16.0f;
constexpr float kPanelY = 16.0f;
constexpr float kPanelW = 304.0f;
constexpr float kPanelH = 168.0f;
constexpr float kButtonX = 28.0f;
constexpr float kButtonY = 56.0f;
constexpr float kButtonW = 164.0f;
constexpr float kButtonH = 24.0f;
constexpr float kHotbarX = 16.0f;
constexpr float kHotbarY = 152.0f;
constexpr float kSlotSize = 18.0f;
constexpr float kSlotPitch = 20.0f;
constexpr int kHotbarSlots = 9;
constexpr int kInventorySlots = 36;

bool inside(float x, float y, float rx, float ry, float rw, float rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

} // namespace

UiSpike::UiSpike() { build_font(); }

void UiSpike::set_canvas(uint32_t width, uint32_t height, uint32_t scale) {
  if (width < 64) width = 64;
  if (height < 64) height = 64;
  if (scale < 1) scale = 1;
  if (scale > 8) scale = 8;
  if (width == canvas_width_ && height == canvas_height_ && scale == ui_scale_) {
    return;
  }
  canvas_width_ = width;
  canvas_height_ = height;
  ui_scale_ = scale;
  ++revision_;
}

void UiSpike::set_screen(uint32_t screen_id) {
  if (screen_id == screen_id_) return;
  screen_id_ = screen_id;
  hovered_element_ = 0;
  hovered_slot_ = REIM_UI_SLOT_NONE;
  ++revision_;
}

bool UiSpike::pointer(float screen_x, float screen_y, uint32_t buttons,
                      uint32_t previous_buttons) {
  pointer_x_ = screen_x / static_cast<float>(ui_scale_);
  pointer_y_ = screen_y / static_cast<float>(ui_scale_);
  hovered_element_ = 0;
  hovered_slot_ = REIM_UI_SLOT_NONE;
  if (screen_id_ != REIM_SCREEN_HUD) {
    return false;
  }
  if (inside(pointer_x_, pointer_y_, kButtonX, kButtonY, kButtonW, kButtonH)) {
    hovered_element_ = kElementButton;
  } else if (pointer_y_ >= kHotbarY && pointer_y_ < kHotbarY + kSlotSize) {
    for (int i = 0; i < kHotbarSlots; ++i) {
      const float x = kHotbarX + static_cast<float>(i) * kSlotPitch;
      if (inside(pointer_x_, pointer_y_, x, kHotbarY, kSlotSize, kSlotSize)) {
        hovered_element_ = kElementHotbar0 + static_cast<uint32_t>(i);
        hovered_slot_ = static_cast<uint32_t>(i);
        break;
      }
    }
  }

  const bool pressed =
      (buttons & REIM_POINTER_BUTTON_LEFT) != 0 &&
      (previous_buttons & REIM_POINTER_BUTTON_LEFT) == 0;
  if (!pressed) return false;
  if (hovered_element_ == kElementButton) {
    button_on_ = !button_on_;
    ++revision_;
    return true;
  }
  if (hovered_slot_ != REIM_UI_SLOT_NONE) {
    selected_slot_ = hovered_slot_;
    ++revision_;
    return true;
  }
  return false;
}

bool UiSpike::slot_click(int32_t slot, int32_t button) {
  if (slot >= 0 && slot < kHotbarSlots && button == 0) {
    selected_slot_ = static_cast<uint32_t>(slot);
    ++revision_;
    return true;
  }
  return false;
}

void UiSpike::close_screen() { set_screen(REIM_SCREEN_HUD); }

void UiSpike::add_text(DrawList& out, float x, float y, float scale,
                       uint32_t color, const char* text) const {
  const float glyph_w = static_cast<float>(font5x7::kGlyphWidth) * scale;
  const float glyph_h = static_cast<float>(font5x7::kGlyphHeight) * scale;
  const float advance = static_cast<float>(font5x7::kCellWidth) * scale;
  float pen = x;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    int code = static_cast<unsigned char>(*cursor);
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
    out.text.push_back(*cursor == '\0' ? '?' : *cursor);
    out.draws.push_back(draw);
    pen += advance;
  }
}

void UiSpike::add_rect(DrawList& out, float x, float y, float w, float h,
                       uint32_t color) const {
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

void UiSpike::add_quad(DrawList& out, float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       uint32_t color, uint32_t texture, uint32_t flags) const {
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

void UiSpike::build(DrawList& out) const {
  out.draws.clear();
  out.text.clear();
  out.slots.clear();
  out.screen_id = screen_id_;
  out.revision = revision_;
  out.ui_scale = ui_scale_;
  out.canvas_width = canvas_width_;
  out.canvas_height = canvas_height_;
  out.cursor_mode = REIM_UI_CURSOR_CROSS;
  out.cursor_x = pointer_x_;
  out.cursor_y = pointer_y_;
  out.hovered_element = hovered_element_;
  out.hovered_slot = hovered_slot_;
  out.selected_slot = selected_slot_;
  out.focused_element = hovered_element_;

  /* The inventory snapshot exists even before items are implemented; it proves
   * the slot array is carried in the frame and never synthesized by the Host. */
  out.slots.reserve(kInventorySlots);
  for (int i = 0; i < kInventorySlots; ++i) {
    ReimInventorySlot slot{};
    slot.slot = static_cast<uint32_t>(i);
    slot.item.struct_size = sizeof(ReimItemStack);
    slot.item.item_id = 0;
    out.slots.push_back(slot);
  }

  if (screen_id_ != REIM_SCREEN_HUD) {
    /* Non-HUD screens are still display-list owned here; this spike only
     * exercises the HUD. An empty list is a valid screen. */
    return;
  }

  add_rect(out, kPanelX, kPanelY, kPanelW, kPanelH, kPanel);
  /* 1px logical border drawn as four rects (no line primitive in v3). */
  add_rect(out, kPanelX, kPanelY, kPanelW, 1.0f, kPanelEdge);
  add_rect(out, kPanelX, kPanelY + kPanelH - 1.0f, kPanelW, 1.0f, kPanelEdge);
  add_rect(out, kPanelX, kPanelY, 1.0f, kPanelH, kPanelEdge);
  add_rect(out, kPanelX + kPanelW - 1.0f, kPanelY, 1.0f, kPanelH, kPanelEdge);

  add_text(out, 28.0f, 28.0f, 2.0f, kText, "REIM UI SPIKE");

  const bool hot = hovered_element_ == kElementButton;
  add_rect(out, kButtonX, kButtonY, kButtonW, kButtonH,
           button_on_ ? kButtonOn : (hot ? kButtonHot : kButton));
  add_text(out, 36.0f, 64.0f, 1.0f, kText, "CLICK ME");
  add_text(out, 28.0f, 88.0f, 1.0f, kTextDim,
           button_on_ ? "STATE ON" : "STATE OFF");

  /* First block-atlas tile: 16 px of a 2048x128 atlas. */
  const float u1 = 16.0f / 2048.0f;
  const float v1 = 16.0f / 128.0f;
  add_quad(out, 28.0f, 108.0f, 32.0f, 32.0f, 0.0f, 0.0f, u1, v1, kText,
           REIM_UI_TEXTURE_BLOCK_ATLAS, REIM_UI_FLAG_ALPHA_MASK);
  /* A5 uses a block-atlas quad as the item-icon stand-in; the pre-rendered
   * isometric item atlas is a Converter deliverable (M3-K6 / phase 4). */
  add_quad(out, 68.0f, 108.0f, 32.0f, 32.0f, 0.0f, 0.0f, u1, v1, kText,
           REIM_UI_TEXTURE_BLOCK_ATLAS,
           REIM_UI_FLAG_ALPHA_MASK | REIM_UI_FLAG_ITEM_ICON);

  /* Clipped text block: the third line is cut by the clip rectangle. */
  const float clip_x = 112.0f;
  const float clip_y = 106.0f;
  const float clip_w = 196.0f;
  const float clip_h = 34.0f;
  const size_t first_clipped_draw = out.draws.size();
  add_text(out, 116.0f, 108.0f, 1.0f, kText, "CLIP LINE 1");
  add_text(out, 116.0f, 120.0f, 1.0f, kText, "CLIP LINE 2");
  add_text(out, 116.0f, 132.0f, 1.0f, kText, "CLIP LINE 3 IS CUT");
  for (size_t i = first_clipped_draw; i < out.draws.size(); ++i) {
    out.draws[i].flags |= REIM_UI_FLAG_CLIP;
    out.draws[i].clip_x = clip_x;
    out.draws[i].clip_y = clip_y;
    out.draws[i].clip_w = clip_w;
    out.draws[i].clip_h = clip_h;
  }

  /* Hotbar: Runtime decides which slot is selected and hovered. */
  for (int i = 0; i < kHotbarSlots; ++i) {
    const float x = kHotbarX + static_cast<float>(i) * kSlotPitch;
    const bool selected = static_cast<uint32_t>(i) == selected_slot_;
    const bool slot_hot = hovered_slot_ == static_cast<uint32_t>(i);
    add_rect(out, x, kHotbarY, kSlotSize, kSlotSize,
             selected ? kSlotSelected : (slot_hot ? kSlotHot : kSlot));
    if (!selected) {
      add_rect(out, x + 1.0f, kHotbarY + 1.0f, kSlotSize - 2.0f,
               kSlotSize - 2.0f, kSlot);
    }
  }

  add_text(out, 16.0f, 192.0f, 1.0f, kTextDim, "M3 A5 UI CONTRACT");
}

void UiSpike::build_font() {
  std::vector<uint8_t> atlas(
      static_cast<size_t>(font5x7::kAtlasWidth) * font5x7::kAtlasHeight * 4, 0);
  auto put = [&](int x, int y, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= font5x7::kAtlasWidth ||
        y >= font5x7::kAtlasHeight) {
      return;
    }
    const size_t offset =
        (static_cast<size_t>(y) * font5x7::kAtlasWidth + x) * 4;
    atlas[offset + 0] = 255;
    atlas[offset + 1] = 255;
    atlas[offset + 2] = 255;
    atlas[offset + 3] = alpha;
  };
  for (int index = 0; index < 95; ++index) {
    const int cell_x = (index % font5x7::kColumns) * font5x7::kCellWidth;
    const int cell_y = (index / font5x7::kColumns) * font5x7::kCellHeight;
    for (int column = 0; column < font5x7::kGlyphWidth; ++column) {
      const unsigned char bits = font5x7::kGlyphs[index][column];
      for (int row = 0; row < font5x7::kGlyphHeight; ++row) {
        if ((bits >> row) & 1u) put(cell_x + column, cell_y + row, 255);
      }
    }
  }
  const int white_x = (font5x7::kWhiteCell % font5x7::kColumns) * font5x7::kCellWidth;
  const int white_y = (font5x7::kWhiteCell / font5x7::kColumns) * font5x7::kCellHeight;
  for (int y = 0; y < font5x7::kGlyphHeight; ++y) {
    for (int x = 0; x < font5x7::kGlyphWidth; ++x) put(white_x + x, white_y + y, 255);
  }
  std::string error;
  if (!png::encode_memory(font5x7::kAtlasWidth, font5x7::kAtlasHeight, 4,
                          atlas.data(), font_png_, error)) {
    font_png_.clear();
  }
  nlohmann::json meta;
  meta["format"] = 1;
  meta["width"] = font5x7::kAtlasWidth;
  meta["height"] = font5x7::kAtlasHeight;
  meta["cell_width"] = font5x7::kCellWidth;
  meta["cell_height"] = font5x7::kCellHeight;
  meta["columns"] = font5x7::kColumns;
  meta["rows"] = font5x7::kRows;
  meta["first_char"] = font5x7::kFirstChar;
  meta["last_char"] = font5x7::kLastChar;
  meta["glyph_width"] = font5x7::kGlyphWidth;
  meta["glyph_height"] = font5x7::kGlyphHeight;
  meta["ascent"] = font5x7::kGlyphHeight;
  meta["descent"] = 1;
  meta["baseline"] = font5x7::kGlyphHeight;
  meta["white_cell"] = font5x7::kWhiteCell;
  meta["sampling"] = "nearest";
  meta["channels"] = 4;
  font_meta_ = meta.dump();
}

} // namespace reim::ui
