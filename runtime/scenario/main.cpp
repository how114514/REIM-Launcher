/*
 * reim-scenario: M3-A8 scenario DSL runner (headless).
 *
 * Loads mc_26.3.dll through the C ABI and executes JSON scenarios against a
 * real world. Every scenario is deterministic (fixed world/fixture, tick
 * waits and input), and every assertion is evaluated from what the Runtime
 * actually published; a scenario that cannot run is `not_run`, never `pass`.
 *
 *   reim-scenario --suite tests\scenarios --out build\m3\reports\scenarios
 *                 [--world <save>] [--generated generated\26.3] [--dll PATH]
 *
 * World selection: --world, else REIM_TEST_WORLD. Without one, all scenarios
 * are not_run (exit 2).
 */
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "reim_abi.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

namespace {

using Json = nlohmann::json;

/* Resolved from the DLL at startup; never link against the DLL import lib,
 * matching the other Hosts (the Runtime is a versioned plug-in). */
const ReimRuntimeApi* g_api = nullptr;

std::string exe_directory() {
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0) return ".";
  const std::u8string value =
      std::filesystem::path(std::wstring(buffer, length)).parent_path().u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return ".";
#endif
}

std::string trim(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

void* REIM_CALL host_alloc(size_t size, size_t align) {
  if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
#if defined(_MSC_VER)
  return _aligned_malloc(size, align);
#else
  return std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
}

void REIM_CALL host_free(void* ptr) {
  if (ptr == nullptr) return;
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void REIM_CALL host_log(int32_t level, const char* category, const char* message) {
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  const int index = std::clamp(static_cast<int>(level), 0, 4);
  std::printf("[%s] %s: %s\n", names[index], category ? category : "?",
              message ? message : "");
  std::fflush(stdout);
}

struct Options {
  std::string suite;
  std::string out;
  std::string world;
  std::string generated;
  std::string dll;
  bool help = false;
};

void print_usage() {
  std::printf(
      "reim-scenario (M3-A8, ABI %u)\n"
      "  --suite DIR       scenario JSON directory (required)\n"
      "  --out DIR         report directory (default build/m3/reports/scenarios)\n"
      "  --world PATH      save directory (default REIM_TEST_WORLD)\n"
      "  --generated DIR   generated/26.3 (default beside the exe or repo)\n"
      "  --dll PATH        mc_26.3.dll (default beside this exe)\n"
      "  --help\n",
      REIM_ABI_VERSION);
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](std::string& out) {
      if (i + 1 >= argc) return false;
      out = argv[++i];
      return true;
    };
    if (arg == "--help" || arg == "-h") options.help = true;
    else if (arg == "--suite") { if (!next(options.suite)) return false; }
    else if (arg == "--out") { if (!next(options.out)) return false; }
    else if (arg == "--world") { if (!next(options.world)) return false; }
    else if (arg == "--generated") { if (!next(options.generated)) return false; }
    else if (arg == "--dll") { if (!next(options.dll)) return false; }
    else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return options.help || (!options.suite.empty());
}

struct FrameSummary {
  bool saw_frame = false;
  bool ready = false;
  uint64_t tick = 0;
  uint32_t ui_screen = 0;
  uint32_t ui_draw_count = 0;
  uint32_t ui_hovered = 0;
  uint32_t ui_hovered_slot = REIM_UI_SLOT_NONE;
  uint32_t ui_selected_slot = 0;
  uint32_t ui_slots = 0;
  uint32_t ui_cursor_mode = 0;
  bool ui_has_clip = false;
  bool ui_has_item_icon = false;
  std::set<std::string> ui_kinds;
  std::string ui_text;
  uint32_t max_entities = 0;
  uint32_t max_entity_parts = 0;
  std::set<uint32_t> audio_sounds;
  uint32_t max_audio = 0;
  uint32_t max_particles = 0;
};

void summarize_frame(const ReimRenderFrame& frame, FrameSummary& summary) {
  summary.saw_frame = true;
  summary.ready = summary.ready || frame.ready != 0;
  summary.tick = std::max<uint64_t>(summary.tick, frame.tick);
  summary.ui_screen = frame.ui.screen_id;
  summary.ui_draw_count = std::max(summary.ui_draw_count, frame.ui.draw_count);
  summary.ui_hovered = frame.ui.hovered_element;
  summary.ui_hovered_slot = frame.ui.hovered_slot;
  summary.ui_selected_slot = frame.ui.inventory.selected_slot;
  summary.ui_slots = frame.ui.inventory.slot_count;
  summary.ui_cursor_mode = frame.ui.cursor_mode;
  if (frame.ui.text != nullptr && frame.ui.text_bytes != 0) {
    summary.ui_text.assign(frame.ui.text, frame.ui.text + frame.ui.text_bytes);
  }
  for (uint32_t i = 0; i < frame.ui.draw_count; ++i) {
    const ReimUiDraw& draw = frame.ui.draws[i];
    if ((draw.flags & REIM_UI_FLAG_CLIP) != 0) summary.ui_has_clip = true;
    if ((draw.flags & REIM_UI_FLAG_ITEM_ICON) != 0) summary.ui_has_item_icon = true;
    if (draw.kind == REIM_UI_DRAW_RECT) summary.ui_kinds.insert("rect");
    else if (draw.kind == REIM_UI_DRAW_TEXTURED_QUAD) summary.ui_kinds.insert("textured_quad");
    else if (draw.kind == REIM_UI_DRAW_GLYPH_QUAD) summary.ui_kinds.insert("glyph_quad");
  }
  summary.max_entities = std::max(summary.max_entities, frame.entity_count);
  for (uint32_t i = 0; i < frame.entity_count; ++i) {
    summary.max_entity_parts =
        std::max(summary.max_entity_parts, frame.entities[i].part_count);
  }
  summary.max_audio = std::max(summary.max_audio, frame.audio_count);
  summary.max_particles = std::max(summary.max_particles, frame.particle_count);
  for (uint32_t i = 0; i < frame.audio_count; ++i) {
    summary.audio_sounds.insert(frame.audio_events[i].sound_id);
  }
}

struct Session {
  const ReimRuntimeApi* api = nullptr;
  ReimInstance* instance = nullptr;
  ReimHostApi host{};
  uint64_t sequence = 0;
  bool expect_create_error = false;
  bool create_failed = false;
  int32_t create_error_code = 0;

  bool open(const Options& options, const Json& scenario, std::string& error) {
    api = g_api;
    if (api == nullptr) {
      error = "reim_get_api rejected the ABI";
      return false;
    }
    host = ReimHostApi{};
    host.struct_size = sizeof(host);
    host.abi_version = REIM_ABI_VERSION;
    host.alloc = &host_alloc;
    host.free = &host_free;
    host.log = &host_log;

    uint32_t flags = 0;
    if (scenario.contains("flags") && scenario["flags"].is_array()) {
      for (const auto& flag : scenario["flags"]) {
        const std::string name = flag.get<std::string>();
        if (name == "ui_spike") flags |= REIM_CONFIG_FLAG_UI_SPIKE;
        else if (name == "channel_spike") flags |= REIM_CONFIG_FLAG_CHANNEL_SPIKE;
        else if (name == "spawn_override") flags |= REIM_CONFIG_FLAG_SPAWN_OVERRIDE;
      }
    }
    uint32_t width = 1280;
    uint32_t height = 720;
    float scale = 2.0f;
    if (scenario.contains("ui_canvas") && scenario["ui_canvas"].is_array() &&
        scenario["ui_canvas"].size() >= 3) {
      width = scenario["ui_canvas"][0].get<uint32_t>();
      height = scenario["ui_canvas"][1].get<uint32_t>();
      scale = scenario["ui_canvas"][2].get<float>();
    }

    if (scenario.contains("spawn") && scenario["spawn"].is_array() &&
        scenario["spawn"].size() >= 3) {
      flags |= REIM_CONFIG_FLAG_SPAWN_OVERRIDE;
    }

    const std::wstring world = std::filesystem::path(options.world).wstring();
    const std::wstring generated = std::filesystem::path(options.generated).wstring();
    const std::string scenario_text = scenario.dump();
    std::string resource_packs_json;
    std::string data_packs_json;
    if (scenario.contains("resource_packs") && scenario["resource_packs"].is_array()) {
      resource_packs_json = scenario["resource_packs"].dump();
    }
    if (scenario.contains("data_packs") && scenario["data_packs"].is_array()) {
      data_packs_json = scenario["data_packs"].dump();
    }
    ReimInstanceConfig config{};
    config.struct_size = sizeof(config);
    config.abi_version = REIM_ABI_VERSION;
    config.world_path = world.c_str();
    config.generated_path = generated.c_str();
    config.render_distance = scenario.value("render_distance", 2u);
    config.render_level = scenario.value("render_level", 2u);
    config.mouse_sensitivity = 0.15f;
    config.flags = flags;
    config.ui_scale = static_cast<uint32_t>(scale);
    config.gamemode = scenario.value("gamemode_id", 0u);
    config.difficulty = scenario.value("difficulty_id", 2u);
    config.create_world = scenario.value("create_world", false) ? 1u : 0u;
    config.seed = scenario.value("seed", 0ull);
    config.scenario = scenario_text.c_str();
    config.resource_packs =
        resource_packs_json.empty() ? nullptr : resource_packs_json.c_str();
    config.data_packs = data_packs_json.empty() ? nullptr : data_packs_json.c_str();
    if ((flags & REIM_CONFIG_FLAG_SPAWN_OVERRIDE) != 0) {
      config.spawn_x = scenario["spawn"][0].get<float>();
      config.spawn_y = scenario["spawn"][1].get<float>();
      config.spawn_z = scenario["spawn"][2].get<float>();
      config.spawn_yaw = scenario.value("spawn_yaw", 0.0f);
      config.spawn_pitch = scenario.value("spawn_pitch", 0.0f);
    }
    int32_t code = 0;
    expect_create_error = scenario.contains("expect_create_error");
    instance = api->create(&config, &host, &code);
    if (instance == nullptr) {
      if (expect_create_error) {
        create_failed = true;
        create_error_code = code;
        return true;
      }
      error = "create failed (" + std::to_string(code) + ")";
      return false;
    }
    if (api->set_ui_canvas) api->set_ui_canvas(instance, width, height, scale);
    if (api->start(instance) != REIM_OK) {
      error = "start failed";
      return false;
    }
    return true;
  }

  ~Session() {
    if (api != nullptr && instance != nullptr) {
      api->stop(instance);
      api->destroy(instance);
    }
  }

  bool fetch(FrameSummary& summary, int32_t& result) {
    ReimRenderFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    result = api->fetch_frame(instance, &frame);
    if (result == REIM_OK) {
      summarize_frame(frame, summary);
      api->release_frame(instance, frame.frame_id);
    }
    return result == REIM_OK;
  }

  Json status() {
    /* The status is rebuilt on each call and may grow by a few bytes between
     * the size query and the fill (positions/health digits, spawned entities).
     * Retry with slack instead of returning an empty object. */
    for (int attempt = 0; attempt < 4; ++attempt) {
      const int32_t needed = api->get_status_json(instance, nullptr, 0);
      if (needed <= 0) return Json::object();
      std::vector<char> buffer(static_cast<size_t>(needed) + 1024, '\0');
      const int32_t written = api->get_status_json(
          instance, buffer.data(), static_cast<uint32_t>(buffer.size()));
      if (written <= 0) continue;
      Json parsed =
          Json::parse(buffer.data(), buffer.data() + written, nullptr, false);
      if (!parsed.is_discarded()) return parsed;
    }
    return Json::object();
  }

  void submit(uint32_t type, const Json& step) {
    ReimActionEvent event{};
    event.struct_size = sizeof(event);
    event.type = type;
    event.sequence = ++sequence;
    event.screen_id = step.value("screen", 0u);
    event.a = step.value("a", 0);
    event.b = step.value("b", 0);
    event.c = step.value("c", 0u);
    event.x = step.value("x", 0.0f);
    event.y = step.value("y", 0.0f);
    event.z = step.value("z", 0.0f);
    event.screen_x = step.value("screen_x", 0.0f);
    event.screen_y = step.value("screen_y", 0.0f);
    event.wheel = step.value("wheel", 0.0f);
    if (step.contains("buttons") && step["buttons"].is_array()) {
      for (const auto& button : step["buttons"]) {
        const std::string name = button.get<std::string>();
        if (name == "left") event.c |= REIM_POINTER_BUTTON_LEFT;
        else if (name == "right") event.c |= REIM_POINTER_BUTTON_RIGHT;
        else if (name == "middle") event.c |= REIM_POINTER_BUTTON_MIDDLE;
      }
    }
    api->submit_action(instance, &event);
  }

  void submit_key(int key, int action) {
    ReimInputEvent event{};
    event.struct_size = sizeof(event);
    event.type = REIM_INPUT_KEY;
    event.key = key;
    event.action = action;
    api->submit_input(instance, &event);
  }
};

bool wait_ready(Session& session, FrameSummary& summary, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int32_t result = 0;
    if (!session.fetch(summary, result)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    if (summary.ready) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

bool wait_ticks(Session& session, FrameSummary& summary, uint64_t ticks,
                int timeout_ms) {
  const uint64_t target = summary.tick + ticks;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    int32_t result = 0;
    if (session.fetch(summary, result) && summary.tick >= target) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

const Json* path_lookup(const Json& root, const std::string& path) {
  const Json* node = &root;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t dot = path.find('.', start);
    const std::string key = path.substr(start, dot == std::string::npos
                                                   ? std::string::npos
                                                   : dot - start);
    if (!node->is_object() || !node->contains(key)) return nullptr;
    node = &(*node)[key];
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return node;
}

uint32_t action_type(const std::string& name) {
  if (name == "pointer") return REIM_ACTIONTYPE_POINTER;
  if (name == "slot_click") return REIM_ACTIONTYPE_SLOT_CLICK;
  if (name == "select_slot") return REIM_ACTIONTYPE_SELECT_SLOT;
  if (name == "drop_item") return REIM_ACTIONTYPE_DROP_ITEM;
  if (name == "break_start") return REIM_ACTIONTYPE_BREAK_START;
  if (name == "break_stop") return REIM_ACTIONTYPE_BREAK_STOP;
  if (name == "break_block") return REIM_ACTIONTYPE_BREAK_BLOCK;
  if (name == "place_block") return REIM_ACTIONTYPE_PLACE_BLOCK;
  if (name == "use_block") return REIM_ACTIONTYPE_USE_BLOCK;
  if (name == "use_item") return REIM_ACTIONTYPE_USE_ITEM;
  if (name == "attack") return REIM_ACTIONTYPE_ATTACK;
  if (name == "pick_block") return REIM_ACTIONTYPE_PICK_BLOCK;
  if (name == "use_start") return REIM_ACTIONTYPE_USE_START;
  if (name == "use_stop") return REIM_ACTIONTYPE_USE_STOP;
  if (name == "wheel") return REIM_ACTIONTYPE_WHEEL;
  if (name == "close_screen") return REIM_ACTIONTYPE_CLOSE_SCREEN;
  if (name == "respawn") return REIM_ACTIONTYPE_RESPAWN;
  if (name == "debug_set_time") return REIM_ACTIONTYPE_DEBUG_SET_TIME;
  if (name == "debug_set_weather") return REIM_ACTIONTYPE_DEBUG_SET_WEATHER;
  if (name == "drag_start") return REIM_ACTIONTYPE_DRAG_START;
  if (name == "drag_to") return REIM_ACTIONTYPE_DRAG_TO;
  if (name == "drag_end") return REIM_ACTIONTYPE_DRAG_END;
  return REIM_ACTIONTYPE_NONE;
}

int key_code(const std::string& name) {
  if (name == "w") return REIM_KEY_W;
  if (name == "a") return REIM_KEY_A;
  if (name == "s") return REIM_KEY_S;
  if (name == "d") return REIM_KEY_D;
  if (name == "space") return REIM_KEY_SPACE;
  if (name == "shift") return REIM_KEY_LEFT_SHIFT;
  if (name == "control") return REIM_KEY_LEFT_CONTROL;
  if (name == "escape") return REIM_KEY_ESCAPE;
  if (name == "e") return REIM_KEY_E;
  if (name == "q") return REIM_KEY_Q;
  if (name == "enter") return REIM_KEY_ENTER;
  if (name == "tab") return REIM_KEY_TAB;
  if (name == "1") return REIM_KEY_1;
  if (name == "2") return REIM_KEY_2;
  if (name == "3") return REIM_KEY_3;
  if (name == "4") return REIM_KEY_4;
  if (name == "5") return REIM_KEY_5;
  if (name == "6") return REIM_KEY_6;
  if (name == "7") return REIM_KEY_7;
  if (name == "8") return REIM_KEY_8;
  if (name == "9") return REIM_KEY_9;
  return REIM_KEY_UNKNOWN;
}

struct AssertionResult {
  std::string type;
  bool pass = false;
  std::string detail;
};

bool evaluate(const Json& assertion, const Json& status, const FrameSummary& frame,
              AssertionResult& result) {
  result.type = assertion.value("type", "");
  if (result.type == "status_equals") {
    const std::string path = assertion.value("path", "");
    const Json* value = path_lookup(status, path);
    if (value == nullptr) {
      result.detail = "missing status path " + path;
      return false;
    }
    result.pass = *value == assertion["value"];
    result.detail = path + "=" + value->dump() + " expected " +
                    assertion["value"].dump();
    return result.pass;
  }
  if (result.type == "status_at_least") {
    const std::string path = assertion.value("path", "");
    const Json* value = path_lookup(status, path);
    if (value == nullptr || !value->is_number()) {
      result.detail = "missing numeric status path " + path;
      return false;
    }
    result.pass = value->get<double>() >= assertion.value("value", 0.0);
    result.detail = path + "=" + value->dump() + " >= " +
                    std::to_string(assertion.value("value", 0.0));
    return result.pass;
  }
  if (result.type == "ui_min_draws") {
    result.pass = frame.ui_draw_count >= assertion.value("value", 1u);
    result.detail = "draws=" + std::to_string(frame.ui_draw_count);
    return result.pass;
  }
  if (result.type == "ui_kind") {
    const std::string kind = assertion.value("value", "");
    result.pass = frame.ui_kinds.contains(kind);
    result.detail = "kinds=" + (frame.ui_kinds.empty() ? std::string("none")
                                                       : *frame.ui_kinds.begin());
    return result.pass;
  }
  if (result.type == "ui_has_clip") {
    result.pass = frame.ui_has_clip;
    result.detail = "clip=" + std::string(frame.ui_has_clip ? "true" : "false");
    return result.pass;
  }
  if (result.type == "ui_has_item_icon") {
    result.pass = frame.ui_has_item_icon;
    result.detail = "item_icon=" + std::string(frame.ui_has_item_icon ? "true" : "false");
    return result.pass;
  }
  if (result.type == "ui_slots") {
    result.pass = frame.ui_slots == assertion.value("value", 0u);
    result.detail = "slots=" + std::to_string(frame.ui_slots);
    return result.pass;
  }
  if (result.type == "entities_at_least") {
    result.pass = frame.max_entities >= assertion.value("value", 1u);
    result.detail = "entities=" + std::to_string(frame.max_entities);
    return result.pass;
  }
  if (result.type == "entity_type_count") {
    const std::string type = assertion.value("entity", assertion.value("type_name", ""));
    const int minimum = assertion.value("min", 1);
    const int maximum = assertion.value("max", -1);
    int actual = 0;
    const Json* counts = path_lookup(status, "gameplay.mob_counts");
    if (counts != nullptr && counts->is_object() && counts->contains(type) &&
        (*counts)[type].is_number_integer()) {
      actual = (*counts)[type].get<int>();
    }
    result.pass = actual >= minimum && (maximum < 0 || actual <= maximum);
    result.detail = type + "=" + std::to_string(actual) + " expected >=" +
                    std::to_string(minimum);
    return result.pass;
  }
  if (result.type == "entity_count_at_least") {
    const int minimum = assertion.value("value", 1);
    const Json* count = path_lookup(status, "gameplay.entity_count");
    const int actual = count != nullptr && count->is_number_integer()
                           ? count->get<int>()
                           : 0;
    result.pass = actual >= minimum;
    result.detail = "entity_count=" + std::to_string(actual);
    return result.pass;
  }
  if (result.type == "player_health_at_most" ||
      result.type == "player_health_at_least" ||
      result.type == "player_food_at_most" ||
      result.type == "player_air_at_most" ||
      result.type == "player_xp_total_at_least" ||
      result.type == "player_xp_level_at_least") {
    std::string path = "player.";
    if (result.type.find("health") != std::string::npos) path += "health";
    else if (result.type.find("food") != std::string::npos) path += "food";
    else if (result.type.find("air") != std::string::npos) path += "air";
    else if (result.type.find("xp_level") != std::string::npos) path += "xp_level";
    else path = "gameplay.survival.xp_total";
    const Json* value = path_lookup(status, path);
    const double actual = value != nullptr && value->is_number()
                              ? value->get<double>()
                              : 0.0;
    const double target = assertion.value("value", 0.0);
    result.pass = result.type.find("_at_least") != std::string::npos
                      ? actual >= target
                      : actual <= target;
    result.detail = path + "=" + std::to_string(actual) + " target " +
                    std::to_string(target);
    return result.pass;
  }
  if (result.type == "player_dead_equals") {
    const Json* dead = path_lookup(status, "player.dead");
    const bool actual = dead != nullptr && dead->is_boolean() && dead->get<bool>();
    result.pass = actual == assertion.value("value", true);
    result.detail = std::string("dead=") + (actual ? "true" : "false");
    return result.pass;
  }
  if (result.type == "damage_taken_at_least") {
    const Json* value = path_lookup(status, "gameplay.survival.damage_taken");
    const double actual = value != nullptr && value->is_number()
                              ? value->get<double>()
                              : 0.0;
    result.pass = actual >= assertion.value("value", 1.0);
    result.detail = "damage_taken=" + std::to_string(actual);
    return result.pass;
  }
  if (result.type == "entity_target_type") {
    const std::string expected = assertion.value("value", "");
    const Json* type = path_lookup(status, "gameplay.target.entity_type");
    const std::string actual =
        type != nullptr && type->is_string() ? type->get<std::string>() : "";
    result.pass = actual == expected;
    result.detail = "entity_target=" + actual;
    return result.pass;
  }
  if (result.type == "entity_type_health_at_most") {
    const std::string type = assertion.value("entity", "");
    const double maximum = assertion.value("value", 0.0);
    const Json* entities = path_lookup(status, "gameplay.entities");
    double actual = -1.0;
    if (entities != nullptr && entities->is_array()) {
      for (const Json& entity : *entities) {
        if (!entity.contains("type") || !entity["type"].is_string()) continue;
        if (entity["type"].get<std::string>() != type) continue;
        actual = entity.value("health", -1.0);
        break;
      }
    }
    result.pass = actual >= 0.0 && actual <= maximum;
    result.detail = type + ".health=" + std::to_string(actual) + " expected <=" +
                    std::to_string(maximum);
    return result.pass;
  }
  if (result.type == "entity_distance_at_least") {
    const std::string type = assertion.value("entity", "");
    const double minimum = assertion.value("value", 0.0);
    const Json* entities = path_lookup(status, "gameplay.entities");
    const Json* player = path_lookup(status, "player");
    double best = -1.0;
    if (entities != nullptr && entities->is_array() && player != nullptr) {
      const double px = player->value("x", 0.0);
      const double pz = player->value("z", 0.0);
      for (const Json& entity : *entities) {
        if (!entity.contains("type") || !entity["type"].is_string()) continue;
        if (entity["type"].get<std::string>() != type) continue;
        const double dx = entity.value("x", 0.0) - px;
        const double dz = entity.value("z", 0.0) - pz;
        best = std::sqrt(dx * dx + dz * dz);
        break;
      }
    }
    result.pass = best >= minimum;
    result.detail = type + ".distance=" + std::to_string(best);
    return result.pass;
  }
  if (result.type == "entity_parts_at_least") {
    result.pass = frame.max_entity_parts >= assertion.value("value", 1u);
    result.detail = "parts=" + std::to_string(frame.max_entity_parts);
    return result.pass;
  }
  if (result.type == "particles_at_least") {
    result.pass = frame.max_particles >= assertion.value("value", 1u);
    result.detail = "particles=" + std::to_string(frame.max_particles) + " >= " +
                    std::to_string(assertion.value("value", 1u));
    return result.pass;
  }
  if (result.type == "audio_sound_seen") {
    const uint32_t id = assertion.value("value", 0u);
    result.pass = frame.audio_sounds.contains(id);
    result.detail = "sound_" + std::to_string(id) + (result.pass ? "=seen" : "=missing");
    return result.pass;
  }
  if (result.type == "inventory_count") {
    const std::string item = assertion.value("item", "");
    const Json* inventory = path_lookup(status, "gameplay.inventory");
    int count = 0;
    if (inventory != nullptr && inventory->is_object() && inventory->contains(item) &&
        (*inventory)[item].is_number_integer()) {
      count = (*inventory)[item].get<int>();
    }
    const int minimum = assertion.value("min", 1);
    const int maximum = assertion.value("max", -1);
    result.pass = count >= minimum && (maximum < 0 || count <= maximum);
    result.detail = item + "=" + std::to_string(count) + " expected >=" +
                    std::to_string(minimum) +
                    (maximum >= 0 ? " and <=" + std::to_string(maximum) : "");
    return result.pass;
  }
  if (result.type == "slot_item") {
    const int slot = assertion.value("slot", -1);
    const std::string expected_item = assertion.value("item", "");
    const int expected_count = assertion.value("count", -1);
    const Json* slots = path_lookup(status, "gameplay.slots");
    std::string actual_item;
    int actual_count = 0;
    bool found = false;
    if (slots != nullptr && slots->is_array()) {
      for (const Json& entry : *slots) {
        if (!entry.contains("slot") || !entry["slot"].is_number_integer()) continue;
        if (entry["slot"].get<int>() != slot) continue;
        found = true;
        const Json* item = entry.contains("item") ? &entry["item"] : nullptr;
        if (item != nullptr && item->is_object()) {
          if (item->contains("item") && (*item)["item"].is_string()) {
            actual_item = (*item)["item"].get<std::string>();
          }
          if (item->contains("count") && (*item)["count"].is_number_integer()) {
            actual_count = (*item)["count"].get<int>();
          }
        }
        break;
      }
    }
    bool pass = found;
    if (pass && !expected_item.empty()) pass = actual_item == expected_item;
    if (pass && expected_count >= 0) pass = actual_count == expected_count;
    result.pass = pass;
    result.detail = "slot " + std::to_string(slot) + " item=" + actual_item +
                    " count=" + std::to_string(actual_count);
    return result.pass;
  }
  if (result.type == "held_item") {
    const std::string expected = assertion.value("item", "");
    const Json* held = path_lookup(status, "gameplay.held.item");
    const std::string actual =
        held != nullptr && held->is_string() ? held->get<std::string>() : "";
    result.pass = actual == expected;
    result.detail = "held=" + actual + " expected " + expected;
    return result.pass;
  }
  if (result.type == "block_at") {
    const int x = assertion.value("x", 0);
    const int y = assertion.value("y", 0);
    const int z = assertion.value("z", 0);
    const std::string expected = assertion.value("state", assertion.value("block", ""));
    const Json* probes = path_lookup(status, "gameplay.probes");
    std::string actual = "<missing>";
    if (probes != nullptr && probes->is_array()) {
      for (const Json& probe : *probes) {
        if (!probe.contains("x") || !probe.contains("y") || !probe.contains("z")) {
          continue;
        }
        if (probe["x"].get<int>() != x || probe["y"].get<int>() != y ||
            probe["z"].get<int>() != z) {
          continue;
        }
        actual = probe.value("block", "");
        break;
      }
    }
    result.pass = !expected.empty() && actual.rfind(expected, 0) == 0;
    result.detail = "block(" + std::to_string(x) + "," + std::to_string(y) + "," +
                    std::to_string(z) + ")=" + actual + " expected " + expected;
    return result.pass;
  }
  if (result.type == "biome_at") {
    const int x = assertion.value("x", 0);
    const int y = assertion.value("y", 0);
    const int z = assertion.value("z", 0);
    const std::string expected = assertion.value("value", "");
    const Json* probes = path_lookup(status, "gameplay.probes");
    std::string actual = "<missing>";
    if (probes != nullptr && probes->is_array()) {
      for (const Json& probe : *probes) {
        if (probe.value("x", 0) != x || probe.value("y", 0) != y ||
            probe.value("z", 0) != z) {
          continue;
        }
        actual = probe.value("biome", "");
        break;
      }
    }
    result.pass = !expected.empty() && actual == expected;
    result.detail = "biome(" + std::to_string(x) + "," + std::to_string(y) +
                    "," + std::to_string(z) + ")=" + actual + " expected " +
                    expected;
    return result.pass;
  }
  if (result.type == "screen_equals") {
    const Json* screen = path_lookup(status, "gameplay.screen");
    const int actual = screen != nullptr && screen->is_number_integer()
                           ? screen->get<int>()
                           : -1;
    result.pass = actual == assertion.value("value", -1);
    result.detail = "screen=" + std::to_string(actual);
    return result.pass;
  }
  if (result.type == "drops_at_least") {
    const Json* count = path_lookup(status, "gameplay.drops_count");
    const int actual = count != nullptr && count->is_number_integer()
                           ? count->get<int>()
                           : 0;
    result.pass = actual >= assertion.value("value", 1);
    result.detail = "drops=" + std::to_string(actual);
    return result.pass;
  }
  if (result.type == "ui_text_contains") {
    const std::string value = assertion.value("value", "");
    result.pass = frame.ui_text.find(value) != std::string::npos;
    result.detail = "text " + std::string(result.pass ? "contains " : "lacks ") + value;
    return result.pass;
  }
  if (result.type == "ui_cursor_mode") {
    result.pass = frame.ui_cursor_mode == assertion.value("value", 0u);
    result.detail = "cursor_mode=" + std::to_string(frame.ui_cursor_mode);
    return result.pass;
  }
  if (result.type == "target_block") {
    const std::string expected = assertion.value("value", "");
    const Json* block = path_lookup(status, "gameplay.target.block");
    const std::string actual =
        block != nullptr && block->is_string() ? block->get<std::string>() : "";
    result.pass = actual.rfind(expected, 0) == 0;
    result.detail = "target=" + actual + " expected " + expected;
    return result.pass;
  }
  result.detail = "unknown assertion type";
  return false;
}

Json run_scenario(const Options& options, const std::filesystem::path& path) {
  Json report;
  report["scenario"] = path.filename().string();
  report["status"] = "not_run";
  Json scenario;
  {
    std::ifstream stream(path);
    if (!stream) {
      report["error"] = "cannot read scenario";
      return report;
    }
    scenario = Json::parse(stream, nullptr, false);
    if (scenario.is_discarded()) {
      report["error"] = "invalid JSON";
      return report;
    }
  }
  report["id"] = scenario.value("id", path.stem().string());

  Session session;
  std::string error;
  if (!session.open(options, scenario, error)) {
    report["error"] = error;
    return report;
  }
  if (session.expect_create_error) {
    if (!session.create_failed) {
      report["error"] = "expected create failure but create succeeded";
      return report;
    }
    report["status"] = "pass";
    report["create_error_code"] = session.create_error_code;
    Json checks = Json::array();
    checks.push_back(Json{{"type", "expect_create_error"},
                          {"pass", true},
                          {"detail", "create failed with code " +
                                         std::to_string(session.create_error_code)}});
    report["assertions"] = std::move(checks);
    return report;
  }
  if (session.create_failed) {
    report["error"] = "create failed unexpectedly";
    return report;
  }

  FrameSummary frame;
  const int timeout_ms = scenario.value("timeout_ms", 20000);
  if (!wait_ready(session, frame, timeout_ms)) {
    report["error"] = "world did not become ready";
    return report;
  }

  Json steps = scenario.value("steps", Json::array());
  Json mid_assertions = Json::array();
  bool mid_pass = true;
  for (const Json& step : steps) {
    if (step.contains("assert") && step["assert"].is_object()) {
      const Json current = session.status();
      AssertionResult evaluated;
      const bool pass = evaluate(step["assert"], current, frame, evaluated);
      mid_pass = mid_pass && pass;
      mid_assertions.push_back({{"type", evaluated.type},
                                {"pass", pass},
                                {"detail", evaluated.detail},
                                {"phase", "mid"}});
      continue;
    }
    if (step.contains("wait_ticks")) {
      if (!wait_ticks(session, frame, step["wait_ticks"].get<uint64_t>(),
                      timeout_ms)) {
        report["error"] = "wait_ticks timed out";
        return report;
      }
    } else if (step.contains("action")) {
      const Json& action = step["action"];
      const uint32_t type = action_type(action.value("type", ""));
      if (type == REIM_ACTIONTYPE_NONE) {
        report["error"] = "unknown action type";
        return report;
      }
      session.submit(type, action);
    } else if (step.contains("input")) {
      const Json& input = step["input"];
      const int key = key_code(input.value("key", ""));
      if (key == REIM_KEY_UNKNOWN) {
        report["error"] = "unknown input key";
        return report;
      }
      const std::string action = input.value("action", "press");
      session.submit_key(key, action == "release" ? REIM_ACTION_RELEASE
                                                  : REIM_ACTION_PRESS);
    } else if (step.contains("fetch_frames")) {
      const int count = step["fetch_frames"].get<int>();
      for (int i = 0; i < count; ++i) {
        int32_t result = 0;
        session.fetch(frame, result);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }

  /* One last fetch so assertions see the state after the final step. */
  int32_t result = 0;
  session.fetch(frame, result);

  const Json final_status = session.status();
  Json assertions = mid_assertions;
  bool all_pass = mid_pass;
  for (const Json& assertion : scenario.value("assert", Json::array())) {
    AssertionResult evaluated;
    const bool pass = evaluate(assertion, final_status, frame, evaluated);
    all_pass = all_pass && pass;
    assertions.push_back({{"type", evaluated.type},
                          {"pass", pass},
                          {"detail", evaluated.detail}});
  }
  report["assertions"] = assertions;
  report["gameplay"] = final_status.contains("gameplay") ? final_status["gameplay"]
                                                         : Json::object();
  report["status"] = all_pass ? "pass" : "fail";
  Json audio_ids = Json::array();
  for (uint32_t sound : frame.audio_sounds) audio_ids.push_back(sound);
  report["frame"] = {{"tick", frame.tick},
                     {"ui_draws", frame.ui_draw_count},
                     {"ui_kinds", frame.ui_kinds},
                     {"entities", frame.max_entities},
                     {"audio_count", frame.max_audio},
                     {"particles", frame.max_particles},
                     {"audio_ids", std::move(audio_ids)}};
  return report;
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    print_usage();
    return 1;
  }
  if (options.help) {
    print_usage();
    return 0;
  }

  if (options.world.empty()) {
    const char* env = std::getenv("REIM_TEST_WORLD");
    if (env != nullptr) options.world = env;
  }
  if (options.generated.empty()) {
    const char* env = std::getenv("REIM_TEST_GENERATED");
    options.generated = env != nullptr ? env
                                       : (std::filesystem::path(exe_directory()) /
                                          "../../../generated/26.3")
                                             .lexically_normal()
                                             .string();
  }
  if (options.dll.empty()) {
    options.dll = (std::filesystem::path(exe_directory()) / "mc_26.3.dll").string();
  }
  if (options.out.empty()) options.out = "build/m3/reports/scenarios";

#ifdef _WIN32
  HMODULE module = ::LoadLibraryW(std::filesystem::path(options.dll).wstring().c_str());
  if (!module) {
    std::fprintf(stderr, "LoadLibraryW failed for %s (error %lu)\n",
                 options.dll.c_str(), ::GetLastError());
    return 3;
  }
  using GetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
  auto get_api =
      reinterpret_cast<GetApiFn>(::GetProcAddress(module, "reim_get_api"));
  if (get_api == nullptr) {
    std::fprintf(stderr, "GetProcAddress(reim_get_api) failed\n");
    ::FreeLibrary(module);
    return 4;
  }
  g_api = get_api(REIM_ABI_VERSION);
  if (g_api == nullptr) {
    std::fprintf(stderr, "reim_get_api rejected ABI %u\n", REIM_ABI_VERSION);
    ::FreeLibrary(module);
    return 5;
  }
#endif

  std::vector<std::filesystem::path> scenarios;
  std::error_code ec;
  std::filesystem::create_directories(options.suite, ec);
  for (const auto& entry : std::filesystem::directory_iterator(options.suite, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      scenarios.push_back(entry.path());
    }
  }
  std::sort(scenarios.begin(), scenarios.end());
  std::filesystem::create_directories(options.out, ec);

  if (options.world.empty() || scenarios.empty()) {
    std::printf("not_run: %s\n",
                options.world.empty() ? "no world (REIM_TEST_WORLD not set)"
                                      : "no scenarios");
    return options.world.empty() ? 2 : 4;
  }

  int passed = 0;
  int failed = 0;
  for (const auto& scenario : scenarios) {
    const Json report = run_scenario(options, scenario);
    const std::string status = report.value("status", "not_run");
    std::filesystem::path out = options.out / scenario.stem();
    out += ".json";
    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    stream << report.dump(2) << "\n";
    std::printf("%-14s %s\n", scenario.stem().string().c_str(), status.c_str());
    if (status == "pass") ++passed;
    else if (status == "fail") ++failed;
  }
  std::printf("scenarios: %d pass, %d fail, %d not_run\n", passed, failed,
              static_cast<int>(scenarios.size()) - passed - failed);
#ifdef _WIN32
  ::FreeLibrary(module);
#endif
  return failed == 0 ? 0 : 1;
}
