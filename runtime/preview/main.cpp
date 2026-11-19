/*
 * REIMPreview.exe, S1 skeleton.
 *
 * M1-A deliverable: a GLFW + OpenGL 3.3 Core window that opens and exits
 * cleanly, plus a DLL ABI smoke path (LoadLibrary + GetProcAddress). This is
 * the development-only Host; it contains no world or renderer logic yet.
 *
 * Optional automation flags:
 *   --frames N        close after N presented frames
 *   --seconds N       close after N seconds
 *   --hidden          do not show the window
 *   --check-dll PATH  load the DLL and validate reim_get_api without a window
 *   --screenshot PATH write the last framebuffer to a PNG (development aid)
 */
#include <GLFW/glfw3.h>

#include "reim_abi.h"
#include "util/log.h"
#include "util/png.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Options {
  int frames = 0;
  double seconds = 0.0;
  bool hidden = false;
  std::string dll_path;
  std::string check_dll_path;
  std::string screenshot_path;
  std::string generated_path;
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
      "REIMPreview (S1 skeleton)\n"
      "  --frames N         close after N frames\n"
      "  --seconds N        close after N seconds\n"
      "  --hidden           create an invisible window\n"
      "  --dll PATH         DLL to ABI-check (default: mc_26.3.dll beside the exe)\n"
      "  --check-dll PATH   check LoadLibrary + reim_get_api without opening a window\n"
      "  --check-generated DIR  validate generated/26.3 through DLL create\n"
      "  --screenshot PATH  save the last frame as PNG\n"
      "  --help, --version\n");
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
    } else if (arg == "--frames") {
      std::string value;
      if (!next(value)) return false;
      options.frames = std::atoi(value.c_str());
    } else if (arg == "--seconds") {
      std::string value;
      if (!next(value)) return false;
      options.seconds = std::atof(value.c_str());
    } else if (arg == "--hidden") {
      options.hidden = true;
    } else if (arg == "--dll") {
      if (!next(options.dll_path)) return false;
    } else if (arg == "--check-dll") {
      if (!next(options.check_dll_path)) return false;
    } else if (arg == "--screenshot") {
      if (!next(options.screenshot_path)) return false;
    } else if (arg == "--check-generated") {
      if (!next(options.generated_path)) return false;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

void console_log(reim::log::Level level, std::string_view category,
                 std::string_view message) {
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  const int index = static_cast<int>(level);
  std::fprintf(stdout, "[%s] %.*s: %.*s\n",
               names[index < 0 ? 0 : (index > 4 ? 4 : index)],
               static_cast<int>(category.size()), category.data(),
               static_cast<int>(message.size()), message.data());
  std::fflush(stdout);
}

#ifdef _WIN32
int check_dll(const std::string& path) {
  const std::wstring wide = std::filesystem::path(path).wstring();
  HMODULE module = ::LoadLibraryW(wide.c_str());
  if (!module) {
    reim::log::error("preview",
                     std::format("LoadLibraryW failed for {} (error {})", path,
                                 ::GetLastError()));
    return 3;
  }
  using GetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
  auto get_api = reinterpret_cast<GetApiFn>(
      ::GetProcAddress(module, "reim_get_api"));
  if (!get_api) {
    reim::log::error("preview", "GetProcAddress(reim_get_api) failed");
    ::FreeLibrary(module);
    return 4;
  }
  const ReimRuntimeApi* api = get_api(REIM_ABI_VERSION);
  if (!api) {
    reim::log::error("preview", "reim_get_api rejected REIM_ABI_VERSION");
    ::FreeLibrary(module);
    return 5;
  }
  if (api->abi_version != REIM_ABI_VERSION ||
      api->struct_size < sizeof(ReimRuntimeApi)) {
    reim::log::error("preview", "DLL returned an incompatible API table");
    ::FreeLibrary(module);
    return 6;
  }
  reim::log::info("preview",
                  std::format("DLL OK: {} (ABI {}, api struct {} bytes)", path,
                              api->abi_version, api->struct_size));
  ::FreeLibrary(module);
  return 0;
}
#else
int check_dll(const std::string& path) {
  reim::log::warn("preview", "DLL checks are implemented on Windows only: " + path);
  return 0;
}
#endif

#ifdef _WIN32
void log_to_file(reim::log::Level level, std::string_view category,
                 std::string_view message, std::FILE* file) {
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  const int index = static_cast<int>(level);
  std::fprintf(file, "[%s] %.*s: %.*s\n",
               names[index < 0 ? 0 : (index > 4 ? 4 : index)],
               static_cast<int>(category.size()), category.data(),
               static_cast<int>(message.size()), message.data());
  std::fflush(file);
}
#endif

#ifdef _WIN32
int check_generated(const std::string& dll_path, const std::string& generated_path) {
  const std::wstring wide_dll = std::filesystem::path(dll_path).wstring();
  HMODULE module = ::LoadLibraryW(wide_dll.c_str());
  if (!module) {
    reim::log::error("preview", std::format("LoadLibraryW failed for {} (error {})",
                                             dll_path, ::GetLastError()));
    return 3;
  }
  using GetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
  auto get_api = reinterpret_cast<GetApiFn>(::GetProcAddress(module, "reim_get_api"));
  if (!get_api) {
    reim::log::error("preview", "GetProcAddress(reim_get_api) failed");
    ::FreeLibrary(module);
    return 4;
  }
  const ReimRuntimeApi* api = get_api(REIM_ABI_VERSION);
  if (!api) {
    reim::log::error("preview", "reim_get_api rejected REIM_ABI_VERSION");
    ::FreeLibrary(module);
    return 5;
  }
  const std::wstring generated = std::filesystem::path(generated_path).wstring();
  const std::wstring empty;
  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  config.world_path = empty.c_str();
  config.generated_path = generated.c_str();
  config.minecraft_dir = nullptr;
  config.render_distance = 6;
  config.mouse_sensitivity = 0.15f;
  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.log = [](int32_t level, const char* category, const char* message) {
    console_log(static_cast<reim::log::Level>(level), category, message);
  };
  int32_t error = 0;
  ReimInstance* instance = api->create(&config, &host, &error);
  if (instance) {
    reim::log::info("preview", "DLL create returned an instance");
    api->destroy(instance);
    ::FreeLibrary(module);
    return 0;
  }
  ::FreeLibrary(module);
  if (error == REIM_ERR_INTERNAL) {
    reim::log::info("preview",
                    "generated manifest accepted; S1 DLL is an ABI/contract stub");
    return 0;
  }
  reim::log::error("preview", std::format("generated manifest rejected (error {})", error));
  return 6;
}
#else
int check_generated(const std::string&, const std::string&) {
  return 0;
}
#endif

bool capture_png(const std::string& path, int width, int height) {
  if (width <= 0 || height <= 0) return false;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  std::vector<uint8_t> flipped(pixels.size());
  const size_t stride = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    std::memcpy(flipped.data() + static_cast<size_t>(y) * stride,
                pixels.data() + static_cast<size_t>(height - 1 - y) * stride,
                stride);
  }
  std::string error;
  if (!reim::png::write_file(path, width, height, 4, flipped.data(), error)) {
    reim::log::error("preview", "screenshot failed: " + error);
    return false;
  }
  reim::log::info("preview", "screenshot written: " + path);
  return true;
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
    std::printf("REIMPreview S1 (ABI %u)\n", REIM_ABI_VERSION);
    return 0;
  }

