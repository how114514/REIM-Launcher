/*
 * REIMPreview.exe, S4 host (M1-F + M1-G).
 *
 * Development-only OpenGL 3.3 host. It loads mc_26.3.dll through the C ABI,
 * reads the Runtime's immutable render frames, uploads/draws the supplied
 * section meshes and forwards input. It never reconstructs world state and
 * never writes to the save.
 *
 * Unified M1 launch arguments (docs/M1_PLAN.md M1-H1):
 *   --world PATH            absolute 26.3 save directory (required to run)
 *   --generated DIR         generated/26.3 (default: repo-relative search)
 *   --minecraft-dir PATH    recorded for diagnostics
 *   --render-distance N     chunk radius, default 6
 *
 * Development / automation flags:
 *   --dll PATH              mc_26.3.dll (default: beside this exe)
 *   --sensitivity X         degrees per mouse pixel (default 0.15)
 *   --invert-y              invert vertical mouse look
 *   --frames N              exit after N presented frames
 *   --seconds N             exit after N wall-clock seconds
 *   --hidden                create an invisible window (automation)
 *   --no-vsync              disable vsync
 *   --screenshot PATH       write the last frame as PNG
 *   --report PATH           write a JSON run summary
 *   --check-dll PATH        validate LoadLibrary + reim_get_api only
 *   --check-generated DIR   validate generated/26.3 through DLL create
 *   --help, --version
 */
#ifdef _WIN32
#include <windows.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "gl_loader.h"
#include "gl_renderer.h"
#include "hud.h"
#include "ui_canvas.h"
#include "reim_abi.h"
#include "render_math.h"
#include "util/log.h"
#include "util/png.h"

#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
  std::string world;
  std::string generated;
  std::string minecraft_dir;
  std::string dll;
  std::string screenshot;
  std::string report;
  std::string scenario;
  std::string check_dll_path;
  std::string check_generated_dir;
  int frames = 0;
  double seconds = 0.0;
  uint32_t render_distance = 6;
  float sensitivity = 0.15f;
  bool invert_y = false;
  bool hidden = false;
  bool vsync = true;
  bool third_person = false;
  bool show_aabb = false;
  bool ui_spike = false;
  bool channel_spike = false;
  bool open_inventory = false;
  float ui_scale = 2.0f;
  float ui_click_x = -1.0f;
  float ui_click_y = -1.0f;
  double auto_walk_seconds = 0.0;
  uint64_t auto_walk_ticks = 0;
  uint64_t exit_tick = 0;
  bool spawn_override = false;
  float spawn_x = 0.0f;
  float spawn_y = 0.0f;
  float spawn_z = 0.0f;
  float spawn_yaw = 0.0f;
  float spawn_pitch = 0.0f;
  bool help = false;
  bool version = false;
};

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

