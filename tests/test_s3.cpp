#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "core/player.h"
#include "core/simulation.h"
#include "data/block_table.h"
#include "mesh/section_mesh.h"
#include "reim_abi.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef REIM_TEST_GENERATED_DIR
#define REIM_TEST_GENERATED_DIR "generated/26.3"
#endif

namespace {

void* REIM_CALL test_alloc(size_t size, size_t align) {
  if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
#if defined(_MSC_VER)
  return _aligned_malloc(size, align);
#else
  return std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
}

void REIM_CALL test_free(void* ptr) {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

/* Fetches and immediately returns the host-owned blob; the integration test
 * only inspects scalar frame fields. */
int32_t fetch_and_release(const ReimRuntimeApi* api, ReimInstance* instance,
                          ReimRenderFrame* frame) {
  const int32_t result = api->fetch_frame(instance, frame);
  if (result == REIM_OK) api->release_frame(instance, frame->frame_id);
  return result;
}

std::filesystem::path generated_dir() {
  const char* env = std::getenv("REIM_TEST_GENERATED");
  if (env != nullptr && *env != '\0') return std::filesystem::path(env);
  return std::filesystem::path(REIM_TEST_GENERATED_DIR);
}

bool load_real_table(reim::data::BlockTable& table, std::string& error) {
  const std::filesystem::path dir = generated_dir();
  if (!std::filesystem::is_directory(dir)) {
    error = "generated data not found: " + dir.string();
    return false;
  }
  return table.load(dir, error);
}

uint32_t query_loaded_chunk(const void* context, int x, int y, int z) {
  return static_cast<const reim::core::LoadedChunk*>(context)->block_id_at(x, y, z);
}

struct FlatWorld {
  uint32_t stone = 0;
  uint32_t water = 0;
  int surface = 64;
  int wall_x = -1000000;
  bool water_at_surface = false;
};

uint32_t query_flat(const void* context, int x, int y, int z) {
  (void)z;
  const auto* world = static_cast<const FlatWorld*>(context);
  if (x == world->wall_x && y >= world->surface && y < world->surface + 2) {
    return world->stone;
  }
  if (world->water_at_surface && y == world->surface - 1) return world->water;
  if (y < world->surface) return world->stone;
  return 0;
}

struct WallWorld {
  int wall_z = 1000000;
};

bool query_wall_collision(const void* context, int /*x*/, int /*y*/, int z) {
  return z == static_cast<const WallWorld*>(context)->wall_z;
}

reim::core::LoadedChunk make_flat_chunk(const reim::data::BlockTable& table,
                                        const std::string& block_name,
                                        int y) {
  reim::world::Chunk chunk;
  chunk.x = 0;
  chunk.z = 0;
  chunk.min_y = 0;
  chunk.max_y = 15;

  reim::world::Section section;
  section.y = 0;
  section.palette.push_back(reim::world::air_block());
  reim::world::BlockState block;
  block.name = block_name;
  section.palette.push_back(std::move(block));
  section.indices.assign(4096, 0);
  section.sky_light.assign(2048, 0xFF);
  section.indices[(y << 8)] = 1;  // local (0, y, 0)
  chunk.sections.push_back(std::move(section));

  reim::core::LoadedChunk loaded;
  loaded.cx = 0;
  loaded.cz = 0;
  loaded.chunk = std::move(chunk);
  loaded.build(table);
  return loaded;
}

std::array<int, 6> normal_histogram(const std::vector<ReimVertex>& vertices) {
  std::array<int, 6> counts{};
  for (const ReimVertex& vertex : vertices) {
    if (vertex.normal[1] > 0.5f) ++counts[0];
    else if (vertex.normal[1] < -0.5f) ++counts[1];
    else if (vertex.normal[2] < -0.5f) ++counts[2];
    else if (vertex.normal[2] > 0.5f) ++counts[3];
    else if (vertex.normal[0] < -0.5f) ++counts[4];
    else if (vertex.normal[0] > 0.5f) ++counts[5];
  }
  return counts;
}

} // namespace

TEST_CASE("S3 block table loads the Converter output") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping block-table test");
    return;
  }
  REQUIRE(table.size() > 1);

  const uint32_t stone_id = table.find("minecraft:stone");
  REQUIRE(stone_id != 0);
  const reim::data::BlockDef& stone = table.def(stone_id);
  CHECK(stone.full_block);
  CHECK(stone.collision);
  CHECK(stone.render_class == reim::data::RenderClass::opaque);
  CHECK(stone.faces[1].u1 > stone.faces[1].u0);

  const uint32_t water_id = table.find("minecraft:water");
  REQUIRE(water_id != 0);
  const reim::data::BlockDef& water = table.def(water_id);
  CHECK(water.liquid);
  CHECK(!water.collision);
  CHECK(water.render_class == reim::data::RenderClass::transparent);

  CHECK(table.def(0).render_class == reim::data::RenderClass::skip);
}

