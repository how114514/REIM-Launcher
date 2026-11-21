/*
 * M3-F unit tests: survival rules, entity physics/AI/combat/drops and the
 * native entity model definitions. Scenario coverage lives in
 * tests/scenarios/m3f (SUR-01..08, MOB-01..08, UI-07).
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "data/block_table.h"
#include "game/entity.h"
#include "game/item.h"
#include "game/survival.h"
#include "game/world_access.h"

#include <cmath>
#include <filesystem>
#include <string>

#include "json.hpp"

#ifndef REIM_TEST_GENERATED_DIR
#define REIM_TEST_GENERATED_DIR "generated/26.3"
#endif

namespace {

using reim::game::DamageCause;
using reim::game::EntityManager;
using reim::game::EntityType;
using reim::game::SurvivalContext;
using reim::game::SurvivalState;

struct Tables {
  reim::data::BlockTable blocks;
  reim::game::ItemTable items;
  bool ok = false;

  Tables() {
    std::string error;
    const std::filesystem::path dir(REIM_TEST_GENERATED_DIR);
    if (!blocks.load(dir, error)) return;
    if (!items.load(blocks, dir, error)) return;
    ok = true;
  }
};

/* Flat test world: solid floor at y < 0, air above, always loaded. */
class FlatWorld final : public reim::game::WorldAccess {
 public:
  FlatWorld(const reim::data::BlockTable& blocks)
      : blocks_(blocks),
        stone_id_(blocks.find("minecraft:stone")) {
    stone_.name = "minecraft:stone";
    air_.name = "minecraft:air";
  }

  const reim::data::BlockTable& blocks() const override { return blocks_; }
  uint32_t block_id_at(int, int y, int) const override {
    return y < 0 ? stone_id_ : 0u;
  }
  bool block_state_at(int, int y, int, reim::world::BlockState& out) const override {
    out = y < 0 ? stone_ : air_;
    return true;
  }
  bool set_block(int, int y, int, const reim::world::BlockState& state,
                 std::string&) override {
    if (y < 0) stone_ = state;
    return true;
  }
  bool is_loaded(int, int) const override { return true; }
  bool collides(int, int y, int) const override { return y < 0; }
  reim::game::EnvironmentState environment() const override {
    reim::game::EnvironmentState state;
    state.time_of_day = 6000;
    state.daylight = true;
    return state;
  }

 private:
  const reim::data::BlockTable& blocks_;
  uint32_t stone_id_ = 0;
  reim::world::BlockState stone_;
  reim::world::BlockState air_;
};

reim::core::Player test_player() {
  reim::core::Player player;
  player.x = 0.5;
  player.y = 0.0;
  player.z = 0.5;
  player.yaw = 270.0f; /* forward = +X */
  player.pitch = 0.0f;
  player.on_ground = true;
  return player;
}

SurvivalContext flat_context() {
  SurvivalContext context;
  context.on_ground = true;
  context.difficulty = 2;
  return context;
}

} // namespace

TEST_CASE("M3-F survival damage applies invulnerability and death") {
  SurvivalState state;
  reim::game::survival_reset(state, true);
  REQUIRE(state.health == 20.0f);
  CHECK(reim::game::survival_damage(state, 5.0f, DamageCause::mob));
  CHECK(state.health == 15.0f);
  CHECK(state.hurt_time == 10);
  /* Invulnerability window rejects a second hit in the same moment. */
  CHECK_FALSE(reim::game::survival_damage(state, 5.0f, DamageCause::mob));
  CHECK(state.health == 15.0f);

  SurvivalContext context = flat_context();
  for (int i = 0; i < 10; ++i) reim::game::survival_tick(state, context);
  CHECK(state.invuln == 0);
  CHECK(reim::game::survival_damage(state, 20.0f, DamageCause::lava));
  CHECK(state.dead);
  CHECK(state.deaths == 1);
  CHECK_FALSE(reim::game::survival_damage(state, 1.0f, DamageCause::mob));

  const uint32_t level = state.xp_level;
  reim::game::survival_respawn(state);
  CHECK_FALSE(state.dead);
  CHECK(state.health == state.max_health);
  CHECK(state.food == 20.0f);
  CHECK(state.invuln == 20);
  CHECK(state.deaths == 1);
  CHECK(state.xp_level == level);
}

TEST_CASE("M3-F fall damage and starvation") {
  CHECK(reim::game::survival_fall_damage(3.0) == 0.0f);
  CHECK(reim::game::survival_fall_damage(4.0) == 1.0f);
  CHECK(reim::game::survival_fall_damage(10.5) == 7.0f);

  SurvivalState state;
  reim::game::survival_reset(state, true);
  SurvivalContext falling = flat_context();
  falling.on_ground = false;
  falling.vy = -0.5;
  for (int i = 0; i < 20; ++i) reim::game::survival_tick(state, falling);
  SurvivalContext landing = flat_context();
  landing.vy = 0.0;
  reim::game::survival_tick(state, landing);
  CHECK(state.health == 13.0f);
  CHECK(state.last_damage == DamageCause::fall);

  SurvivalState hungry;
  reim::game::survival_reset(hungry, true);
  hungry.food = 0.0f;
  hungry.saturation = 0.0f;
  for (int i = 0; i < 90; ++i) {
    reim::game::survival_tick(hungry, flat_context());
  }
  CHECK(hungry.health <= 19.0f);
  CHECK(hungry.last_damage == DamageCause::starve);
}

