#include "game/recipes.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace reim::game {
namespace {

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

std::string_view bare_name(const ItemTable& items, uint32_t item_id) {
  const std::string& name = items.def(item_id).name;
  const size_t colon = name.find(':');
  return colon == std::string::npos ? std::string_view(name)
                                    : std::string_view(name).substr(colon + 1);
}

Ingredient item(uint32_t item_id) {
  Ingredient ingredient;
  ingredient.item_id = item_id;
  return ingredient;
}

Ingredient tag(std::string name) {
  Ingredient ingredient;
  ingredient.tag = std::move(name);
  return ingredient;
}

ItemStack result(uint32_t item_id, int count) {
  ItemStack stack;
  stack.item_id = item_id;
  stack.count = count;
  return stack;
}

std::array<Ingredient, 9> pattern_of(std::initializer_list<Ingredient> cells) {
  std::array<Ingredient, 9> out{};
  size_t index = 0;
  for (const Ingredient& cell : cells) {
    if (index < out.size()) out[index++] = cell;
  }
  return out;
}

constexpr std::array<std::string_view, 13> kWoods = {
    "acacia", "bamboo",  "birch",    "cherry", "crimson", "dark_oak", "jungle",
    "mangrove", "oak",   "pale_oak", "poplar", "spruce",  "warped"};

} // namespace

bool Ingredient::matches(const ItemTable& items, const ItemStack& stack) const {
  if (stack.empty()) return false;
  if (!tag.empty()) {
    const std::string_view bare = bare_name(items, stack.item_id);
    if (tag == "planks") return ends_with(bare, "_planks");
    if (tag == "logs") {
      return ends_with(bare, "_log") || ends_with(bare, "_wood") ||
             ends_with(bare, "_stem") || ends_with(bare, "_hyphae") ||
             bare == "bamboo_block";
    }
    if (tag == "coals") return bare == "coal" || bare == "charcoal";
    if (tag == "stone_tool_materials") {
      return bare == "cobblestone" || bare == "cobbled_deepslate" ||
             bare == "blackstone" || bare == "cobblestone_slab";
    }
    return false;
  }
  return stack.item_id == item_id;
}

void RecipeBook::add_shaped(int width, int height,
                            const std::array<Ingredient, 9>& pattern,
                            ItemStack output) {
  if (width <= 0 || height <= 0 || width > 3 || height > 3 ||
      output.empty()) {
    return;
  }
  Recipe recipe;
  recipe.width = width;
  recipe.height = height;
  recipe.pattern = pattern;
  recipe.result = std::move(output);
  shaped_.push_back(std::move(recipe));
}

void RecipeBook::add_shapeless(std::vector<Ingredient> ingredients,
                               ItemStack output) {
  if (ingredients.empty() || output.empty()) return;
  Recipe recipe;
  recipe.shapeless = std::move(ingredients);
  recipe.result = std::move(output);
  shapeless_.push_back(std::move(recipe));
}

void RecipeBook::add_smelting(const ItemTable& items, std::string_view input,
                              std::string_view output) {
  const uint32_t input_id = items.find(input);
  const uint32_t output_id = items.find(output);
  if (input_id == 0 || output_id == 0) return;
  smelting_[input_id] = result(output_id, 1);
}