void print_usage() {
  std::printf(
      "REIMPreview (M1-F/G, ABI %u)\n"
      "  --world PATH           26.3 save directory (required)\n"
      "  --generated DIR        generated/26.3 (default: auto-detect)\n"
      "  --minecraft-dir PATH   recorded for diagnostics\n"
      "  --render-distance N    chunk radius, default 6\n"
      "  --dll PATH             mc_26.3.dll (default: beside this exe)\n"
      "  --sensitivity X        degrees per mouse pixel, default 0.15\n"
      "  --invert-y             invert vertical look\n"
      "  --frames N             exit after N presented frames\n"
      "  --seconds N            exit after N seconds\n"
      "  --hidden               create an invisible window\n"
      "  --no-vsync             disable vsync\n"
      "  --third-person         start in the third-person debug view\n"
      "  --show-aabb            draw the player AABB at start\n"
      "  --ui-spike             draw the M3-A5 Runtime UI display list\n"
      "  --channel-spike        show the M3-A6 entity box and play its sound\n"
      "  --ui-scale N           integer UI layout scale (default 2)\n"
      "  --ui-click X Y         submit one pointer click at frame pixels once ready\n"
      "  --open-inventory       press E once ready (screenshot/diagnostic)\n"
      "  --auto-walk SECONDS    hold forward for N seconds once ready (diagnostics)\n"
      "  --auto-walk-ticks N    hold forward for N simulation ticks once ready\n"
      "  --spawn X Y Z          override spawn (diagnostics; same as UE instance)\n"
      "  --spawn-yaw DEG        spawn yaw (with --spawn)\n"
      "  --spawn-pitch DEG      spawn pitch (with --spawn)\n"
      "  --exit-tick N          exit once the Runtime reaches tick N\n"
      "  --screenshot PATH      write the last frame as PNG\n"
      "  --scenario PATH        diagnostic scenario JSON (setup/create_world)\n"
      "  --report PATH          write a JSON run summary\n"
      "  --check-dll PATH       validate the DLL ABI without a window\n"
      "  --check-generated DIR  validate generated/26.3 through DLL create\n"
      "  --help, --version\n",
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
    if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else if (arg == "--version") {
      options.version = true;
    } else if (arg == "--world") {
      if (!next(options.world)) return false;
    } else if (arg == "--generated") {
      if (!next(options.generated)) return false;
    } else if (arg == "--minecraft-dir") {
      if (!next(options.minecraft_dir)) return false;
    } else if (arg == "--dll") {
      if (!next(options.dll)) return false;
    } else if (arg == "--screenshot") {
      if (!next(options.screenshot)) return false;
    } else if (arg == "--report") {
      if (!next(options.report)) return false;
    } else if (arg == "--check-dll") {
      if (!next(options.check_dll_path)) return false;
    } else if (arg == "--check-generated") {
      if (!next(options.check_generated_dir)) return false;
    } else if (arg == "--frames") {
      std::string value;
      if (!next(value)) return false;
      options.frames = std::atoi(value.c_str());
    } else if (arg == "--seconds") {
      std::string value;
      if (!next(value)) return false;
      options.seconds = std::atof(value.c_str());
    } else if (arg == "--render-distance") {
      std::string value;
      if (!next(value)) return false;
      options.render_distance =
          static_cast<uint32_t>((std::max)(1, std::atoi(value.c_str())));
    } else if (arg == "--sensitivity") {
      std::string value;
      if (!next(value)) return false;
      options.sensitivity = static_cast<float>(std::atof(value.c_str()));
    } else if (arg == "--invert-y") {
      options.invert_y = true;
    } else if (arg == "--hidden") {
      options.hidden = true;
    } else if (arg == "--no-vsync") {
      options.vsync = false;
    } else if (arg == "--third-person") {
      options.third_person = true;
    } else if (arg == "--show-aabb") {
      options.show_aabb = true;
    } else if (arg == "--ui-spike") {
      options.ui_spike = true;
    } else if (arg == "--channel-spike") {
      options.channel_spike = true;
    } else if (arg == "--open-inventory") {
      options.open_inventory = true;
    } else if (arg == "--ui-scale") {
      std::string value;
      if (!next(value)) return false;
      options.ui_scale = static_cast<float>(std::atof(value.c_str()));
      if (options.ui_scale <= 0.0f) options.ui_scale = 2.0f;
    } else if (arg == "--ui-click") {
      std::string x, y;
      if (!next(x) || !next(y)) return false;
      options.ui_click_x = static_cast<float>(std::atof(x.c_str()));
      options.ui_click_y = static_cast<float>(std::atof(y.c_str()));
    } else if (arg == "--auto-walk") {
      std::string value;
      if (!next(value)) return false;
      options.auto_walk_seconds = std::atof(value.c_str());
    } else if (arg == "--auto-walk-ticks") {
      std::string value;
      if (!next(value)) return false;
      options.auto_walk_ticks =
          static_cast<uint64_t>(std::strtoull(value.c_str(), nullptr, 10));
    } else if (arg == "--exit-tick") {
      std::string value;
      if (!next(value)) return false;
      options.exit_tick =
          static_cast<uint64_t>(std::strtoull(value.c_str(), nullptr, 10));
    } else if (arg == "--spawn") {
      std::string x;
      std::string y;
      std::string z;
      if (!next(x) || !next(y) || !next(z)) return false;
      options.spawn_override = true;
      options.spawn_x = static_cast<float>(std::atof(x.c_str()));
      options.spawn_y = static_cast<float>(std::atof(y.c_str()));
      options.spawn_z = static_cast<float>(std::atof(z.c_str()));
    } else if (arg == "--spawn-yaw") {
      std::string value;
      if (!next(value)) return false;
      options.spawn_yaw = static_cast<float>(std::atof(value.c_str()));
    } else if (arg == "--spawn-pitch") {
      std::string value;
      if (!next(value)) return false;
      options.spawn_pitch = static_cast<float>(std::atof(value.c_str()));
    } else if (arg == "--scenario") {
      if (!next(options.scenario)) return false;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

const char* result_name(int32_t result) {
  switch (result) {
    case REIM_OK: return "REIM_OK";
    case REIM_ERR_INVALID_ARGUMENT: return "REIM_ERR_INVALID_ARGUMENT";
    case REIM_ERR_ABI_MISMATCH: return "REIM_ERR_ABI_MISMATCH";
    case REIM_ERR_NOT_FOUND: return "REIM_ERR_NOT_FOUND";
    case REIM_ERR_IO: return "REIM_ERR_IO";
    case REIM_ERR_PARSE: return "REIM_ERR_PARSE";
    case REIM_ERR_VERSION_MISMATCH: return "REIM_ERR_VERSION_MISMATCH";
    case REIM_ERR_NO_WORLD: return "REIM_ERR_NO_WORLD";
    case REIM_ERR_NOT_STARTED: return "REIM_ERR_NOT_STARTED";
    case REIM_ERR_ALREADY_STARTED: return "REIM_ERR_ALREADY_STARTED";
    case REIM_ERR_INTERNAL: return "REIM_ERR_INTERNAL";
    default: return "REIM_ERR_UNKNOWN";
  }
}

void host_log(int32_t level, const char* category, const char* message) {
  reim::log::write(static_cast<reim::log::Level>(level), category ? category : "runtime",
                   message ? message : "");
}

/* ABI v2 requires a thread-safe alloc/free pair for host-owned frame blobs. */
void* REIM_CALL host_alloc(size_t size, size_t align) {
  if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
#if defined(_MSC_VER)
  return _aligned_malloc(size, align);
#else
  return std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
}

void REIM_CALL host_free(void* ptr) {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

std::string find_generated_dir(const Options& options) {
  if (!options.generated.empty()) return options.generated;
  const std::filesystem::path exe = exe_directory();
  const std::filesystem::path candidates[] = {
      std::filesystem::current_path() / "generated" / "26.3",
      exe / "generated" / "26.3",
      exe / ".." / ".." / ".." / "generated" / "26.3",
  };
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate / "blocks.json", ec)) {
      return candidate.lexically_normal().string();
    }
  }
  return {};
}

void rotate_log_if_needed(const std::filesystem::path& log_path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(log_path, ec);
  if (ec || size < 8u * 1024u * 1024u) return;
  const std::filesystem::path previous = log_path.string() + ".1";
  std::filesystem::remove(previous, ec);
  std::filesystem::rename(log_path, previous, ec);
}

bool capture_png(const std::string& path, int width, int height) {
  if (width <= 0 || height <= 0) return false;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  std::vector<uint8_t> flipped(pixels.size());
  const size_t stride = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    std::memcpy(flipped.data() + static_cast<size_t>(y) * stride,
                pixels.data() + static_cast<size_t>(height - 1 - y) * stride, stride);
  }
  std::string error;
  if (!reim::png::write_file(path, width, height, 4, flipped.data(), error)) {
    reim::log::error("preview", "screenshot failed: " + error);
    return false;
  }
  reim::log::info("preview", "screenshot written: " + path);
  return true;
}

#ifdef _WIN32
struct LoadedDll {
  HMODULE module = nullptr;
  const ReimRuntimeApi* api = nullptr;
};

bool open_dll(const std::string& path, LoadedDll& out) {
  out.module = ::LoadLibraryW(std::filesystem::path(path).wstring().c_str());
  if (!out.module) {
    reim::log::error("preview", std::format("LoadLibraryW failed for {} (error {})",
                                             path, ::GetLastError()));
    return false;
  }
  using GetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
  auto get_api =
      reinterpret_cast<GetApiFn>(::GetProcAddress(out.module, "reim_get_api"));
  if (!get_api) {
    reim::log::error("preview", "GetProcAddress(reim_get_api) failed");
    ::FreeLibrary(out.module);
    out.module = nullptr;
    return false;
  }
  out.api = get_api(REIM_ABI_VERSION);
  if (!out.api) {
    reim::log::error("preview",
                     std::format("reim_get_api rejected ABI {}", REIM_ABI_VERSION));
    ::FreeLibrary(out.module);
    out.module = nullptr;
    return false;
  }
  if (out.api->abi_version != REIM_ABI_VERSION ||
      out.api->struct_size < sizeof(ReimRuntimeApi)) {
    reim::log::error("preview", "DLL returned an incompatible API table");
    ::FreeLibrary(out.module);
    out.module = nullptr;
    out.api = nullptr;
    return false;
  }
  reim::log::info("preview",
                  std::format("DLL OK: {} (ABI {}, api struct {} bytes)", path,
                              out.api->abi_version, out.api->struct_size));
  return true;
}
#else
struct LoadedDll {
  void* module = nullptr;
  const ReimRuntimeApi* api = nullptr;
};
bool open_dll(const std::string&, LoadedDll&) {
  reim::log::error("preview", "DLL loading is implemented on Windows only");
  return false;
}
#endif

#ifdef _WIN32
int check_dll(const std::string& path) {
  LoadedDll dll;
  if (!open_dll(path, dll)) return 3;
  ::FreeLibrary(dll.module);
  return 0;
}
#else
int check_dll(const std::string&) { return 0; }
#endif

