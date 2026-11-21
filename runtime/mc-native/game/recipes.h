#pragma once

#include "game/inventory.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace reim::game {

/*
 * M3 subset recipe book (M3-E3/E4). 26.3 recipe JSON is a Converter deliverable
 * (M3-K4, phase 4B); until it lands the runtime carries a native table for the
 * first content set in M3_PLAN 1.3. The table is explicit so the gap is
 * visible in the acceptance record rather than hidden behind a fake data load.
 *
 * Tags are a deliberately small subset: planks, logs, coals, stone tool
 * materials. Unsupported data-pack tags are reported by the Converter gap
 * report, not silently accepted here.
 */
struct Ingredient {
  uint32_t item_id = 0;
  std::string tag;

  bool empty() const { return item_id == 0 && tag.empty(); }
  bool matches(const ItemTable& items, const ItemStack& stack) const;
};

struct Recipe {
  int width = 0;
  int height = 0;
  std::array<Ingredient, 9> pattern{};
  std::vector<Ingredient> shapeless;
  ItemStack result;

  bool is_shaped() const { return width > 0 && height > 0; }
};

class RecipeBook {
 public:
  void load(const ItemTable& items);

  /* Returns true and fills result when the grid matches. */
  bool match_crafting(const ItemTable& items, const CraftingGrid& grid,
                      ItemStack& result) const;

  /* Consumes one item from every occupied grid slot. */
  static void consume_crafting(CraftingGrid& grid);

  bool smelt(uint32_t input_item, ItemStack& output) const;
  int fuel_ticks(const ItemTable& items, const ItemStack& stack) const;

  size_t crafting_count() const { return shaped_.size() + shapeless_.size(); }
  size_t smelting_count() const { return smelting_.size(); }
  const std::vector<Recipe>& shaped_recipes() const { return shaped_; }

 private:
  void add_shaped(int width, int height,
                  const std::array<Ingredient, 9>& pattern, ItemStack result);
  void add_shapeless(std::vector<Ingredient> ingredients, ItemStack result);
  void add_smelting(const ItemTable& items, std::string_view input,
                    std::string_view output);

  std::vector<Recipe> shaped_;
  std::vector<Recipe> shapeless_;
  std::unordered_map<uint32_t, ItemStack> smelting_;
};

} // namespace reim::game