void RecipeBook::load(const ItemTable& items) {
  shaped_.clear();
  shapeless_.clear();
  smelting_.clear();

  const uint32_t planks_tag = 0;  /* tags are matched by name */
  (void)planks_tag;

  const uint32_t sticks = items.find("minecraft:stick");
  const uint32_t crafting_table = items.find("minecraft:crafting_table");
  const uint32_t chest = items.find("minecraft:chest");
  const uint32_t furnace = items.find("minecraft:furnace");
  const uint32_t torch = items.find("minecraft:torch");

  for (std::string_view wood : kWoods) {
    const std::string planks_name = "minecraft:" + std::string(wood) + "_planks";
    const uint32_t planks = items.find(planks_name);
    if (planks == 0) continue;

    /* Log/stem/hyphae -> 4 planks (shapeless, one item). */
    static constexpr std::array<std::string_view, 6> kSuffixes = {
        "_log", "_wood", "_stem", "_hyphae", "_block", ""};
    for (std::string_view suffix : kSuffixes) {
      if (suffix == "_block" && wood != "bamboo") continue;
      if (suffix.empty()) continue;
      const uint32_t log = items.find("minecraft:" + std::string(wood) + std::string(suffix));
      if (log == 0) continue;
      add_shapeless({item(log)}, result(planks, 4));
    }
    for (std::string_view suffix : {"_log", "_wood", "_stem", "_hyphae"}) {
      const uint32_t stripped =
          items.find("minecraft:stripped_" + std::string(wood) + std::string(suffix));
      if (stripped == 0) continue;
      add_shapeless({item(stripped)}, result(planks, 4));
    }

    if (sticks != 0) {
      add_shaped(1, 2, pattern_of({item(planks), item(planks)}),
                 result(sticks, 4));
    }
    if (crafting_table != 0) {
      add_shaped(2, 2, pattern_of({item(planks), item(planks), item(planks),
                                   item(planks)}),
                 result(crafting_table, 1));
    }
    if (chest != 0) {
      add_shaped(3, 3,
                 pattern_of({item(planks), item(planks), item(planks),
                             item(planks), Ingredient{}, item(planks),
                             item(planks), item(planks), item(planks)}),
                 result(chest, 1));
    }
    const uint32_t stairs = items.find("minecraft:" + std::string(wood) + "_stairs");
    if (stairs != 0) {
      add_shaped(3, 3,
                 pattern_of({item(planks), Ingredient{}, Ingredient{},
                             item(planks), item(planks), Ingredient{},
                             item(planks), item(planks), item(planks)}),
                 result(stairs, 4));
    }
    const uint32_t slab = items.find("minecraft:" + std::string(wood) + "_slab");
    if (slab != 0) {
      add_shaped(3, 1,
                 pattern_of({item(planks), item(planks), item(planks)}),
                 result(slab, 6));
    }
  }

  if (furnace != 0) {
    const uint32_t cobblestone = items.find("minecraft:cobblestone");
    if (cobblestone != 0) {
      add_shaped(3, 3,
                 pattern_of({item(cobblestone), item(cobblestone), item(cobblestone),
                             item(cobblestone), Ingredient{}, item(cobblestone),
                             item(cobblestone), item(cobblestone), item(cobblestone)}),
                 result(furnace, 1));
    }
  }
  if (torch != 0 && sticks != 0) {
    add_shaped(1, 2, pattern_of({tag("coals"), item(sticks)}), result(torch, 4));
  }
  const uint32_t bread = items.find("minecraft:bread");
  const uint32_t wheat = items.find("minecraft:wheat");
  if (bread != 0 && wheat != 0) {
    add_shapeless({item(wheat), item(wheat), item(wheat)}, result(bread, 1));
  }

  struct ToolSet {
    const char* prefix;
    Ingredient material;
  };
  const uint32_t cobblestone = items.find("minecraft:cobblestone");
  const uint32_t iron = items.find("minecraft:iron_ingot");
  const uint32_t diamond = items.find("minecraft:diamond");
  std::vector<ToolSet> sets;
  sets.push_back({"wooden", tag("planks")});
  if (cobblestone != 0) sets.push_back({"stone", item(cobblestone)});
  if (iron != 0) sets.push_back({"iron", item(iron)});
  if (diamond != 0) sets.push_back({"diamond", item(diamond)});
  for (const ToolSet& set : sets) {
    const std::string base = "minecraft:" + std::string(set.prefix) + "_";
    if (sticks == 0) break;
    const struct {
      const char* name;
      int width;
      int height;
      std::array<Ingredient, 9> pattern;
    } tools[] = {
        {"pickaxe", 3, 3,
         pattern_of({set.material, set.material, set.material, Ingredient{},
                     item(sticks), Ingredient{}, Ingredient{}, item(sticks),
                     Ingredient{}})},
        {"axe", 2, 3,
         pattern_of({set.material, set.material, set.material, item(sticks),
                     Ingredient{}, item(sticks)})},
        {"shovel", 1, 3,
         pattern_of({set.material, item(sticks), item(sticks)})},
        {"sword", 1, 3,
         pattern_of({set.material, set.material, item(sticks)})},
        {"hoe", 2, 3,
         pattern_of({set.material, set.material, Ingredient{}, item(sticks),
                     Ingredient{}, item(sticks)})},
    };
    for (const auto& tool : tools) {
      const uint32_t output = items.find(base + tool.name);
      if (output == 0) continue;
      add_shaped(tool.width, tool.height, tool.pattern, result(output, 1));
    }
  }

  add_smelting(items, "minecraft:iron_ore", "minecraft:iron_ingot");
  add_smelting(items, "minecraft:deepslate_iron_ore", "minecraft:iron_ingot");
  add_smelting(items, "minecraft:raw_iron", "minecraft:iron_ingot");
  add_smelting(items, "minecraft:gold_ore", "minecraft:gold_ingot");
  add_smelting(items, "minecraft:deepslate_gold_ore", "minecraft:gold_ingot");
  add_smelting(items, "minecraft:raw_gold", "minecraft:gold_ingot");
  add_smelting(items, "minecraft:copper_ore", "minecraft:copper_ingot");
  add_smelting(items, "minecraft:deepslate_copper_ore", "minecraft:copper_ingot");
  add_smelting(items, "minecraft:raw_copper", "minecraft:copper_ingot");
  add_smelting(items, "minecraft:sand", "minecraft:glass");
  add_smelting(items, "minecraft:red_sand", "minecraft:glass");
  add_smelting(items, "minecraft:cobblestone", "minecraft:stone");
  add_smelting(items, "minecraft:cobbled_deepslate", "minecraft:deepslate");
  add_smelting(items, "minecraft:porkchop", "minecraft:cooked_porkchop");
  add_smelting(items, "minecraft:beef", "minecraft:cooked_beef");
  add_smelting(items, "minecraft:chicken", "minecraft:cooked_chicken");
  add_smelting(items, "minecraft:mutton", "minecraft:cooked_mutton");
  for (std::string_view wood : kWoods) {
    for (std::string_view suffix : {"_log", "_wood", "_stem", "_hyphae"}) {
      const std::string log =
          "minecraft:" + std::string(wood) + std::string(suffix);
      add_smelting(items, log, "minecraft:charcoal");
    }
  }
}