TEST_CASE("S4 block table resolves state variants and non-cube geometry") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping variant test");
    return;
  }

  // grass_block[snowy=true] -> grass_block_snow model.
  std::map<std::string, std::string, std::less<>> snowy;
  snowy["snowy"] = "true";
  const uint32_t snowy_id = table.find_state("minecraft:grass_block", snowy);
  REQUIRE(snowy_id != 0);
  CHECK(snowy_id != table.find("minecraft:grass_block"));
  const reim::data::BlockDef& snowy_grass = table.def(snowy_id);
  CHECK(snowy_grass.full_block);
  CHECK(snowy_grass.render_class == reim::data::RenderClass::opaque);

  std::map<std::string, std::string, std::less<>> plain;
  plain["snowy"] = "false";
  CHECK(table.find_state("minecraft:grass_block", plain) ==
        table.find("minecraft:grass_block"));

  // Thin snow layer carries explicit box geometry instead of a cube.
  std::map<std::string, std::string, std::less<>> one_layer;
  one_layer["layers"] = "1";
  const uint32_t snow_id = table.find_state("minecraft:snow", one_layer);
  REQUIRE(snow_id != 0);
  const reim::data::BlockDef& snow = table.def(snow_id);
  CHECK(!snow.full_block);
  CHECK(!snow.geometry.empty());
  CHECK(snow.render_class == reim::data::RenderClass::opaque);

  std::map<std::string, std::string, std::less<>> eight_layers;
  eight_layers["layers"] = "8";
  CHECK(table.def(table.find_state("minecraft:snow", eight_layers)).full_block);

  // Cross plants are cutout geometry, not full cubes.
  const reim::data::BlockDef& grass =
      table.def(table.find("minecraft:short_grass"));
  CHECK(!grass.full_block);
  CHECK(grass.render_class == reim::data::RenderClass::opaque);
  CHECK(grass.geometry.size() == 4);

  // Grass/foliage faces resolve their tint from the biome table.
  uint32_t color = 0;
  CHECK(table.biome_color("minecraft:snowy_plains", reim::data::TintKind::grass,
                          color));
  CHECK(color == 0xFF97B480u); // 0x80B497 as RGBA8 (red in the low byte)
}

TEST_CASE("S4 snow layer meshes as a thin box, not a cube") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping snow mesh test");
    return;
  }
  reim::core::LoadedChunk chunk = make_flat_chunk(table, "minecraft:snow", 0);
  // The palette entry needs the layer property to resolve to the thin model.
  chunk.chunk.sections[0].palette[1].properties["layers"] = "1";
  chunk.build(table);

  std::vector<reim::mesh::SectionMeshOutput> out;
  reim::mesh::mesh_chunk(0, 0, chunk.chunk, chunk.section_block_ids, table,
                         &query_loaded_chunk, &chunk, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].opaque_indices.size() == 36); // six faces of the box
  float max_y = 0.0f;
  for (const ReimVertex& vertex : out[0].opaque_vertices) {
    max_y = std::max(max_y, vertex.position[1]);
  }
  CHECK(max_y < 0.2f); // 2/16 block high
}