#ifdef _WIN32
  std::FILE* log_file = nullptr;
  {
    const std::filesystem::path logs = std::filesystem::path(exe_directory()) / "logs";
    std::error_code ec;
    std::filesystem::create_directories(logs, ec);
    if (!ec) {
      const std::filesystem::path log_path = logs / "preview.log";
      log_file = _wfopen(log_path.wstring().c_str(), L"a");
    }
  }
  reim::log::set_sink([log_file](reim::log::Level level, std::string_view category,
                                 std::string_view message) {
    console_log(level, category, message);
    if (log_file) log_to_file(level, category, message, log_file);
  });
#endif

  reim::log::info("preview", std::format("REIMPreview S1 starting (ABI {})",
                                         REIM_ABI_VERSION));

  std::string dll_path = options.check_dll_path;
  if (dll_path.empty() && !options.dll_path.empty()) dll_path = options.dll_path;
  if (options.check_dll_path.empty()) {
    if (dll_path.empty()) {
      const std::u8string value =
          (std::filesystem::path(exe_directory()) / "mc_26.3.dll").u8string();
      dll_path.assign(reinterpret_cast<const char*>(value.data()), value.size());
    }
    if (std::filesystem::exists(dll_path)) {
      check_dll(dll_path);
    } else {
      reim::log::warn("preview", "DLL not found next to the host: " + dll_path);
    }
  } else {
    const int result = check_dll(dll_path);
    if (log_file) std::fclose(log_file);
    return result;
  }

  if (!options.generated_path.empty()) {
    const int result = check_generated(dll_path, options.generated_path);
    if (log_file) std::fclose(log_file);
    return result;
  }

  if (!glfwInit()) {
    reim::log::error("preview", "glfwInit failed");
    if (log_file) std::fclose(log_file);
    return 2;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);

  GLFWwindow* window = glfwCreateWindow(1280, 720, "REIMPreview - S1 skeleton",
                                        nullptr, nullptr);
  if (!window) {
    reim::log::error("preview", "glfwCreateWindow failed (OpenGL 3.3 Core unavailable)");
    glfwTerminate();
    if (log_file) std::fclose(log_file);
    return 2;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  reim::log::info("preview",
                  std::format("OpenGL: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION))));

  const auto start = std::chrono::steady_clock::now();
  int frame = 0;
  while (!glfwWindowShouldClose(window)) {
    glClearColor(0.07f, 0.10f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glfwSwapBuffers(window);
    glfwPollEvents();
    ++frame;

    if (options.frames > 0 && frame >= options.frames) break;
    if (options.seconds > 0.0) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
      if (elapsed >= options.seconds) break;
    }
  }

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (!options.screenshot_path.empty()) {
    capture_png(options.screenshot_path, width, height);
  }

  reim::log::info("preview", std::format("REIMPreview S1 exiting after {} frames", frame));
  glfwDestroyWindow(window);
  glfwTerminate();
  if (log_file) std::fclose(log_file);
  return 0;
}
