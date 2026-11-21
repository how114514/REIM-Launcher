/*
 * M2-B: Contract v2 data-plane tests.
 *
 * These run against the real 26.3 save when REIM_TEST_WORLD is set; without it
 * they warn and skip, matching the M1 integration-test convention. Unit checks
 * that need no world still run.
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "data/block_table.h"
#include "reim_abi.h"
#include "ReimCoordinate.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#ifndef REIM_TEST_GENERATED_DIR
#define REIM_TEST_GENERATED_DIR "generated/26.3"
#endif

namespace {

std::atomic<uint64_t> g_allocs{0};
std::atomic<uint64_t> g_frees{0};

void* REIM_CALL counting_alloc(size_t size, size_t align) {
  if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
#if defined(_MSC_VER)
  void* ptr = _aligned_malloc(size, align);
#else
  void* ptr = std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
  if (ptr != nullptr) g_allocs.fetch_add(1);
  return ptr;
}

void REIM_CALL counting_free(void* ptr) {
  if (ptr == nullptr) return;
  g_frees.fetch_add(1);
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

  bool create(const std::filesystem::path& world, uint32_t render_distance,
              uint32_t render_level = 0) {
    api = reim_get_api(REIM_ABI_VERSION);
    if (api == nullptr) return false;
    host = ReimHostApi{};
    host.struct_size = sizeof(host);
    host.abi_version = REIM_ABI_VERSION;
    host.alloc = &counting_alloc;
    host.free = &counting_free;
    host.log = &silent_log;

    const std::wstring world_path = world.wstring();
    const std::wstring generated = generated_dir().wstring();
    ReimInstanceConfig config{};
    config.struct_size = sizeof(config);
    config.abi_version = REIM_ABI_VERSION;
    config.world_path = world_path.c_str();
    config.generated_path = generated.c_str();
    config.render_distance = render_distance;
    config.mouse_sensitivity = 0.15f;
    config.render_level = render_level;
    int32_t error = 0;
    instance = api->create(&config, &host, &error);
    return instance != nullptr;
  }

  ~Fixture() {
    if (api != nullptr && instance != nullptr) api->destroy(instance);
  }
};

int32_t fetch(const Fixture& f, ReimRenderFrame& frame) {
  frame = ReimRenderFrame{};
  frame.struct_size = sizeof(frame);
  frame.abi_version = REIM_ABI_VERSION;
  return f.api->fetch_frame(f.instance, &frame);
}

} // namespace

TEST_CASE("M2-B coordinate, FOV and angle conversion") {
  using namespace reim::bridge;

  const Vec3 south = PositionMetresToUe(0.0, 0.0, 1.0);
  CHECK(south.x == 100.0);   // MC south -> UE forward
  CHECK(south.y == 0.0);
  CHECK(south.z == 0.0);

  const Vec3 east = PositionMetresToUe(1.0, 0.0, 0.0);
  CHECK(east.x == 0.0);
  CHECK(east.y == -100.0);   // MC east -> UE left
  CHECK(east.z == 0.0);

  const Vec3 up = PositionMetresToUe(0.0, 1.0, 0.0);
  CHECK(up.z == 100.0);      // MC +Y up -> UE +Z up

  const Vec3 mixed = PositionMetresToUe(1.5, 65.0, -80.0);
  CHECK(mixed.x == -8000.0);
  CHECK(mixed.y == -150.0);
  CHECK(mixed.z == 6500.0);

  const Vec3 normal = NormalToUe(0.0, 1.0, 0.0);
  CHECK(normal.z == 1.0);

  float ue_yaw = 0.0f;
  float ue_pitch = 0.0f;
  LookToUe(90.0f, 15.0f, ue_yaw, ue_pitch);
  CHECK(ue_yaw == 90.0f);
  CHECK(ue_pitch == -15.0f);

  CHECK(VerticalToHorizontalFovDegrees(70.0, 1.0) == Approx(70.0).epsilon(1e-9));
  const double hd = VerticalToHorizontalFovDegrees(70.0, 16.0 / 9.0);
  CHECK(hd > 70.0);
  CHECK(hd == Approx(102.45).epsilon(1e-3));

  CHECK(ClipPlaneMetresToUe(0.05) == Approx(kR0NearUe));
  CHECK(R0FarUe(8) == Approx(14400.0));

  const float wrapped_up = std::fmod(InterpolateAngleDegrees(350.0f, 10.0f, 0.5f),
                                     360.0f);
  const float wrapped_down = std::fmod(InterpolateAngleDegrees(10.0f, 350.0f, 0.5f),
                                       360.0f);
  CHECK(wrapped_up == Approx(0.0f).margin(1e-4));
  CHECK(wrapped_down == Approx(0.0f).margin(1e-4));
  CHECK(InterpolateAngleDegrees(0.0f, 20.0f, 0.25f) == Approx(5.0f));
  CHECK(InterpolateAngleDegrees(180.0f, 180.0f, 0.5f) == Approx(180.0f));
}

TEST_CASE("M2-B create rejects unsupported render level and missing allocator") {
  const ReimRuntimeApi* api = reim_get_api(REIM_ABI_VERSION);
  REQUIRE(api != nullptr);

  const std::wstring generated = generated_dir().wstring();
  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  config.generated_path = generated.c_str();
  config.render_level = 1;

  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.alloc = &counting_alloc;
  host.free = &counting_free;
  host.log = &silent_log;

  int32_t error = 0;
  ReimInstance* instance = api->create(&config, &host, &error);
  CHECK(instance == nullptr);
  CHECK(error == REIM_ERR_CAPABILITY);

  config.render_level = 0;
  ReimHostApi no_alloc{};
  no_alloc.struct_size = sizeof(no_alloc);
  no_alloc.abi_version = REIM_ABI_VERSION;
  no_alloc.log = &silent_log;
  CHECK(api->create(&config, &no_alloc, &error) == nullptr);
  CHECK(error == REIM_ERR_INVALID_ARGUMENT);
}

TEST_CASE("M2-B frame ownership, backpressure, interpolation and assets") {
  std::filesystem::path world;
  if (!world_available(world)) {
    WARN("REIM_TEST_WORLD not set; skipping M2-B runtime integration test");
    return;
  }
  reim::data::BlockTable probe;
  std::string load_error;
  if (!probe.load(generated_dir(), load_error)) {
    WARN(load_error << "; skipping M2-B runtime integration test");
    return;
  }

  g_allocs = 0;
  g_frees = 0;
  {
    Fixture fixture;
    REQUIRE(fixture.create(world, 2));
    REQUIRE(fixture.api->start(fixture.instance) == REIM_OK);

    ReimRuntimeInfo info{};
    info.struct_size = sizeof(info);
    REQUIRE(fixture.api->get_runtime_info(fixture.instance, &info) == REIM_OK);
    CHECK(info.abi_version == REIM_ABI_VERSION);
    CHECK(info.dll_abi_version == REIM_ABI_VERSION);
    CHECK(std::strcmp(info.profile, "26.3") == 0);
    CHECK((info.capabilities & REIM_CAP_FRAME_BLOB) != 0);
    CHECK((info.capabilities & REIM_CAP_INTERPOLATION) != 0);
    CHECK((info.capabilities & REIM_CAP_ATLAS_ASSET) != 0);
    CHECK(info.render_level == 0);

    const void* png = nullptr;
    uint64_t png_size = 0;
    uint64_t png_revision = 0;
    REQUIRE(fixture.api->get_asset(fixture.instance, REIM_ASSET_ATLAS_PNG, &png,
                                   &png_size, &png_revision) == REIM_OK);
    REQUIRE(png != nullptr);
    REQUIRE(png_size > 8);
    const auto* bytes = static_cast<const uint8_t*>(png);
    CHECK(bytes[0] == 0x89);
    CHECK(bytes[1] == 'P');
    CHECK(png_revision != 0);

    const void* meta = nullptr;
    uint64_t meta_size = 0;
    uint64_t meta_revision = 0;
    REQUIRE(fixture.api->get_asset(fixture.instance, REIM_ASSET_ATLAS_META, &meta,
                                   &meta_size, &meta_revision) == REIM_OK);
    REQUIRE(meta != nullptr);
    const std::string meta_text(static_cast<const char*>(meta),
                                static_cast<size_t>(meta_size));
    CHECK(meta_text.find("\"width\"") != std::string::npos);
    CHECK(meta_text.find("\"tile_size\"") != std::string::npos);
    CHECK(meta_text.find("\"hash\"") != std::string::npos);

    /* Four outstanding blobs are allowed; the fifth fetch must fail without
     * advancing frame ids or allocating. */
    ReimRenderFrame frames[REIM_MAX_PENDING_FRAMES];
    for (uint32_t i = 0; i < REIM_MAX_PENDING_FRAMES; ++i) {
      REQUIRE(fetch(fixture, frames[i]) == REIM_OK);
      CHECK(frames[i].owned_block != nullptr);
      CHECK(frames[i].owned_bytes > 0);
      if (i != 0) CHECK(frames[i].frame_id == frames[i - 1].frame_id + 1);
    }
    const uint64_t last_id = frames[REIM_MAX_PENDING_FRAMES - 1].frame_id;
    ReimRenderFrame busy{};
    CHECK(fetch(fixture, busy) == REIM_ERR_BUSY);

    for (uint32_t i = 0; i < REIM_MAX_PENDING_FRAMES; ++i) {
      fixture.api->release_frame(fixture.instance, frames[i].frame_id);
    }
    /* Duplicate release is a no-op. */
    fixture.api->release_frame(fixture.instance, last_id);

    ReimRenderFrame after{};
    REQUIRE(fetch(fixture, after) == REIM_OK);
    CHECK(after.frame_id > last_id);
    CHECK(after.tick_alpha >= 0.0f);
    CHECK(after.tick_alpha <= 1.0f);
    CHECK(after.prev_player.position.y > -1000.0f);
    fixture.api->release_frame(fixture.instance, after.frame_id);

    /* Pausing pins tick_alpha at 0 and keeps prev == current. */
    fixture.api->set_simulation_paused(fixture.instance, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    ReimRenderFrame paused{};
    REQUIRE(fetch(fixture, paused) == REIM_OK);
    CHECK(paused.tick_alpha == 0.0f);
    CHECK(paused.prev_player.position.x == paused.player.position.x);
    CHECK(paused.prev_player.position.z == paused.player.position.z);
    fixture.api->release_frame(fixture.instance, paused.frame_id);
    fixture.api->set_simulation_paused(fixture.instance, 0);

    /* Blobs still outstanding at destroy must be freed by the Runtime. */
    ReimRenderFrame leaked_a{};
    ReimRenderFrame leaked_b{};
    REQUIRE(fetch(fixture, leaked_a) == REIM_OK);
    REQUIRE(fetch(fixture, leaked_b) == REIM_OK);
    fixture.api->stop(fixture.instance);
  }
  CHECK(g_allocs.load() == g_frees.load());
}