#ifdef _WIN32
int check_generated(const std::string& dll_path, const std::string& generated_path) {
  LoadedDll dll;
  if (!open_dll(dll_path, dll)) return 3;

  const std::wstring generated = std::filesystem::path(generated_path).wstring();
  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  // The contract check validates generated data only; a null world makes the
  // Runtime return REIM_ERR_NO_WORLD without a misleading "world not found".
  config.world_path = nullptr;
  config.generated_path = generated.c_str();
  config.render_distance = 6;
  config.mouse_sensitivity = 0.15f;
  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.alloc = &host_alloc;
  host.free = &host_free;
  host.log = &host_log;

  int32_t error = 0;
  ReimInstance* instance = dll.api->create(&config, &host, &error);
  if (instance) {
    reim::log::info("preview", "DLL create returned an instance");
    dll.api->destroy(instance);
    ::FreeLibrary(dll.module);
    return 0;
  }
  ::FreeLibrary(dll.module);
  if (error == REIM_ERR_NO_WORLD) {
    reim::log::info("preview",
                    "generated manifest accepted (world required for a full instance)");
    return 0;
  }
  reim::log::error("preview",
                   std::format("generated manifest rejected ({})", result_name(error)));
  return 6;
}
#else
int check_generated(const std::string&, const std::string&) { return 0; }
#endif

struct HostState {
  const ReimRuntimeApi* api = nullptr;
  ReimInstance* instance = nullptr;
  bool captured = false;
  bool first_cursor = true;
  double last_cursor_x = 0.0;
  double last_cursor_y = 0.0;
  bool third_person = false;
  bool debug_aabb = false;
  bool paused = false;
  bool show_hud = true;
  double auto_walk_seconds = 0.0;
  uint64_t auto_walk_ticks = 0;
  uint64_t auto_walk_ready_tick = 0;
  uint64_t last_audio_sequence = 0;
  bool auto_walk_started = false;
  bool auto_walk_finished = false;
  std::chrono::steady_clock::time_point auto_walk_start;
  ReimVec3 auto_walk_from{0.0f, 0.0f, 0.0f};
  float auto_walk_distance = 0.0f;
  /* v3 UI pointer mode: the Runtime decides what the pointer hits. */
  bool ui_pointer_mode = false;
  bool modal_screen = false;
  uint32_t last_screen = REIM_SCREEN_HUD;
  uint32_t ui_buttons = 0;
  double ui_cursor_x = 0.0;
  double ui_cursor_y = 0.0;
  uint64_t action_sequence = 0;
};

void submit_key(const HostState& state, int key, int action) {
  ReimInputEvent event{};
  event.struct_size = sizeof(event);
  event.type = REIM_INPUT_KEY;
  event.key = key;
  event.action = action;
  state.api->submit_input(state.instance, &event);
}

void play_audio(const ReimRenderFrame& frame, HostState& state) {
#ifdef _WIN32
  for (uint32_t i = 0; i < frame.audio_count; ++i) {
    const ReimAudioEvent& event = frame.audio_events[i];
    if (event.sequence <= state.last_audio_sequence) continue;
    state.last_audio_sequence = event.sequence;
    /* The A6 spike sound is synthesized; the resource pipeline (M3-I) will
     * deliver OGG assets through SOUND_OGG. */
    if (event.sound_id == 1) {
      reim::log::info("audio",
                      "spike sound #" + std::to_string(event.sequence) +
                          " (UI, 2D) -> Beep");
      std::thread([] { ::Beep(880, 45); }).detach();
    }
  }
#else
  (void)frame;
  (void)state;
#endif
}

void submit_pointer(HostState& state, double x, double y, uint32_t buttons) {
  state.ui_cursor_x = x;
  state.ui_cursor_y = y;
  state.ui_buttons = buttons;
  ReimActionEvent event{};
  event.struct_size = sizeof(event);
  event.type = REIM_ACTIONTYPE_POINTER;
  event.sequence = ++state.action_sequence;
  event.c = buttons;
  event.screen_x = static_cast<float>(x);
  event.screen_y = static_cast<float>(y);
  state.api->submit_action(state.instance, &event);
}

/* HUD-mode world intents (M3-D04): the Host only submits the START/STOP
 * pair; the Runtime owns target selection, progress and world changes. */
void submit_action(HostState& state, uint32_t type) {
  ReimActionEvent event{};
  event.struct_size = sizeof(event);
  event.type = type;
  event.sequence = ++state.action_sequence;
  event.screen_id = state.last_screen;
  state.api->submit_action(state.instance, &event);
}

void submit_wheel(HostState& state, double delta) {
  ReimActionEvent event{};
  event.struct_size = sizeof(event);
  event.type = REIM_ACTIONTYPE_WHEEL;
  event.sequence = ++state.action_sequence;
  event.screen_id = state.last_screen;
  event.wheel = static_cast<float>(delta);
  state.api->submit_action(state.instance, &event);
}

int map_key(int key) {
  switch (key) {
    case GLFW_KEY_W: return REIM_KEY_W;
    case GLFW_KEY_A: return REIM_KEY_A;
    case GLFW_KEY_S: return REIM_KEY_S;
    case GLFW_KEY_D: return REIM_KEY_D;
    case GLFW_KEY_SPACE: return REIM_KEY_SPACE;
    case GLFW_KEY_LEFT_SHIFT: return REIM_KEY_LEFT_SHIFT;
    case GLFW_KEY_LEFT_CONTROL: return REIM_KEY_LEFT_CONTROL;
    case GLFW_KEY_UP: return REIM_KEY_UP;
    case GLFW_KEY_DOWN: return REIM_KEY_DOWN;
    case GLFW_KEY_LEFT: return REIM_KEY_LEFT;
    case GLFW_KEY_RIGHT: return REIM_KEY_RIGHT;
    case GLFW_KEY_E: return REIM_KEY_E;
    case GLFW_KEY_Q: return REIM_KEY_Q;
    case GLFW_KEY_ENTER: return REIM_KEY_ENTER;
    case GLFW_KEY_TAB: return REIM_KEY_TAB;
    case GLFW_KEY_1: return REIM_KEY_1;
    case GLFW_KEY_2: return REIM_KEY_2;
    case GLFW_KEY_3: return REIM_KEY_3;
    case GLFW_KEY_4: return REIM_KEY_4;
    case GLFW_KEY_5: return REIM_KEY_5;
    case GLFW_KEY_6: return REIM_KEY_6;
    case GLFW_KEY_7: return REIM_KEY_7;
    case GLFW_KEY_8: return REIM_KEY_8;
    case GLFW_KEY_9: return REIM_KEY_9;
    default: return REIM_KEY_UNKNOWN;
  }
}