bool RecipeBook::match_crafting(const ItemTable& items, const CraftingGrid& grid,
                                ItemStack& out) const {
  int min_row = grid.size;
  int max_row = -1;
  int min_col = grid.size;
  int max_col = -1;
  int occupied = 0;
  for (int row = 0; row < grid.size; ++row) {
    for (int column = 0; column < grid.size; ++column) {
      if (grid.at(row, column).empty()) continue;
      ++occupied;
      min_row = std::min(min_row, row);
      max_row = std::max(max_row, row);
      min_col = std::min(min_col, column);
      max_col = std::max(max_col, column);
    }
  }
  if (occupied == 0) return false;

  const int grid_height = max_row - min_row + 1;
  const int grid_width = max_col - min_col + 1;
  for (const Recipe& recipe : shaped_) {
    if (recipe.width != grid_width || recipe.height != grid_height) continue;
    bool matched = true;
    for (int row = 0; row < recipe.height && matched; ++row) {
      for (int column = 0; column < recipe.width && matched; ++column) {
        const Ingredient& ingredient = recipe.pattern[static_cast<size_t>(
            row * recipe.width + column)];
        const ItemStack& stack = grid.at(min_row + row, min_col + column);
        if (ingredient.empty()) {
          if (!stack.empty()) matched = false;
        } else if (!ingredient.matches(items, stack)) {
          matched = false;
        }
      }
    }
    if (matched) {
      out = recipe.result;
      return true;
    }
  }
  for (const Recipe& recipe : shapeless_) {
    if (static_cast<int>(recipe.shapeless.size()) != occupied) continue;
    std::vector<bool> used(static_cast<size_t>(grid.size * grid.size), false);
    bool matched = true;
    for (const Ingredient& ingredient : recipe.shapeless) {
      bool found = false;
      for (int row = 0; row < grid.size && !found; ++row) {
        for (int column = 0; column < grid.size && !found; ++column) {
          const size_t index = static_cast<size_t>(row * grid.size + column);
          if (used[index]) continue;
          const ItemStack& stack = grid.at(row, column);
          if (stack.empty() || !ingredient.matches(items, stack)) continue;
          used[index] = true;
          found = true;
        }
      }
      if (!found) {
        matched = false;
        break;
      }
    }
    if (matched) {
      out = recipe.result;
      return true;
    }
  }
  return false;
}

void RecipeBook::consume_crafting(CraftingGrid& grid) {
  for (int row = 0; row < grid.size; ++row) {
    for (int column = 0; column < grid.size; ++column) {
      ItemStack& stack = grid.at(row, column);
      if (stack.empty()) continue;
      --stack.count;
      if (stack.count <= 0) stack.clear();
    }
  }
}

bool RecipeBook::smelt(uint32_t input_item, ItemStack& output) const {
  const auto found = smelting_.find(input_item);
  if (found == smelting_.end()) return false;
  output = found->second;
  return true;
}

int RecipeBook::fuel_ticks(const ItemTable& items, const ItemStack& stack) const {
  if (stack.empty()) return 0;
  return items.def(stack.item_id).fuel_ticks;
}

} // namespace reim::game