TEST_CASE("S3 section mesh emits six outward faces for one block") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping mesh test");
    return;
  }
  reim::core::LoadedChunk chunk = make_flat_chunk(table, "minecraft:stone", 0);

  std::vector<reim::mesh::SectionMeshOutput> out;
  reim::mesh::mesh_chunk(0, 0, chunk.chunk, chunk.section_block_ids, table,
                         &query_loaded_chunk, &chunk, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].opaque_indices.size() == 36);
  CHECK(out[0].opaque_vertices.size() == 24);
  CHECK(out[0].transparent_indices.empty());

  const std::array<int, 6> normals = normal_histogram(out[0].opaque_vertices);
  CHECK(normals[0] == 4);  // up
  CHECK(normals[1] == 4);  // down
  CHECK(normals[2] == 4);  // north
  CHECK(normals[3] == 4);  // south
  CHECK(normals[4] == 4);  // west
  CHECK(normals[5] == 4);  // east

  // Sky light 15 was filled into the only section.
  CHECK((out[0].opaque_vertices.front().light >> 8) == 15);
  CHECK(out[0].opaque_vertices.front().sprite == table.def(table.find("minecraft:stone"))
                                                   .faces[0]
                                                   .sprite);
}

TEST_CASE("S3 section mesh culls faces between two opaque blocks") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping mesh culling test");
    return;
  }
  reim::core::LoadedChunk chunk = make_flat_chunk(table, "minecraft:stone", 0);
  reim::world::Section& section = chunk.chunk.sections[0];
  section.indices[(0 << 8) | 1] = 1;  // local (1, 0, 0)
  chunk.build(table);

  std::vector<reim::mesh::SectionMeshOutput> out;
  reim::mesh::mesh_chunk(0, 0, chunk.chunk, chunk.section_block_ids, table,
                         &query_loaded_chunk, &chunk, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].opaque_vertices.size() == 40);  // ten faces
  CHECK(out[0].opaque_indices.size() == 60);

  const std::array<int, 6> normals = normal_histogram(out[0].opaque_vertices);
  CHECK(normals[5] == 4);  // one remaining east face (the second block)
  CHECK(normals[4] == 4);  // one remaining west face (the first block)
}

TEST_CASE("S3 water meshes into the transparent list") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping water mesh test");
    return;
  }
  reim::core::LoadedChunk chunk = make_flat_chunk(table, "minecraft:water", 0);
  std::vector<reim::mesh::SectionMeshOutput> out;
  reim::mesh::mesh_chunk(0, 0, chunk.chunk, chunk.section_block_ids, table,
                         &query_loaded_chunk, &chunk, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].opaque_indices.empty());
  CHECK(out[0].transparent_indices.size() == 36);
}

TEST_CASE("S3 player falls, lands and jumps on a flat floor") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping player physics test");
    return;
  }
  FlatWorld world;
  world.surface = 64;
  world.stone = table.find("minecraft:stone");

  reim::core::Player player;
  player.x = 0.5;
  player.y = 70.0;
  player.z = 0.5;
  reim::core::InputState input;
  for (int tick = 0; tick < 200; ++tick) {
    reim::core::player_step(player, input, &query_flat, &world, table);
  }
  CHECK(std::fabs(player.y - 64.0) < 0.01);
  CHECK(player.on_ground);
  CHECK((player.flags & REIM_PLAYER_ON_GROUND) != 0);

  input.jump_requested = true;
  double peak = player.y;
  for (int tick = 0; tick < 40; ++tick) {
    reim::core::player_step(player, input, &query_flat, &world, table);
    peak = std::max(peak, player.y);
  }
  CHECK(peak > 64.3);
  CHECK(std::fabs(player.y - 64.0) < 0.01);
  CHECK(player.on_ground);
}