void release_mouse(GLFWwindow* window, HostState& state) {
  if (!state.captured) return;
  state.captured = false;
  state.first_cursor = true;
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void capture_mouse(GLFWwindow* window, HostState& state) {
  if (state.captured) return;
  state.captured = true;
  state.first_cursor = true;
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action,
                  int /*mods*/) {
  auto* state = static_cast<HostState*>(glfwGetWindowUserPointer(window));
  if (state == nullptr) return;

  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    /* The Runtime owns the pause/close state machine (M3-H5); the Host only
     * forwards the key and restores the mouse. */
    ReimInputEvent event{};
    event.struct_size = sizeof(event);
    event.type = REIM_INPUT_KEY;
    event.key = REIM_KEY_ESCAPE;
    event.action = REIM_ACTION_PRESS;
    state->api->submit_input(state->instance, &event);
    if (state->modal_screen || state->captured) {
      release_mouse(window, *state);
    } else {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    return;
  }
  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_F1) state->show_hud = !state->show_hud;
    if (key == GLFW_KEY_F3) {
      state->paused = !state->paused;
      state->api->set_simulation_paused(state->instance,
                                        state->paused ? uint8_t{1} : uint8_t{0});
      reim::log::info("preview", state->paused ? "simulation paused" : "simulation resumed");
    }
    if (key == GLFW_KEY_F5) state->third_person = !state->third_person;
    if (key == GLFW_KEY_F8) state->debug_aabb = !state->debug_aabb;
  }

  const int mapped = map_key(key);
  if (mapped == REIM_KEY_UNKNOWN) return;
  if (action != GLFW_PRESS && action != GLFW_RELEASE && action != GLFW_REPEAT) return;
  ReimInputEvent event{};
  event.struct_size = sizeof(event);
  event.type = REIM_INPUT_KEY;
  event.key = mapped;
  event.action = action == GLFW_RELEASE ? REIM_ACTION_RELEASE
                 : action == GLFW_REPEAT ? REIM_ACTION_REPEAT
                                         : REIM_ACTION_PRESS;
  state->api->submit_input(state->instance, &event);
}

void cursor_callback(GLFWwindow* window, double x, double y) {
  auto* state = static_cast<HostState*>(glfwGetWindowUserPointer(window));
  if (state == nullptr) return;
  if (state->ui_pointer_mode) {
    submit_pointer(*state, x, y, state->ui_buttons);
    return;
  }
  if (!state->captured) return;
  if (state->first_cursor) {
    state->first_cursor = false;
    state->last_cursor_x = x;
    state->last_cursor_y = y;
    return;
  }
  const float dx = static_cast<float>(x - state->last_cursor_x);
  const float dy = static_cast<float>(y - state->last_cursor_y);
  state->last_cursor_x = x;
  state->last_cursor_y = y;
  if (dx == 0.0f && dy == 0.0f) return;
  ReimInputEvent event{};
  event.struct_size = sizeof(event);
  event.type = REIM_INPUT_MOUSE_MOTION;
  event.dx = dx;
  event.dy = dy;
  state->api->submit_input(state->instance, &event);
}

void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
  auto* state = static_cast<HostState*>(glfwGetWindowUserPointer(window));
  if (state == nullptr || yoffset == 0.0) return;
  submit_wheel(*state, yoffset);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/) {
  auto* state = static_cast<HostState*>(glfwGetWindowUserPointer(window));
  if (state == nullptr) return;
  if (state->ui_pointer_mode) {
    uint32_t buttons = state->ui_buttons;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS) buttons |= REIM_POINTER_BUTTON_LEFT;
      else buttons &= ~REIM_POINTER_BUTTON_LEFT;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      if (action == GLFW_PRESS) buttons |= REIM_POINTER_BUTTON_RIGHT;
      else buttons &= ~REIM_POINTER_BUTTON_RIGHT;
    }
    submit_pointer(*state, state->ui_cursor_x, state->ui_cursor_y, buttons);
    return;
  }
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) capture_mouse(window, *state);
    submit_action(*state, action == GLFW_PRESS ? REIM_ACTIONTYPE_BREAK_START
                                               : REIM_ACTIONTYPE_BREAK_STOP);
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    submit_action(*state, action == GLFW_PRESS ? REIM_ACTIONTYPE_USE_START
                                               : REIM_ACTIONTYPE_USE_STOP);
  } else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
    submit_action(*state, REIM_ACTIONTYPE_PICK_BLOCK);
  }
}

void focus_callback(GLFWwindow* window, int focused) {
  auto* state = static_cast<HostState*>(glfwGetWindowUserPointer(window));
  if (state == nullptr) return;
  if (focused == GLFW_FALSE) release_mouse(window, *state);
}

void error_callback(int code, const char* description) {
  reim::log::error("glfw", std::format("error {}: {}", code,
                                       description ? description : ""));
}

/* S1-compatible OpenGL smoke window: no world, no DLL, just a clear frame.
 * Kept for tools\s1-smoke.ps1, which launches the host without a save. */
int run_empty_window(const Options& options) {
  glfwSetErrorCallback(&error_callback);
  if (!glfwInit()) {
    reim::log::error("preview", "glfwInit failed");
    return 7;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);
  GLFWwindow* window =
      glfwCreateWindow(1280, 720, "REIMPreview - OpenGL smoke", nullptr, nullptr);
  if (!window) {
    reim::log::error("preview", "glfwCreateWindow failed (OpenGL 3.3 Core unavailable)");
    glfwTerminate();
    return 7;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(options.vsync ? 1 : 0);
  std::string gl_error;
  if (!load_gl_functions(gl_error)) {
    reim::log::error("preview", gl_error);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 7;
  }
  reim::log::info("preview",
                  std::format("OpenGL: {} | {}",
                              reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                              reinterpret_cast<const char*>(glGetString(GL_RENDERER))));

  const auto start = std::chrono::steady_clock::now();
  int frames = 0;
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.07f, 0.10f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glfwSwapBuffers(window);
    ++frames;

    bool stop = false;
    if (options.frames > 0 && frames >= options.frames) stop = true;
    if (options.seconds > 0.0) {
      const double elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      if (elapsed >= options.seconds) stop = true;
    }
    if (stop) break;
  }

  if (!options.screenshot.empty()) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    capture_png(options.screenshot, width, height);
  }
  reim::log::info("preview", std::format("empty-window smoke exiting after {} frames",
                                          frames));
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void draw_hud(reim::preview::Hud& hud, const ReimRenderFrame& frame,
              const HostState& state,
              const reim::preview::RenderStats& render_stats, float fps, int width,
              int height) {
  const uint32_t white = 0xFFFFFFFF;
  const uint32_t dim = 0xFFB8C4D0;
  const uint32_t accent = 0xFF77E0A0;
  const uint32_t warn = 0xFFFFCF66;
  const float scale = 2.0f;
  const float line_height = 18.0f;
  const float panel_x = 12.0f;
  float y = 14.0f;

  hud.rect(8.0f, 8.0f, 920.0f, 8.0f * line_height + 12.0f, 0x99000000);

  if (!frame.ready) {
    hud.text(panel_x, y, 3.0f, warn, "LOADING WORLD");
    y += 30.0f;
    hud.text(panel_x, y, scale, white,
             std::format("{} / {} CHUNKS  {}%", frame.loaded_chunks,
                         frame.total_chunks, frame.loading_percent));
    y += line_height;
    hud.text(panel_x, y, scale, dim, "REAL 26.3 SAVE - RUNTIME LOADING ON ITS THREAD");
    return;
  }

  const double frames_per_second = static_cast<double>(fps);
  hud.text(panel_x, y, scale, accent,
           std::format("FPS {:.1f}   TPS {:.1f}   FRAME {:.1f} MS", frames_per_second,
                       frame.measured_tps, frame.measured_frame_ms));
  y += line_height;
  // loaded_chunks includes the streaming halo, so it can exceed the initial
  // total; the initial total is still shown on the loading screen.
  hud.text(panel_x, y, scale, white,
           std::format("TICK {}   CHUNKS {}   LOAD {}%", frame.tick,
                       frame.loaded_chunks, frame.loading_percent));
  y += line_height;
  hud.text(panel_x, y, scale, white,
           std::format("POS {:.2f} {:.2f} {:.2f}   YAW {:.0f} PITCH {:.0f}",
                       frame.player.position.x, frame.player.position.y,
                       frame.player.position.z, frame.player.yaw,
                       frame.player.pitch));
  y += line_height;
  hud.text(panel_x, y, scale, white,
           std::format("SECTIONS {} MESHED   BLOCKS {}   {}",
                       frame.mesh_sections, frame.block_count,
                       (frame.player.flags & REIM_PLAYER_ON_GROUND) ? "GROUND" : "AIR"));
  y += line_height;
  hud.text(panel_x, y, scale, white,
           std::format("DRAW {}   TRIS {}   CULLED {}   MESHES {}", render_stats.draw_calls,
                       render_stats.triangles, render_stats.culled_sections,
                       render_stats.meshes));
  y += line_height;
  hud.text(panel_x, y, scale, dim,
           std::format("ATLAS {}X{}   {}   {}{}", render_stats.atlas_width,
                       render_stats.atlas_height,
                       state.third_person ? "THIRD PERSON" : "FIRST PERSON",
                       state.debug_aabb ? "AABB " : "",
                       state.paused ? "PAUSED" : ""));
  y += line_height;
  hud.text(panel_x, y, scale, dim,
           "WASD MOVE  SPACE JUMP  SHIFT SNEAK  CTRL SPRINT  MOUSE/ARROWS LOOK");
  y += line_height;
  hud.text(panel_x, y, scale, dim,
           "CLICK CAPTURE  ESC RELEASE/QUIT  F1 HUD  F3 PAUSE  F5 VIEW  F8 AABB");

  if (!state.third_person) {
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    hud.rect(cx - 1.0f, cy - 7.0f, 2.0f, 14.0f, 0xD0FFFFFF);
    hud.rect(cx - 7.0f, cy - 1.0f, 14.0f, 2.0f, 0xD0FFFFFF);
  }
}

