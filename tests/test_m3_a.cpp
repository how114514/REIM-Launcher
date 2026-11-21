/*
 * M3-A: Contract v3 integration tests (action channel, UI display list,
 * entity/audio channels). They need a real 26.3 world and skip without
 * REIM_TEST_WORLD, matching the M1/M2 integration convention.
 *
 * These tests assert only what the Runtime actually publishes. The "out of
 * reach" validation of M3-A4 is owned by M3-D1 (raycast), which does not exist
 * in phase 1, so it is not claimed here.
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "reim_abi.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

#ifndef REIM_TEST_GENERATED_DIR
#define REIM_TEST_GENERATED_DIR "generated/26.3"
#endif

namespace {

using Json = nlohmann::json;

void* REIM_CALL align_alloc(size_t size, size_t align) {
  if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
#if defined(_MSC_VER)
  return _aligned_malloc(size, align);
#else
  return std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
}

void REIM_CALL align_free(void* ptr) {
  if (ptr == nullptr) return;
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void REIM_CALL silent_log(int32_t, const char*, const char*) {}

std::filesystem::path generated_dir() {
  const char* env = std::getenv("REIM_TEST_GENERATED");
  if (env != nullptr && *env != '\0') return std::filesystem::path(env);
  return std::filesystem::path(REIM_TEST_GENERATED_DIR);
}

bool world_available(std::filesystem::path& world) {
  const char* env = std::getenv("REIM_TEST_WORLD");
  if (env == nullptr || *env == '\0') return false;
  world = std::filesystem::path(env);
  return std::filesystem::is_directory(world);
}

struct Fixture {
  const ReimRuntimeApi* api = nullptr;
  ReimInstance* instance = nullptr;
  ReimHostApi host{};
  uint64_t sequence = 0;

  bool create(const std::filesystem::path& world, uint32_t flags,
              uint32_t ui_scale = 2) {
    api = reim_get_api(REIM_ABI_VERSION);
    if (api == nullptr) return false;
    host = ReimHostApi{};
    host.struct_size = sizeof(host);
    host.abi_version = REIM_ABI_VERSION;
    host.alloc = &align_alloc;
    host.free = &align_free;
    host.log = &silent_log;

    const std::wstring world_path = world.wstring();
    const std::wstring generated = generated_dir().wstring();
    ReimInstanceConfig config{};
    config.struct_size = sizeof(config);
    config.abi_version = REIM_ABI_VERSION;
    config.world_path = world_path.c_str();
    config.generated_path = generated.c_str();
    config.render_distance = 2;
    config.mouse_sensitivity = 0.15f;
    config.flags = flags;
    config.ui_scale = ui_scale;
    int32_t error = 0;
    instance = api->create(&config, &host, &error);
    return instance != nullptr;
  }

  ~Fixture() {
    if (api != nullptr && instance != nullptr) api->destroy(instance);
  }

  int32_t fetch(ReimRenderFrame& frame) {
    frame = ReimRenderFrame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    return api->fetch_frame(instance, &frame);
  }

  Json status() {
    const int32_t needed = api->get_status_json(instance, nullptr, 0);
    if (needed <= 0) return Json::object();
    std::vector<char> buffer(static_cast<size_t>(needed) + 1, '\0');
    const int32_t written =
        api->get_status_json(instance, buffer.data(),
                             static_cast<uint32_t>(buffer.size()));
    if (written <= 0) return Json::object();
    return Json::parse(buffer.data(), buffer.data() + written, nullptr, false);
  }

  bool wait_ready(int timeout_ms = 15000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      ReimRenderFrame frame;
      if (fetch(frame) != REIM_OK) break;
      const bool ready = frame.ready != 0;
      api->release_frame(instance, frame.frame_id);
      if (ready) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  void action(uint32_t type, uint32_t screen, int32_t a = 0, int32_t b = 0,
              uint32_t c = 0, float screen_x = 0.0f, float screen_y = 0.0f,
              float wheel = 0.0f, uint64_t forced_sequence = 0) {
    ReimActionEvent event{};
    event.struct_size = sizeof(event);
    event.type = type;
    event.sequence = forced_sequence != 0 ? forced_sequence : ++sequence;
    event.screen_id = screen;
    event.a = a;
    event.b = b;
    event.c = c;
    event.screen_x = screen_x;
    event.screen_y = screen_y;
    event.wheel = wheel;
    api->submit_action(instance, &event);
  }

  void pause(bool paused) {
    api->set_simulation_paused(instance, paused ? uint8_t{1} : uint8_t{0});
  }
};

void render_distance_frames(Fixture& f, int milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline) {
    ReimRenderFrame frame;
    if (f.fetch(frame) == REIM_OK) f.api->release_frame(f.instance, frame.frame_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

int64_t get_int(const Json& value, const char* group, const char* key,
                int64_t fallback = -1) {
  if (!value.is_object() || !value.contains(group)) return fallback;
  const Json& node = value[group];
  if (!node.is_object() || !node.contains(key)) return fallback;
  if (!node[key].is_number_integer()) return fallback;
  return node[key].get<int64_t>();
}

} // namespace

TEST_CASE("M3-A4 action queue validates, applies and enforces hold pairing") {
  std::filesystem::path world;
  if (!world_available(world)) {
    WARN("REIM_TEST_WORLD not set; skipping M3-A4 action integration test");
    return;
  }
  Fixture f;
  REQUIRE(f.create(world, 0));
  REQUIRE(f.api->start(f.instance) == REIM_OK);
  REQUIRE(f.wait_ready());

  const Json base = f.status();
  REQUIRE(base.contains("actions"));
  REQUIRE(get_int(base, "actions", "rejected") == 0);

  /* Unknown type and a screen mismatch are rejected before applying. */
  f.action(999u, 0, 0, 0, 0, 0.0f, 0.0f);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(get_int(f.status(), "actions", "rejected") >= 1);

  f.action(REIM_ACTIONTYPE_SLOT_CLICK, 99, 2, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(get_int(f.status(), "actions", "rejected") >= 2);

  /* A valid intent applies on a later tick. */
  const uint64_t applied_before = get_int(f.status(), "actions", "applied");
  f.action(REIM_ACTIONTYPE_SELECT_SLOT, REIM_SCREEN_HUD, 3);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "actions", "applied") > applied_before);
  CHECK(get_int(f.status(), "ui", "selected_slot") == 3);

  /* Duplicate sequence numbers are rejected. */
  const int64_t rejected_after_valid = get_int(f.status(), "actions", "rejected");
  f.action(REIM_ACTIONTYPE_SELECT_SLOT, REIM_SCREEN_HUD, 4, 0, 0, 0, 0, 0,
           f.sequence);  // same sequence as the last accepted event
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(get_int(f.status(), "actions", "rejected") > rejected_after_valid);

  /* STOP without START is rejected; a repeated START coalesces. */
  f.action(REIM_ACTIONTYPE_BREAK_STOP, REIM_SCREEN_HUD);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  CHECK(get_int(f.status(), "actions", "rejected") > rejected_after_valid + 1);
  const uint64_t coalesced_before = get_int(f.status(), "actions", "coalesced");
  f.action(REIM_ACTIONTYPE_BREAK_START, REIM_SCREEN_HUD);
  f.action(REIM_ACTIONTYPE_BREAK_START, REIM_SCREEN_HUD);
  f.action(REIM_ACTIONTYPE_BREAK_STOP, REIM_SCREEN_HUD);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "actions", "coalesced") > coalesced_before);

  /* While paused the tick loop does not drain the queue, so the bounded queue
   * rejects excess intents instead of blocking. */
  f.pause(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  const int64_t rejected_paused = get_int(f.status(), "actions", "rejected");
  for (int i = 0; i < REIM_MAX_ACTION_QUEUE + 64; ++i) {
    f.action(REIM_ACTIONTYPE_SELECT_SLOT, REIM_SCREEN_HUD, i % 9);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(get_int(f.status(), "actions", "rejected") - rejected_paused >= 64);
  f.pause(false);
  render_distance_frames(f, 100);
}

TEST_CASE("M3-A5 UI display list, clip, glyphs and Runtime hit testing") {
  std::filesystem::path world;
  if (!world_available(world)) {
    WARN("REIM_TEST_WORLD not set; skipping M3-A5 UI integration test");
    return;
  }
  Fixture f;
  REQUIRE(f.create(world, REIM_CONFIG_FLAG_UI_SPIKE, 2));
  f.api->set_ui_canvas(f.instance, 1280, 720, 2.0f);
  REQUIRE(f.api->start(f.instance) == REIM_OK);
  REQUIRE(f.wait_ready());

  /* Copy the display list before release frees the frame blob. */
  std::vector<ReimUiDraw> draws;
  std::string text;
  uint32_t draw_count = 0;
  uint32_t slot_count = 0;
  uint32_t screen = 0;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline && draw_count == 0) {
    ReimRenderFrame frame;
    REQUIRE(f.fetch(frame) == REIM_OK);
    if (frame.ui.draw_count != 0) {
      draws.assign(frame.ui.draws, frame.ui.draws + frame.ui.draw_count);
      text.assign(frame.ui.text, frame.ui.text + frame.ui.text_bytes);
      draw_count = frame.ui.draw_count;
      slot_count = frame.ui.inventory.slot_count;
      screen = frame.ui.screen_id;
    }
    f.api->release_frame(f.instance, frame.frame_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(draw_count > 0);
  CHECK(screen == REIM_SCREEN_HUD);
  CHECK(slot_count == 36);
  CHECK(text.find("REIM UI SPIKE") != std::string::npos);

  bool has_rect = false, has_textured = false, has_glyph = false;
  bool has_clip = false, has_item_icon = false;
  for (const ReimUiDraw& draw : draws) {
    if (draw.kind == REIM_UI_DRAW_RECT) has_rect = true;
    if (draw.kind == REIM_UI_DRAW_TEXTURED_QUAD) has_textured = true;
    if (draw.kind == REIM_UI_DRAW_GLYPH_QUAD) has_glyph = true;
    if ((draw.flags & REIM_UI_FLAG_CLIP) != 0) has_clip = true;
    if ((draw.flags & REIM_UI_FLAG_ITEM_ICON) != 0) has_item_icon = true;
    CHECK(draw.w >= 0.0f);
    CHECK(draw.h >= 0.0f);
  }
  CHECK(has_rect);
  CHECK(has_textured);
  CHECK(has_glyph);
  CHECK(has_clip);
  CHECK(has_item_icon);

  /* Pointer in physical pixels (logical button: 28..192 x 56..80). */
  f.action(REIM_ACTIONTYPE_POINTER, 0, 0, 0, 0, 60.0f, 120.0f);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "ui", "hovered_element") == 1);
  const int64_t revision_before = get_int(f.status(), "ui", "revision");
  f.action(REIM_ACTIONTYPE_POINTER, 0, 0, 0, REIM_POINTER_BUTTON_LEFT, 60.0f,
           120.0f);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "ui", "button_on") == 1);
  CHECK(get_int(f.status(), "ui", "revision") > revision_before);
  /* Release, let a tick observe it, then press again to toggle back. */
  f.action(REIM_ACTIONTYPE_POINTER, 0, 0, 0, 0, 60.0f, 120.0f);
  render_distance_frames(f, 120);
  f.action(REIM_ACTIONTYPE_POINTER, 0, 0, 0, REIM_POINTER_BUTTON_LEFT, 60.0f,
           120.0f);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "ui", "button_on") == 0);

  /* Hotbar hit testing: slot 4 is at logical x 96..114, y 152..170. */
  f.action(REIM_ACTIONTYPE_POINTER, 0, 0, 0, 0, 202.0f, 320.0f);
  render_distance_frames(f, 150);
  CHECK(get_int(f.status(), "ui", "hovered_slot") == 4);

  f.action(REIM_ACTIONTYPE_SLOT_CLICK, REIM_SCREEN_HUD, 6, 0);
  render_distance_frames(f, 120);
  CHECK(get_int(f.status(), "ui", "selected_slot") == 6);
  f.action(REIM_ACTIONTYPE_WHEEL, REIM_SCREEN_HUD, 0, 0, 0, 0.0f, 0.0f, 1.0f);
  render_distance_frames(f, 120);
  CHECK(get_int(f.status(), "ui", "selected_slot") == 7);
  f.action(REIM_ACTIONTYPE_WHEEL, REIM_SCREEN_HUD, 0, 0, 0, 0.0f, 0.0f, -1.0f);
  render_distance_frames(f, 120);
  CHECK(get_int(f.status(), "ui", "selected_slot") == 6);
}