TEST_CASE("S3 player collides with a wall on the horizontal axis") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping wall collision test");
    return;
  }
  FlatWorld world;
  world.surface = 64;
  world.stone = table.find("minecraft:stone");
  world.wall_x = 2;

  reim::core::Player player;
  player.x = 0.5;
  player.y = 64.0;
  player.z = 0.5;
  player.yaw = 270.0f;  // forward = +X
  reim::core::InputState input;
  input.forward = true;
  for (int tick = 0; tick < 80; ++tick) {
    reim::core::player_step(player, input, &query_flat, &world, table);
  }
  CHECK(player.x <= 1.7 + 1e-3);
  CHECK(player.x > 1.4);
}

TEST_CASE("S3 arrow keys rotate the view at a fixed per-tick rate") {
  reim::data::BlockTable table;
  std::string error;
  if (!load_real_table(table, error)) {
    WARN(error << "; skipping arrow-look test");
    return;
  }
  FlatWorld world;
  world.surface = 64;
  world.stone = table.find("minecraft:stone");

  reim::core::Player player;
  player.x = 0.5;
  player.y = 64.0;
  player.z = 0.5;
  reim::core::InputState input;
  input.look_right = true;
  for (int tick = 0; tick < 10; ++tick) {
    reim::core::player_step(player, input, &query_flat, &world, table);
  }
  CHECK(std::fabs(player.yaw - 50.0f) < 1e-3f);

  input.look_right = false;
  input.look_up = true;
  for (int tick = 0; tick < 5; ++tick) {
    reim::core::player_step(player, input, &query_flat, &world, table);
  }
  CHECK(std::fabs(player.pitch + 25.0f) < 1e-3f);
}

TEST_CASE("S3 third-person backoff stops before a colliding block") {
  // Eye at z=0.5 facing +Z: pulling back walks towards -Z and enters block
  // z=-1 (its span is [-1, 0)) after half a block.
  WallWorld wall{-1};
  const float blocked = reim::core::camera_backoff_distance(
      0.5, 1.62, 0.5, 0.0f, 0.0f, 4.0f, &query_wall_collision, &wall);
  CHECK(blocked > 0.2f);
  CHECK(blocked < 0.4f);

  WallWorld open{1000000};
  const float free = reim::core::camera_backoff_distance(
      0.5, 1.62, 0.5, 0.0f, 0.0f, 4.0f, &query_wall_collision, &open);
  CHECK(std::fabs(free - 4.0f) < 1e-5f);

  // Eye already inside a colliding block (block z=0): no pull-back at all.
  WallWorld inside{0};
  const float stuck = reim::core::camera_backoff_distance(
      0.5, 1.62, 0.5, 0.0f, 0.0f, 4.0f, &query_wall_collision, &inside);
  CHECK(stuck == 0.0f);
}

