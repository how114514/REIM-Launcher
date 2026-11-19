/*
 * reim-headless: the M1-D7 diagnostic host.
 *
 * Loads mc_26.3.dll through the C ABI, opens a real 26.3 world, runs the fixed
 * 20 TPS simulation for N ticks and writes a JSON report. It never opens a
 * window and never starts Java.
 *
 *   reim-headless --headless --world <save> --generated generated\26.3
 *                 --ticks 100 --dump build\m1\reports\s3-headless.json
 */
#include "reim_abi.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Options {
  std::string world;
  std::string generated;
  std::string minecraft_dir;
  std::string dll;
  std::string dump;
  uint64_t ticks = 0;
  double seconds = 0.0;
  double timeout_seconds = 120.0;
  uint32_t render_distance = 6;
  bool spawn_override = false;
  float spawn_x = 0.0f;
  float spawn_y = 0.0f;
  float spawn_z = 0.0f;
  bool headless = false;
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
      "reim-headless (M1-D7)\n"
      "  --headless               confirm windowless mode (always true here)\n"
      "  --world PATH             absolute 26.3 save directory (required)\n"
      "  --generated DIR          generated/26.3 directory (required)\n"
      "  --minecraft-dir PATH     optional, recorded for diagnostics\n"
      "  --dll PATH               mc_26.3.dll (default: beside this exe)\n"
      "  --render-distance N      chunk radius, default 6\n"
      "  --spawn X Y Z            override the level.dat spawn (diagnostics)\n"
      "  --ticks N                run until the runtime reaches tick N\n"
      "  --seconds N              run for N wall-clock seconds\n"
      "  --timeout N              abort after N seconds (default 120)\n"
      "  --dump PATH              write the JSON report\n"
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
    if (arg == "--help" || arg == "-h") options.help = true;
    else if (arg == "--version") options.version = true;
    else if (arg == "--headless") options.headless = true;
    else if (arg == "--world") { if (!next(options.world)) return false; }
    else if (arg == "--generated") { if (!next(options.generated)) return false; }
    else if (arg == "--minecraft-dir") { if (!next(options.minecraft_dir)) return false; }
    else if (arg == "--dll") { if (!next(options.dll)) return false; }
    else if (arg == "--dump") { if (!next(options.dump)) return false; }
    else if (arg == "--ticks") {
      std::string value;
      if (!next(value)) return false;
      options.ticks = std::strtoull(value.c_str(), nullptr, 10);
    } else if (arg == "--seconds") {
      std::string value;
      if (!next(value)) return false;
      options.seconds = std::atof(value.c_str());
    } else if (arg == "--timeout") {
      std::string value;
      if (!next(value)) return false;
      options.timeout_seconds = std::atof(value.c_str());
    } else if (arg == "--render-distance") {
      std::string value;
      if (!next(value)) return false;
      options.render_distance =
          static_cast<uint32_t>((std::max)(1, std::atoi(value.c_str())));
    } else if (arg == "--spawn") {
      std::string x, y, z;
      if (!next(x) || !next(y) || !next(z)) return false;
      options.spawn_override = true;
      options.spawn_x = static_cast<float>(std::atof(x.c_str()));
      options.spawn_y = static_cast<float>(std::atof(y.c_str()));
      options.spawn_z = static_cast<float>(std::atof(z.c_str()));
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

void host_log(int32_t level, const char* category, const char* message) {
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
  const int index = std::clamp(static_cast<int>(level), 0, 4);
  std::printf("[%s] %s: %s\n", names[index], category ? category : "?",
              message ? message : "");
  std::fflush(stdout);
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
    std::printf("reim-headless S3 (ABI %u)\n", REIM_ABI_VERSION);
    return 0;
  }
  if (options.world.empty() || options.generated.empty()) {
    std::fprintf(stderr, "--world and --generated are required\n");
    print_usage();
    return 2;
  }
  if (options.ticks == 0 && options.seconds <= 0.0) {
    options.ticks = 100;
  }

  std::string dll_path = options.dll;
  if (dll_path.empty()) {
    dll_path = (std::filesystem::path(exe_directory()) / "mc_26.3.dll").string();
  }

#ifdef _WIN32
  HMODULE module = ::LoadLibraryW(std::filesystem::path(dll_path).wstring().c_str());
  if (!module) {
    std::fprintf(stderr, "LoadLibraryW failed for %s (error %lu)\n", dll_path.c_str(),
                 ::GetLastError());
    return 3;
  }
  using GetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
  auto get_api = reinterpret_cast<GetApiFn>(::GetProcAddress(module, "reim_get_api"));
  if (!get_api) {
    std::fprintf(stderr, "GetProcAddress(reim_get_api) failed\n");
    ::FreeLibrary(module);
    return 4;
  }
  const ReimRuntimeApi* api = get_api(REIM_ABI_VERSION);
  if (!api || api->abi_version != REIM_ABI_VERSION) {
    std::fprintf(stderr, "reim_get_api rejected ABI %u\n", REIM_ABI_VERSION);
    ::FreeLibrary(module);
    return 5;
  }
#else
  std::fprintf(stderr, "reim-headless is Windows-only in M1\n");
  return 3;
#endif

  const std::wstring world_path = std::filesystem::path(options.world).wstring();
  const std::wstring generated_path = std::filesystem::path(options.generated).wstring();
  const std::wstring minecraft_path =
      std::filesystem::path(options.minecraft_dir).wstring();

  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.log = &host_log;

  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  config.world_path = world_path.c_str();
  config.generated_path = generated_path.c_str();
  config.minecraft_dir = options.minecraft_dir.empty() ? nullptr : minecraft_path.c_str();
  config.render_distance = options.render_distance;
  config.flags = options.spawn_override ? REIM_CONFIG_FLAG_SPAWN_OVERRIDE : 0u;
  config.mouse_sensitivity = 0.15f;
  config.invert_y = 0;
  config.spawn_x = options.spawn_x;
  config.spawn_y = options.spawn_y;
  config.spawn_z = options.spawn_z;

  int32_t error = 0;
  ReimInstance* instance = api->create(&config, &host, &error);
  if (!instance) {
    std::fprintf(stderr, "create failed with error %d\n", error);
#ifdef _WIN32
    ::FreeLibrary(module);
#endif
    return 6;
  }

  const int32_t started = api->start(instance);
  if (started != REIM_OK) {
    std::fprintf(stderr, "start failed with error %d\n", started);
    api->destroy(instance);
#ifdef _WIN32
    ::FreeLibrary(module);
#endif
    return 7;
  }

  const auto start_time = std::chrono::steady_clock::now();
  ReimRenderFrame last{};
  last.struct_size = sizeof(last);
  uint64_t frames = 0;
  uint64_t updates_seen = 0;
  uint64_t removes_seen = 0;
  uint64_t vertices_seen = 0;
  uint64_t indices_seen = 0;
  bool ready = false;
  bool timed_out = false;
  bool failed = false;
  int32_t last_result = REIM_OK;

  while (true) {
    ReimRenderFrame frame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    last_result = api->fetch_frame(instance, &frame);
    if (last_result != REIM_OK) {
      failed = true;
      break;
    }
    last = frame;
    ++frames;
    updates_seen += frame.update_count;
    removes_seen += frame.remove_count;
    for (uint32_t i = 0; i < frame.update_count; ++i) {
      vertices_seen += frame.updates[i].vertex_count;
      indices_seen += frame.updates[i].index_count;
    }
    if (frame.ready) ready = true;

    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();
    if (ready && options.ticks > 0 && frame.tick >= options.ticks) break;
    if (ready && options.seconds > 0.0 && elapsed >= options.seconds) break;
    if (elapsed >= options.timeout_seconds) {
      timed_out = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  int32_t status_needed = api->get_status_json(instance, nullptr, 0);
  std::vector<char> status_buffer(
      status_needed > 0 ? static_cast<size_t>(status_needed) : 4096u, '\0');
  const int32_t status_written =
      api->get_status_json(instance, status_buffer.data(),
                           static_cast<uint32_t>(status_buffer.size()));
  std::string status_text =
      status_written > 0 ? std::string(status_buffer.data(), status_written)
                         : std::string("{}");

  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start_time)
                             .count();

  std::string report;
  report += "{\n";
  report += "  \"stage\": \"S3-headless\",\n";
  report += std::string("  \"ready\": ") + (ready ? "true" : "false") + ",\n";
  report += std::string("  \"timed_out\": ") + (timed_out ? "true" : "false") + ",\n";
  report += std::string("  \"failed\": ") + (failed ? "true" : "false") + ",\n";
  report += "  \"last_result\": " + std::to_string(last_result) + ",\n";
  report += "  \"frames\": " + std::to_string(frames) + ",\n";
  report += "  \"tick\": " + std::to_string(last.tick) + ",\n";
  report += "  \"elapsed_seconds\": " + std::to_string(elapsed) + ",\n";
  report += "  \"updates_seen\": " + std::to_string(updates_seen) + ",\n";
  report += "  \"removes_seen\": " + std::to_string(removes_seen) + ",\n";
  report += "  \"vertices_seen\": " + std::to_string(vertices_seen) + ",\n";
  report += "  \"indices_seen\": " + std::to_string(indices_seen) + ",\n";
  report += "  \"frame\": {\n";
  report += "    \"ready\": " + std::string(last.ready ? "true" : "false") + ",\n";
  report += "    \"tick\": " + std::to_string(last.tick) + ",\n";
  report += "    \"loaded_chunks\": " + std::to_string(last.loaded_chunks) + ",\n";
  report += "    \"total_chunks\": " + std::to_string(last.total_chunks) + ",\n";
  report += "    \"loading_percent\": " + std::to_string(last.loading_percent) + ",\n";
  report += "    \"loaded_sections\": " + std::to_string(last.loaded_sections) + ",\n";
  report += "    \"mesh_sections\": " + std::to_string(last.mesh_sections) + ",\n";
  report += "    \"block_count\": " + std::to_string(last.block_count) + ",\n";
  report += "    \"measured_tps\": " + std::to_string(last.measured_tps) + ",\n";
  report += "    \"update_count\": " + std::to_string(last.update_count) + ",\n";
  report += "    \"remove_count\": " + std::to_string(last.remove_count) + ",\n";
  report += "    \"player\": {\"x\": " + std::to_string(last.player.position.x) +
            ", \"y\": " + std::to_string(last.player.position.y) +
            ", \"z\": " + std::to_string(last.player.position.z) +
            ", \"yaw\": " + std::to_string(last.player.yaw) +
            ", \"pitch\": " + std::to_string(last.player.pitch) +
            ", \"flags\": " + std::to_string(last.player.flags) + "}\n";
  report += "  },\n";
  report += "  \"status\": " + status_text + "\n";
  report += "}\n";

  if (!options.dump.empty()) {
    std::error_code ec;
    const std::filesystem::path dump_path(options.dump);
    if (dump_path.has_parent_path()) {
      std::filesystem::create_directories(dump_path.parent_path(), ec);
    }
    std::ofstream stream(dump_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
      std::fprintf(stderr, "cannot write %s\n", options.dump.c_str());
    } else {
      stream << report;
      std::printf("report written: %s\n", options.dump.c_str());
    }
  }
  std::printf("%s", report.c_str());

  api->stop(instance);
  api->destroy(instance);
#ifdef _WIN32
  ::FreeLibrary(module);
#endif

  if (failed) return 8;
  if (timed_out) return 9;
  if (!ready) return 10;
  if (options.ticks > 0 && last.tick < options.ticks) return 11;
  return 0;
}