TEST_CASE("M3-A6 entity/particle/audio channels and synthesized assets") {
  std::filesystem::path world;
  if (!world_available(world)) {
    WARN("REIM_TEST_WORLD not set; skipping M3-A6 channel integration test");
    return;
  }
  Fixture f;
  REQUIRE(f.create(world, REIM_CONFIG_FLAG_CHANNEL_SPIKE, 2));
  REQUIRE(f.api->start(f.instance) == REIM_OK);

  bool ready = false;
  bool saw_entity = false;
  bool saw_audio = false;
  bool audio_2d = false;
  bool parts_present = false;
  float first_yaw = 0.0f;
  bool yaw_changed = false;
  uint32_t audio_sound = 0;
  uint32_t audio_category = 0;

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < deadline && !ready) {
    ReimRenderFrame frame;
    REQUIRE(f.fetch(frame) == REIM_OK);
    if (frame.ready) ready = true;
    for (uint32_t i = 0; i < frame.audio_count; ++i) {
      const ReimAudioEvent& event = frame.audio_events[i];
      if (event.sound_id == 1 && !saw_audio) {
        saw_audio = true;
        audio_2d = (event.flags & REIM_AUDIO_FLAG_2D) != 0;
        audio_sound = event.sound_id;
        audio_category = event.category;
      }
    }
    if (frame.entity_count != 0) {
      const ReimEntityRender& entity = frame.entities[0];
      if (!saw_entity) {
        saw_entity = true;
        first_yaw = entity.yaw;
        CHECK(entity.model_id == 0);
        CHECK(entity.part_count == 1);
        parts_present = entity.parts != nullptr && entity.part_count == 1;
        if (parts_present) {
          CHECK(entity.parts[0].pivot[1] == Approx(0.5f));
          CHECK(entity.parts[0].scale[0] == Approx(1.0f));
        }
      } else if (std::fabs(entity.yaw - first_yaw) > 0.01f) {
        yaw_changed = true;
      }
    }
    f.api->release_frame(f.instance, frame.frame_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(ready);

  /* Keep observing after ready: the Runtime animates the box while the Host
   * only interpolates, so a changing yaw proves animation is Runtime-owned. */
  const auto yaw_deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(400);
  while (std::chrono::steady_clock::now() < yaw_deadline) {
    ReimRenderFrame frame;
    if (f.fetch(frame) == REIM_OK) {
      if (frame.entity_count != 0 && saw_entity &&
          std::fabs(frame.entities[0].yaw - first_yaw) > 0.01f) {
        yaw_changed = true;
      }
      f.api->release_frame(f.instance, frame.frame_id);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  CHECK(saw_entity);
  CHECK(saw_audio);
  CHECK(audio_sound == 1);
  CHECK(audio_2d);
  CHECK(audio_category == REIM_AUDIO_CATEGORY_UI);
  CHECK(parts_present);
  CHECK(yaw_changed);

  const void* data = nullptr;
  uint64_t size = 0;
  uint64_t revision = 0;
  REQUIRE(f.api->get_asset(f.instance, REIM_ASSET_FONT_ATLAS, 0, &data, &size,
                           &revision) == REIM_OK);
  REQUIRE(size > 8);
  const auto* png = static_cast<const uint8_t*>(data);
  CHECK(png[0] == 0x89);
  CHECK(png[1] == 'P');

  REQUIRE(f.api->get_asset(f.instance, REIM_ASSET_MODEL_DEFS_JSON, 0, &data,
                           &size, &revision) == REIM_OK);
  const Json models =
      Json::parse(static_cast<const char*>(data),
                  static_cast<const char*>(data) + size, nullptr, false);
  REQUIRE(models.is_object());
  CHECK(models["format"] == 1);
  REQUIRE(models["models"].is_array());
  CHECK(models["models"][0]["id"] == 0);
  CHECK(models["models"][0]["parts"].size() == 1);

  REQUIRE(f.api->get_asset(f.instance, REIM_ASSET_SOUND_INDEX_JSON, 0, &data,
                           &size, &revision) == REIM_OK);
  const Json sounds =
      Json::parse(static_cast<const char*>(data),
                  static_cast<const char*>(data) + size, nullptr, false);
  REQUIRE(sounds.is_object());
  REQUIRE(sounds["sounds"].is_array());
  CHECK(sounds["sounds"][0]["id"] == 1);

  /* A3/A6: the entity/part/audio/UI lists are only published in the frame
   * blob; nothing here is Host-owned or reconstructed. */
  CHECK(revision != 0);
}
