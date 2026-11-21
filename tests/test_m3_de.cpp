/*
 * M3-D/E unit tests that do not need a world: item table, inventory stack
 * rules, shaped/shapeless recipes, smelting and loot determinism. Integration
 * and UI behaviour live in tests/scenarios/m3{d,e,h}.
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "catch.hpp"

#include "data/block_table.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/recipes.h"

#include <filesystem>
#include <string>

#ifndef REIM_TEST_GENERATED_DIR
#define REIM_TEST_GENERATED_DIR "generated/26.3"
#endif

namespace {

struct Tables {
  reim::data::BlockTable blocks;
  reim::game::ItemTable items;
  reim::game::RecipeBook recipes;
  bool ok = false;

  Tables() {
    std::string error;
    const std::filesystem::path dir(REIM_TEST_GENERATED_DIR);
    if (!blocks.load(dir, error)) return;
    if (!items.load(blocks, dir, error)) return;
    recipes.load(items);
    ok = true;
  }
};

reim::game::ItemStack stack(uint32_t item, int count) {
  reim::game::ItemStack value;
  value.item_id = item;
  value.count = count;
  return value;
}

} // namespace

TEST_CASE("M3-D item and block tables cover the first content set") {
  Tables t;
  REQUIRE(t.ok);
  for (const char* name : {
           "minecraft:oak_log", "minecraft:oak_planks", "minecraft:stick",
           "minecraft:wooden_pickaxe", "minecraft:stone_pickaxe",
           "minecraft:iron_ingot", "minecraft:coal", "minecraft:apple",
           "minecraft:crafting_table", "minecraft:furnace", "minecraft:chest",
           "minecraft:torch"}) {
    CHECK(t.items.find(name) != 0);
  }
  const uint32_t log = t.items.find("minecraft:oak_log");
  const uint32_t pick = t.items.find("minecraft:wooden_pickaxe");
  CHECK(t.items.def(log).is_block());
  CHECK(t.items.def(pick).is_tool());
  CHECK(t.items.def(pick).max_stack == 1);

  const reim::game::BlockRules stone = reim::game::block_rules("minecraft:stone");
  CHECK(stone.needs_tool);
  CHECK(stone.tool == reim::game::ToolClass::pickaxe);
  CHECK(!reim::game::can_harvest(stone, stack(0, 0), t.items));
  CHECK(reim::game::can_harvest(stone, stack(t.items.find("minecraft:wooden_pickaxe"), 1),
                                t.items));
  CHECK(reim::game::break_ticks(stone, stack(t.items.find("minecraft:diamond_pickaxe"), 1),
                                t.items, true) <
        reim::game::break_ticks(stone, stack(0, 0), t.items, false));
}

TEST_CASE("M3-E shaped tool, shapeless planks and smelting recipes match") {
  Tables t;
  REQUIRE(t.ok);
  const uint32_t planks = t.items.find("minecraft:oak_planks");
  const uint32_t sticks = t.items.find("minecraft:stick");
  const uint32_t pick = t.items.find("minecraft:wooden_pickaxe");
  REQUIRE(planks != 0);
  REQUIRE(sticks != 0);
  REQUIRE(pick != 0);

  reim::game::CraftingGrid grid;
  grid.size = 3;
  grid.slots[0] = stack(planks, 1);
  grid.slots[1] = stack(planks, 1);
  grid.slots[2] = stack(planks, 1);
  grid.slots[4] = stack(sticks, 1);
  grid.slots[7] = stack(sticks, 1);
  reim::game::ItemStack result;
  REQUIRE(t.recipes.match_crafting(t.items, grid, result));
  CHECK(result.item_id == pick);
  CHECK(result.count == 1);

  reim::game::CraftingGrid log_grid;
  log_grid.size = 2;
  log_grid.slots[0] = stack(t.items.find("minecraft:oak_log"), 1);
  REQUIRE(t.recipes.match_crafting(t.items, log_grid, result));
  CHECK(result.item_id == planks);
  CHECK(result.count == 4);

  reim::game::ItemStack smelted;
  const uint32_t raw_iron = t.items.find("minecraft:raw_iron");
  REQUIRE(raw_iron != 0);
  REQUIRE(t.recipes.smelt(raw_iron, smelted));
  CHECK(smelted.item_id == t.items.find("minecraft:iron_ingot"));
}

TEST_CASE("M3-E inventory stacking and loot determinism") {
  Tables t;
  REQUIRE(t.ok);
  const uint32_t cobble = t.items.find("minecraft:cobblestone");
  const uint32_t pick = t.items.find("minecraft:stone_pickaxe");
  REQUIRE(cobble != 0);
  REQUIRE(pick != 0);

  reim::game::Inventory inventory;
  CHECK(inventory.add(t.items, stack(cobble, 70)));
  CHECK(inventory.count(cobble) == 70);
  CHECK(inventory.slots[0].count == 64);
  CHECK(inventory.slots[1].count == 6);

  uint64_t rng_a = 0x1234;
  uint64_t rng_b = 0x1234;
  const auto drops_a = reim::game::block_drops(
      t.items, "minecraft:stone", true, rng_a);
  const auto drops_b = reim::game::block_drops(
      t.items, "minecraft:stone", true, rng_b);
  REQUIRE(drops_a.size() == 1);
  REQUIRE(drops_b.size() == 1);
  CHECK(drops_a[0].item_id == cobble);
  CHECK(drops_a[0].min_count == drops_b[0].min_count);

  uint64_t rng_c = 0x1234;
  CHECK(reim::game::block_drops(t.items, "minecraft:stone", false, rng_c).empty());
}