void write_report(const std::string& path, const ReimRenderFrame& frame,
                  const reim::preview::RenderStats& render_stats, uint64_t frames,
                  double elapsed_seconds, double average_fps, const std::string& screenshot,
                  const std::string& exit_reason, float auto_walk_distance,
                  const std::string& status_json) {
  if (path.empty()) return;
  std::error_code ec;
  const std::filesystem::path report_path(path);
  if (report_path.has_parent_path()) {
    std::filesystem::create_directories(report_path.parent_path(), ec);
  }
  std::ofstream out(report_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    reim::log::error("preview", "cannot write report: " + path);
    return;
  }
  out << "{\n";
  out << "  \"stage\": \"S4-preview\",\n";
  out << "  \"ready\": " << (frame.ready ? "true" : "false") << ",\n";
  out << "  \"exit_reason\": \"" << exit_reason << "\",\n";
  out << "  \"frames\": " << frames << ",\n";
  out << "  \"elapsed_seconds\": " << elapsed_seconds << ",\n";
  out << "  \"average_fps\": " << average_fps << ",\n";
  out << "  \"tick\": " << frame.tick << ",\n";
  out << "  \"measured_tps\": " << frame.measured_tps << ",\n";
  out << "  \"loaded_chunks\": " << frame.loaded_chunks << ",\n";
  out << "  \"total_chunks\": " << frame.total_chunks << ",\n";
  out << "  \"loading_percent\": " << frame.loading_percent << ",\n";
  out << "  \"loaded_sections\": " << frame.loaded_sections << ",\n";
  out << "  \"mesh_sections\": " << frame.mesh_sections << ",\n";
  out << "  \"block_count\": " << frame.block_count << ",\n";
  out << "  \"draw_calls\": " << render_stats.draw_calls << ",\n";
  out << "  \"triangles\": " << render_stats.triangles << ",\n";
  out << "  \"meshes\": " << render_stats.meshes << ",\n";
  out << "  \"culled_sections\": " << render_stats.culled_sections << ",\n";
  out << "  \"atlas\": {\"width\": " << render_stats.atlas_width
      << ", \"height\": " << render_stats.atlas_height << "},\n";
  out << "  \"player\": {\"x\": " << frame.player.position.x
      << ", \"y\": " << frame.player.position.y << ", \"z\": " << frame.player.position.z
      << ", \"yaw\": " << frame.player.yaw << ", \"pitch\": " << frame.player.pitch
      << ", \"flags\": " << frame.player.flags << "},\n";
  out << "  \"third_person_distance\": " << frame.third_person_distance << ",\n";
  out << "  \"auto_walk_distance\": " << auto_walk_distance << ",\n";
  out << "  \"screenshot\": \"" << json_escape(screenshot) << "\",\n";
  out << "  \"status\": " << (status_json.empty() ? std::string("{}") : status_json)
      << "\n";
  out << "}\n";
  out.flush();
  if (!out) {
    reim::log::error("preview", "report write failed: " + path);
    return;
  }
  reim::log::info("preview", "report written: " + path);
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
  if (options.version) {
    std::printf("REIMPreview S4 (ABI %u)\n", REIM_ABI_VERSION);
    return 0;
  }

  std::string dll_path = options.dll;
  if (dll_path.empty()) {
    dll_path = (std::filesystem::path(exe_directory()) / "mc_26.3.dll").string();
  }

#ifdef _WIN32
  std::FILE* log_file = nullptr;
  {
    const std::filesystem::path logs = std::filesystem::path(exe_directory()) / "logs";
    std::error_code ec;
    std::filesystem::create_directories(logs, ec);
    if (!ec) {
      const std::filesystem::path log_path = logs / "preview.log";
      rotate_log_if_needed(log_path);
      log_file = _wfopen(log_path.wstring().c_str(), L"a");
    }
  }
  reim::log::set_sink([log_file](reim::log::Level level, std::string_view category,
                                 std::string_view message) {
    static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    const int index = std::clamp(static_cast<int>(level), 0, 4);
    std::printf("[%s] %.*s: %.*s\n", names[index], static_cast<int>(category.size()),
                category.data(), static_cast<int>(message.size()), message.data());
    std::fflush(stdout);
    if (log_file) {
      std::fprintf(log_file, "[%s] %.*s: %.*s\n", names[index],
                   static_cast<int>(category.size()), category.data(),
                   static_cast<int>(message.size()), message.data());
      std::fflush(log_file);
    }
  });
#endif

  reim::log::info("preview", std::format("REIMPreview S4 starting (ABI {})",
                                          REIM_ABI_VERSION));

  if (!options.check_dll_path.empty()) {
    const int result = check_dll(options.check_dll_path);
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return result;
  }
  if (!options.check_generated_dir.empty()) {
    const int result = check_generated(dll_path, options.check_generated_dir);
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return result;
  }

  if (options.world.empty()) {
    // No save given: keep the S1 OpenGL smoke path (no DLL, no world). If the
    // caller supplied runtime paths, treat the missing --world as an error.
    if (!options.generated.empty() || !options.minecraft_dir.empty()) {
      reim::log::error("preview", "--world is required when --generated/--minecraft-dir is set");
      print_usage();
#ifdef _WIN32
      if (log_file) std::fclose(log_file);
#endif
      return 2;
    }
    const int result = run_empty_window(options);
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return result;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(options.world, ec) ||
      !std::filesystem::is_regular_file(std::filesystem::path(options.world) / "level.dat",
                                        ec)) {
    reim::log::error("preview", "world not found (level.dat missing): " + options.world);
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return 2;
  }
  const std::string generated = find_generated_dir(options);
  if (generated.empty() ||
      !std::filesystem::is_regular_file(std::filesystem::path(generated) / "blocks.json",
                                        ec)) {
    reim::log::error("preview",
                     "generated data not found (blocks.json); pass --generated DIR");
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return 2;
  }

  LoadedDll dll;
  if (!open_dll(dll_path, dll)) {
#ifdef _WIN32
    if (log_file) std::fclose(log_file);
#endif
    return 3;
  }

  glfwSetErrorCallback(&error_callback);
  if (!glfwInit()) {
    reim::log::error("preview", "glfwInit failed");
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 7;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);

  GLFWwindow* window =
      glfwCreateWindow(1280, 720, "REIMPreview - 26.3", nullptr, nullptr);
  if (!window) {
    reim::log::error("preview",
                     "glfwCreateWindow failed (OpenGL 3.3 Core unavailable)");
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 7;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(options.vsync ? 1 : 0);

  std::string gl_error;
  if (!load_gl_functions(gl_error)) {
    reim::log::error("preview", gl_error);
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 7;
  }
  reim::log::info("preview",
                  std::format("OpenGL: {} | {}",
                              reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                              reinterpret_cast<const char*>(glGetString(GL_RENDERER))));

  reim::preview::Renderer renderer;
  std::string render_error;
  reim::preview::UiCanvas ui_canvas;
  reim::preview::Hud hud;
  std::string hud_error;
  if (!hud.initialize(hud_error)) {
    reim::log::error("preview", hud_error);
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 7;
  }

  const std::wstring world_path = std::filesystem::path(options.world).wstring();
  const std::wstring generated_path = std::filesystem::path(generated).wstring();
  std::wstring minecraft_path;
  if (!options.minecraft_dir.empty()) {
    minecraft_path = std::filesystem::path(options.minecraft_dir).wstring();
  }

  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.alloc = &host_alloc;
  host.free = &host_free;
  host.log = &host_log;

  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  config.world_path = world_path.c_str();
  config.generated_path = generated_path.c_str();
  config.minecraft_dir = minecraft_path.empty() ? nullptr : minecraft_path.c_str();
  config.render_distance = options.render_distance;
  config.render_level = 2; /* REIMPreview consumes the full Runtime frame set. */
  config.flags = 0;
  if (options.ui_spike) config.flags |= REIM_CONFIG_FLAG_UI_SPIKE;
  if (options.channel_spike) config.flags |= REIM_CONFIG_FLAG_CHANNEL_SPIKE;
  config.ui_scale = static_cast<uint32_t>(options.ui_scale);
  config.mouse_sensitivity = options.sensitivity;
  config.invert_y = options.invert_y ? 1u : 0u;
  if (options.spawn_override) {
    config.flags |= REIM_CONFIG_FLAG_SPAWN_OVERRIDE;
    config.spawn_x = options.spawn_x;
    config.spawn_y = options.spawn_y;
    config.spawn_z = options.spawn_z;
    config.spawn_yaw = options.spawn_yaw;
    config.spawn_pitch = options.spawn_pitch;
  }
  std::string scenario_text;
  if (!options.scenario.empty()) {
    std::ifstream stream(options.scenario, std::ios::binary);
    if (!stream) {
      reim::log::error("preview", "cannot read scenario: " + options.scenario);
      return 2;
    }
    scenario_text.assign(std::istreambuf_iterator<char>(stream),
                         std::istreambuf_iterator<char>());
    const auto scenario_json = nlohmann::json::parse(
        scenario_text.begin(), scenario_text.end(), nullptr, false);
    if (!scenario_json.is_discarded() && scenario_json.is_object()) {
      config.create_world = scenario_json.value("create_world", false) ? 1u : 0u;
      config.seed = scenario_json.value("seed", 0ull);
      if (scenario_json.contains("spawn") &&
          scenario_json["spawn"].is_array() &&
          scenario_json["spawn"].size() >= 3) {
        config.flags |= REIM_CONFIG_FLAG_SPAWN_OVERRIDE;
        config.spawn_x = scenario_json["spawn"][0].get<float>();
        config.spawn_y = scenario_json["spawn"][1].get<float>();
        config.spawn_z = scenario_json["spawn"][2].get<float>();
        config.spawn_yaw = scenario_json.value("spawn_yaw", 0.0f);
        config.spawn_pitch = scenario_json.value("spawn_pitch", 0.0f);
      }
    }
    config.scenario = scenario_text.c_str();
  }

  int32_t create_error = 0;
  ReimInstance* instance = dll.api->create(&config, &host, &create_error);
  if (!instance) {
    reim::log::error("preview", std::format("create failed: {} ({})",
                                             result_name(create_error), create_error));
    hud.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 5;
  }

  /* Prefer the ABI v2 atlas asset; the generated/ file is only a diagnostic
   * fallback if the Runtime cannot deliver it. */
  const void* atlas_bytes = nullptr;
  uint64_t atlas_size = 0;
  uint64_t atlas_revision = 0;
  const bool have_asset =
      dll.api->get_asset(instance, REIM_ASSET_ATLAS_PNG, 0, &atlas_bytes,
                         &atlas_size, &atlas_revision) == REIM_OK &&
      atlas_bytes != nullptr && atlas_size != 0;
  const bool atlas_ok =
      have_asset ? renderer.initialize_from_png(
                       static_cast<const uint8_t*>(atlas_bytes),
                       static_cast<size_t>(atlas_size),
                       std::format("get_asset(ATLAS_PNG, revision {})", atlas_revision),
                       render_error)
                 : renderer.initialize(generated, render_error);
  if (!atlas_ok) {
    reim::log::error("preview", render_error);
    dll.api->destroy(instance);
    hud.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 7;
  }
  if (!have_asset) {
    reim::log::warn("preview",
                    "get_asset(ATLAS_PNG) failed; using generated/atlas.png fallback");
  }

  /* v3 UI canvas needs the Runtime font atlas and the block atlas. Both are
   * Runtime-owned assets; UiCanvas copies them into GL textures. */
  const void* font_bytes = nullptr;
  uint64_t font_size = 0;
  uint64_t font_revision = 0;
  const bool have_font =
      dll.api->get_asset(instance, REIM_ASSET_FONT_ATLAS, 0, &font_bytes,
                         &font_size, &font_revision) == REIM_OK &&
      font_bytes != nullptr && font_size != 0;
  if (have_asset && have_font) {
    std::string ui_error;
    if (!ui_canvas.initialize(static_cast<const uint8_t*>(atlas_bytes),
                              static_cast<size_t>(atlas_size),
                              static_cast<const uint8_t*>(font_bytes),
                              static_cast<size_t>(font_size), ui_error)) {
      reim::log::error("preview", ui_error);
      dll.api->destroy(instance);
      hud.shutdown();
      renderer.shutdown();
      glfwDestroyWindow(window);
      glfwTerminate();
      return 8;
    }
  } else if (options.ui_spike) {
    reim::log::error("preview", "UI spike requested but atlas/font assets are missing");
    dll.api->destroy(instance);
    hud.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 8;
  }

  const void* model_bytes = nullptr;
  uint64_t model_size = 0;
  uint64_t model_revision = 0;
  if (dll.api->get_asset(instance, REIM_ASSET_MODEL_DEFS_JSON, 0, &model_bytes,
                         &model_size, &model_revision) == REIM_OK &&
      model_bytes != nullptr && model_size != 0) {
    std::string model_error;
    if (!renderer.load_model_defs(model_bytes, static_cast<size_t>(model_size),
                                  model_error)) {
      reim::log::warn("preview", "model defs rejected: " + model_error);
    }
  }

  {
    const void* item_png = nullptr;
    uint64_t item_png_size = 0;
    uint64_t item_png_revision = 0;
    const void* item_meta = nullptr;
    uint64_t item_meta_size = 0;
    uint64_t item_meta_revision = 0;
    const void* item_index = nullptr;
    uint64_t item_index_size = 0;
    uint64_t item_index_revision = 0;
    if (dll.api->get_asset(instance, REIM_ASSET_ITEM_ATLAS_PNG, 0, &item_png,
                           &item_png_size, &item_png_revision) == REIM_OK &&
        dll.api->get_asset(instance, REIM_ASSET_ITEM_ATLAS_META, 0, &item_meta,
                           &item_meta_size, &item_meta_revision) == REIM_OK &&
        dll.api->get_asset(instance, REIM_ASSET_ITEM_INDEX_JSON, 0, &item_index,
                           &item_index_size, &item_index_revision) == REIM_OK &&
        item_png != nullptr && item_meta != nullptr && item_index != nullptr) {
      std::string item_error;
      if (!renderer.load_item_assets(item_png, static_cast<size_t>(item_png_size),
                                     item_meta, static_cast<size_t>(item_meta_size),
                                     item_index, static_cast<size_t>(item_index_size),
                                     item_error)) {
        reim::log::warn("preview", "item atlas rejected: " + item_error);
      }
    }
  }

  const int32_t start_result = dll.api->start(instance);
  if (start_result != REIM_OK) {
    reim::log::error("preview", std::format("start failed: {} ({})",
                                             result_name(start_result), start_result));
    dll.api->destroy(instance);
    hud.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    ::FreeLibrary(dll.module);
    if (log_file) std::fclose(log_file);
#endif
    return 5;
  }

  HostState state;
  state.api = dll.api;
  state.instance = instance;
  state.third_person = options.third_person;
  state.debug_aabb = options.show_aabb;
  state.ui_pointer_mode = options.ui_spike;
  state.auto_walk_seconds = options.auto_walk_seconds;
  state.auto_walk_ticks = options.auto_walk_ticks;
  if (state.ui_pointer_mode) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
  glfwSetWindowUserPointer(window, &state);
  glfwSetKeyCallback(window, &key_callback);
  glfwSetCursorPosCallback(window, &cursor_callback);
  glfwSetMouseButtonCallback(window, &mouse_button_callback);
  glfwSetScrollCallback(window, &scroll_callback);
  glfwSetWindowFocusCallback(window, &focus_callback);

  glEnable(GL_DEPTH_TEST);
  int fb_width = 0;
  int fb_height = 0;
  glfwGetFramebufferSize(window, &fb_width, &fb_height);

  const auto start_time = std::chrono::steady_clock::now();
  auto fps_window_start = start_time;
  uint64_t fps_window_frames = 0;
  double fps = 0.0;
  auto next_title_update = start_time;
  auto next_stats_log = start_time + std::chrono::seconds(5);
  auto next_stats_frame = start_time + std::chrono::milliseconds(500);
  uint64_t frames = 0;
  bool ready_logged = false;
  bool ui_click_sent = false;
  bool inventory_key_sent = false;
  bool screenshot_written = false;
  std::string exit_reason = "window_closed";
  ReimRenderFrame last_frame{};
  last_frame.struct_size = sizeof(last_frame);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    glfwGetFramebufferSize(window, &fb_width, &fb_height);

    ReimRenderFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    const int32_t result = dll.api->fetch_frame(instance, &frame);
    if (result != REIM_OK) {
      reim::log::error("preview", std::format("fetch_frame failed: {} ({})",
                                               result_name(result), result));
      exit_reason = "fetch_frame_failed";
      break;
    }
    last_frame = frame;
    renderer.apply_frame(frame);
    dll.api->set_ui_canvas(instance, static_cast<uint32_t>(fb_width),
                           static_cast<uint32_t>(fb_height), options.ui_scale);

    /* The Runtime owns screen state; the Host switches capture/pointer mode
     * from the published screen id instead of deciding it itself (M3-H5). */
    state.last_screen = frame.ui.screen_id;
    state.modal_screen =
        frame.ui.screen_id == REIM_SCREEN_INVENTORY ||
        frame.ui.screen_id == REIM_SCREEN_CRAFTING_TABLE ||
        frame.ui.screen_id == REIM_SCREEN_FURNACE ||
        frame.ui.screen_id == REIM_SCREEN_CHEST ||
        frame.ui.screen_id == REIM_SCREEN_PAUSE ||
        frame.ui.screen_id == REIM_SCREEN_DEATH;
    state.ui_pointer_mode = options.ui_spike || state.modal_screen;
    if (state.modal_screen && state.captured) release_mouse(window, state);
    if (state.modal_screen) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    reim::preview::RenderOptions render_options;
    render_options.third_person = state.third_person;
    render_options.debug_player_aabb = state.debug_aabb;
    renderer.render(frame, render_options, fb_width, fb_height);
    play_audio(frame, state);

    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - start_time).count();
    ++frames;
    ++fps_window_frames;
    const double fps_elapsed =
        std::chrono::duration<double>(now - fps_window_start).count();
    if (fps_elapsed >= 0.5) {
      fps = static_cast<double>(fps_window_frames) / fps_elapsed;
      fps_window_frames = 0;
      fps_window_start = now;
    }

    if (state.show_hud) {
      hud.begin(fb_width, fb_height);
      draw_hud(hud, frame, state, renderer.stats(), static_cast<float>(fps), fb_width,
               fb_height);
      hud.end();
    }
    /* v3 UI draws on top of the diagnostic HUD. The blob must still be alive:
     * release happens after this point. */
    if (frame.ui.draw_count != 0) {
      ui_canvas.draw(frame.ui, fb_width, fb_height);
    }
    dll.api->release_frame(instance, frame.frame_id);

    if (frame.ready && !ready_logged) {
      ready_logged = true;
      reim::log::info("preview",
                      std::format("world ready: {} chunks, {} meshed sections, spawn "
                                  "({:.2f},{:.2f},{:.2f})",
                                  frame.loaded_chunks, frame.mesh_sections,
                                  frame.player.position.x, frame.player.position.y,
                                  frame.player.position.z));
    }
    if (frame.ready && !ui_click_sent && options.ui_click_x >= 0.0f &&
        options.ui_click_y >= 0.0f) {
      ui_click_sent = true;
      submit_pointer(state, options.ui_click_x, options.ui_click_y,
                     REIM_POINTER_BUTTON_LEFT);
      reim::log::info("preview",
                      std::format("UI click submitted at {},{}",
                                  options.ui_click_x, options.ui_click_y));
    }
    if (frame.ready && options.open_inventory && !inventory_key_sent) {
      inventory_key_sent = true;
      submit_key(state, REIM_KEY_E, REIM_ACTION_PRESS);
      reim::log::info("preview", "inventory key submitted (diagnostic)");
    }

    // Optional automation: prove the Host -> ABI input path moves the player.
    if ((state.auto_walk_seconds > 0.0 || state.auto_walk_ticks > 0) && frame.ready) {
      if (!state.auto_walk_started) {
        state.auto_walk_started = true;
        state.auto_walk_start = now;
        state.auto_walk_ready_tick = frame.tick;
        state.auto_walk_from = frame.player.position;
        submit_key(state, REIM_KEY_W, REIM_ACTION_PRESS);
        reim::log::info("preview",
                        std::format("auto-walk: holding W from ({:.2f},{:.2f},{:.2f}) "
                                    "at tick {}",
                                    state.auto_walk_from.x, state.auto_walk_from.y,
                                    state.auto_walk_from.z, frame.tick));
      } else if (!state.auto_walk_finished) {
        const float dx = frame.player.position.x - state.auto_walk_from.x;
        const float dz = frame.player.position.z - state.auto_walk_from.z;
        state.auto_walk_distance = std::sqrt(dx * dx + dz * dz);
        const double walked =
            std::chrono::duration<double>(now - state.auto_walk_start).count();
        const bool by_ticks = state.auto_walk_ticks > 0 &&
                              frame.tick >= state.auto_walk_ready_tick +
                                                 state.auto_walk_ticks;
        const bool by_seconds = state.auto_walk_ticks == 0 &&
                                walked >= state.auto_walk_seconds;
        if (by_ticks || by_seconds) {
          state.auto_walk_finished = true;
          submit_key(state, REIM_KEY_W, REIM_ACTION_RELEASE);
          reim::log::info(
              "preview",
              std::format("auto-walk complete at tick {}: {:.2f} blocks horizontally "
                          "in {:.2f}s",
                          frame.tick, state.auto_walk_distance, walked));
        }
      }
    }

    if (now >= next_stats_log) {
      next_stats_log = now + std::chrono::seconds(5);
      reim::log::info(
          "stats",
          std::format("fps={:.1f} tps={:.1f} tick={} pos=({:.2f},{:.2f},{:.2f}) "
                      "chunks={} sections={} draws={} tris={} {}",
                      fps, frame.measured_tps, frame.tick, frame.player.position.x,
                      frame.player.position.y, frame.player.position.z,
                      frame.loaded_chunks, frame.mesh_sections,
                      renderer.stats().draw_calls, renderer.stats().triangles,
                      state.paused ? "paused" : "running"));
    }
    if (now >= next_stats_frame || now >= next_title_update) {
      next_stats_frame = now + std::chrono::milliseconds(500);
      next_title_update = now + std::chrono::milliseconds(500);
      if (!options.hidden) {
        glfwSetWindowTitle(
            window,
            std::format("REIMPreview - 26.3 | {:.0f} FPS | {:.1f} TPS | tick {} | {}",
                        fps, frame.measured_tps, frame.tick,
                        frame.ready ? "ready" : "loading")
                .c_str());
      }
    }

    bool last = false;
    if (options.frames > 0 && frames >= static_cast<uint64_t>(options.frames)) {
      last = true;
      exit_reason = "frame_limit";
    }
    if (options.seconds > 0.0 && elapsed >= options.seconds) {
      last = true;
      exit_reason = "time_limit";
    }
    if (options.exit_tick > 0 && frame.tick >= options.exit_tick) {
      last = true;
      exit_reason = "tick_limit";
    }
    if (glfwWindowShouldClose(window)) {
      last = true;
      exit_reason = "window_closed";
    }

    if (last && !options.screenshot.empty() && !screenshot_written) {
      screenshot_written = capture_png(options.screenshot, fb_width, fb_height);
    }
    glfwSwapBuffers(window);
    if (last) break;
  }

  const auto end_time = std::chrono::steady_clock::now();
  const double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
  if (!screenshot_written && !options.screenshot.empty()) {
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    screenshot_written = capture_png(options.screenshot, fb_width, fb_height);
  }

  if (state.auto_walk_started && !state.auto_walk_finished) {
    state.auto_walk_finished = true;
    submit_key(state, REIM_KEY_W, REIM_ACTION_RELEASE);
    const float dx = last_frame.player.position.x - state.auto_walk_from.x;
    const float dz = last_frame.player.position.z - state.auto_walk_from.z;
    state.auto_walk_distance = std::sqrt(dx * dx + dz * dz);
  }

  /* G1: freeze the simulation before exporting state so both hosts report the
   * same tick and the Runtime's status JSON describes that frame. */
  if (options.exit_tick > 0) {
    dll.api->set_simulation_paused(instance, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ReimRenderFrame frozen{};
    frozen.struct_size = sizeof(frozen);
    frozen.abi_version = REIM_ABI_VERSION;
    if (dll.api->fetch_frame(instance, &frozen) == REIM_OK) {
      last_frame = frozen;
      dll.api->release_frame(instance, frozen.frame_id);
      last_frame.updates = nullptr;
      last_frame.removes = nullptr;
      last_frame.owned_block = nullptr;
    }
  }

  const double average_fps =
      elapsed_seconds > 0.0 ? static_cast<double>(frames) / elapsed_seconds : 0.0;
  reim::log::info("preview",
                  std::format("exiting after {} frames in {:.2f}s ({:.1f} avg FPS) [{}]",
                              frames, elapsed_seconds, average_fps, exit_reason));

  std::string status_json;
  const int32_t status_needed = dll.api->get_status_json(instance, nullptr, 0);
  if (status_needed > 0) {
    std::vector<char> status_buffer(static_cast<size_t>(status_needed) + 1, '\0');
    const int32_t status_written =
        dll.api->get_status_json(instance, status_buffer.data(),
                                 static_cast<uint32_t>(status_buffer.size()));
    if (status_written > 0) {
      status_json.assign(status_buffer.data(), static_cast<size_t>(status_written));
    }
  }

  write_report(options.report, last_frame, renderer.stats(), frames, elapsed_seconds,
               average_fps, screenshot_written ? options.screenshot : std::string(),
               exit_reason, state.auto_walk_distance, status_json);

  release_mouse(window, state);
  dll.api->stop(instance);
  dll.api->destroy(instance);
  hud.shutdown();
  ui_canvas.shutdown();
  renderer.shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
#ifdef _WIN32
  ::FreeLibrary(dll.module);
  if (log_file) std::fclose(log_file);
#endif

  if (exit_reason == "fetch_frame_failed") return 8;
  if (!last_frame.ready) return 9;
  return 0;
}