TEST_CASE("M3-F regeneration and experience curve") {
  SurvivalState state;
  reim::game::survival_reset(state, true);
  state.health = 10.0f;
  for (int i = 0; i < 81; ++i) {
    reim::game::survival_tick(state, flat_context());
  }
  CHECK(state.health >= 11.0f);

  reim::game::survival_add_xp(state, 100);
  CHECK(state.xp_total == 100);
  CHECK(state.xp_level > 0);
  CHECK(reim::game::survival_xp_progress(state) >= 0.0f);
  CHECK(reim::game::survival_xp_progress(state) <= 1.0f);
}

TEST_CASE("M3-F entity raycast, melee, loot and removal") {
  Tables tables;
  REQUIRE(tables.ok);
  FlatWorld world(tables.blocks);
  EntityManager entities;
  entities.initialize(&world, &tables.items, 12345);
  entities.set_natural_spawning(false);
  reim::core::Player player = test_player();
  player.pitch = 20.0f; /* aim at the cow body below eye height */
  const uint64_t cow = entities.spawn(EntityType::cow, 2.0, 0.0, 0.5);
  REQUIRE(entities.size() == 1);

  const reim::game::EntityHit hit = entities.raycast(player, 3.0);
  REQUIRE(hit.valid);
  CHECK(hit.id == cow);
  CHECK(hit.type == EntityType::cow);

  reim::game::EntityEvents events;
  CHECK(entities.damage(cow, 4.0f, player, events));
  CHECK(events.sounds.size() == 1);
  CHECK_FALSE(events.kills);
  CHECK(entities.view().front().health == 6.0f);

  reim::game::EntityEvents death_events;
  CHECK(entities.damage(cow, 30.0f, player, death_events));
  CHECK(death_events.kills == 1);
  CHECK(death_events.xp > 0);
  CHECK_FALSE(death_events.drops.empty());

  for (int i = 0; i < 25; ++i) {
    reim::game::EntityEvents tick_events;
    entities.tick(player, world.environment(), 2, tick_events);
  }
  CHECK(entities.size() == 0);
}

TEST_CASE("M3-F hostile AI closes and damages the player") {
  Tables tables;
  REQUIRE(tables.ok);
  FlatWorld world(tables.blocks);
  EntityManager entities;
  entities.initialize(&world, &tables.items, 7);
  entities.set_natural_spawning(false);
  reim::core::Player player = test_player();
  entities.spawn(EntityType::spider, 2.5, 0.0, 0.5);

  bool damaged = false;
  for (int i = 0; i < 80 && !damaged; ++i) {
    reim::game::EntityEvents events;
    entities.tick(player, world.environment(), 2, events);
    damaged = !events.player_damage.empty();
  }
  CHECK(damaged);
}

TEST_CASE("M3-F creeper explosion edits blocks and hurts the player") {
  Tables tables;
  REQUIRE(tables.ok);
  FlatWorld world(tables.blocks);
  EntityManager entities;
  entities.initialize(&world, &tables.items, 9);
  entities.set_natural_spawning(false);
  reim::core::Player player = test_player();
  entities.spawn(EntityType::creeper, 2.5, 0.0, 0.5);

  bool exploded = false;
  bool player_hurt = false;
  std::size_t edits = 0;
  for (int i = 0; i < 45; ++i) {
    reim::game::EntityEvents events;
    entities.tick(player, world.environment(), 2, events);
    if (!events.block_edits.empty()) {
      exploded = true;
      edits = events.block_edits.size();
    }
    for (const reim::game::MobDamageEvent& damage : events.player_damage) {
      if (damage.cause == DamageCause::explosion) player_hurt = true;
    }
  }
  CHECK(exploded);
  CHECK(edits > 0);
  CHECK(player_hurt);
  CHECK(entities.counts().count("minecraft:creeper") == 0);
}

TEST_CASE("M3-F native entity model definitions cover eight mobs") {
  const nlohmann::json root =
      nlohmann::json::parse(reim::game::entity_model_defs_json());
  REQUIRE(root["format"] == 1);
  REQUIRE(root["models"].is_array());
  REQUIRE(root["models"].size() == 8);
  for (uint32_t i = 0; i < 8; ++i) {
    const nlohmann::json& model = root["models"][i];
    CHECK(model["id"] == static_cast<int>(i + 1));
    CHECK(model["parts"].is_array());
    CHECK(model["parts"].size() >= 6);
    for (const nlohmann::json& part : model["parts"]) {
      CHECK(part.contains("name"));
      CHECK(part.contains("pivot"));
      CHECK(part.contains("size"));
    }
  }
}