TEST_CASE("S3 runtime ABI loads the real world, ticks and applies input") {
  const char* world_env = std::getenv("REIM_TEST_WORLD");
  if (world_env == nullptr || *world_env == '\0') {
    WARN("REIM_TEST_WORLD not set; skipping S3 runtime integration test");
    return;
  }
  std::string error;
  reim::data::BlockTable probe;
  if (!load_real_table(probe, error)) {
    WARN(error << "; skipping S3 runtime integration test");
    return;
  }

  const ReimRuntimeApi* api = reim_get_api(REIM_ABI_VERSION);
  REQUIRE(api != nullptr);
  REQUIRE(api->abi_version == REIM_ABI_VERSION);

  ReimHostApi host{};
  host.struct_size = sizeof(host);
  host.abi_version = REIM_ABI_VERSION;
  host.alloc = &test_alloc;
  host.free = &test_free;
  host.log = +[](int32_t, const char*, const char*) {};

  const std::wstring world_path = std::filesystem::path(world_env).wstring();
  const std::wstring generated_path = generated_dir().wstring();

  ReimInstanceConfig config{};
  config.struct_size = sizeof(config);
  config.abi_version = REIM_ABI_VERSION;
  config.world_path = world_path.c_str();
  config.generated_path = generated_path.c_str();
  config.render_distance = 2;
  config.mouse_sensitivity = 0.15f;

  int32_t create_error = 0;
  ReimInstance* instance = api->create(&config, &host, &create_error);
  INFO("create error " << create_error);
  REQUIRE(instance != nullptr);
  REQUIRE(api->start(instance) == REIM_OK);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(120);
  ReimRenderFrame frame{};
  uint64_t updates_seen = 0;
  bool ready = false;
  while (std::chrono::steady_clock::now() < deadline) {
    frame = ReimRenderFrame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    REQUIRE(fetch_and_release(api, instance, &frame) == REIM_OK);
    updates_seen += frame.update_count;
    if (frame.ready && frame.tick >= 40) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(ready);
  CHECK(frame.loaded_chunks > 0);
  CHECK(frame.mesh_sections > 0);
  CHECK(updates_seen > 0);
  CHECK(frame.measured_tps > 10.0f);
  CHECK(frame.player.position.y > -64.0f);

  ReimInputEvent mouse{};
  mouse.struct_size = sizeof(mouse);
  mouse.type = REIM_INPUT_MOUSE_MOTION;
  mouse.dx = 40.0f;
  api->submit_input(instance, &mouse);

  ReimInputEvent key{};
  key.struct_size = sizeof(key);
  key.type = REIM_INPUT_KEY;
  key.key = REIM_KEY_W;
  key.action = REIM_ACTION_PRESS;
  api->submit_input(instance, &key);

  const uint64_t start_tick = frame.tick;
  const float start_yaw = frame.player.yaw;
  const float start_x = frame.player.position.x;
  const float start_z = frame.player.position.z;
  while (frame.tick < start_tick + 20 && std::chrono::steady_clock::now() < deadline) {
    frame = ReimRenderFrame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    REQUIRE(fetch_and_release(api, instance, &frame) == REIM_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(frame.tick >= start_tick + 20);
  CHECK(std::fabs(frame.player.yaw - (start_yaw + 6.0f)) < 0.05f);
  const float moved = std::hypot(frame.player.position.x - start_x,
                                 frame.player.position.z - start_z);
  CHECK(moved > 0.25f);

  // Keep walking south long enough to cross a chunk boundary; streaming must
  // load/remesh without stalling the tick loop or crashing.
  const uint64_t walk_tick = frame.tick;
  while (frame.tick < walk_tick + 140 &&
         std::chrono::steady_clock::now() < deadline) {
    frame = ReimRenderFrame{};
    frame.struct_size = sizeof(frame);
    frame.abi_version = REIM_ABI_VERSION;
    REQUIRE(fetch_and_release(api, instance, &frame) == REIM_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(frame.tick >= walk_tick + 140);
  CHECK(frame.loaded_chunks > 0);
  CHECK(frame.mesh_sections > 0);
  const int start_chunk_z = static_cast<int>(std::floor(start_z / 16.0f));
  const int end_chunk_z = static_cast<int>(std::floor(frame.player.position.z / 16.0f));
  CHECK(end_chunk_z != start_chunk_z);

  int32_t status_length = api->get_status_json(instance, nullptr, 0);
  REQUIRE(status_length > 1);
  std::vector<char> status(static_cast<size_t>(status_length), '\0');
  const int32_t written =
      api->get_status_json(instance, status.data(), static_cast<uint32_t>(status.size()));
  REQUIRE(written > 0);
  const std::string status_text(status.data(), static_cast<size_t>(written));
  CHECK(status_text.find("\"ready\":true") != std::string::npos);
  CHECK(status_text.find("\"samples\"") != std::string::npos);

  // Pausing stops fixed ticks; rendering (fetch) keeps working.
  api->set_simulation_paused(instance, 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  frame = ReimRenderFrame{};
  frame.struct_size = sizeof(frame);
  REQUIRE(fetch_and_release(api, instance, &frame) == REIM_OK);
  const uint64_t paused_tick = frame.tick;
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  frame = ReimRenderFrame{};
  frame.struct_size = sizeof(frame);
  REQUIRE(fetch_and_release(api, instance, &frame) == REIM_OK);
  CHECK(frame.tick <= paused_tick + 1);
  api->set_simulation_paused(instance, 0);

  api->stop(instance);
  api->destroy(instance);
}
