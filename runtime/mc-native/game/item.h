#pragma once

#include "data/block_table.h"
#include "reim_game.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace reim::game {

/*
 * Runtime item table (M3-D3). Block items are generated from the Converter's
 * blocks.json so the item namespace cannot drift from the block namespace;
 * items that are not blocks (tools, food, ingots) and the block behaviour table
 * are native because their source in 26.3 is Java code, not the data reports
 * (M3_PLAN 9.2). Container/recipe/loot data is a native M3 subset until the
 * Converter emits recipes.json/loot_tables.json (M3-K4, phase 4B); that gap is
 * recorded in M3_ACCEPTANCE.md.
 *
 * Item ids are stable for the instance lifetime. Block item ids equal the
 * representative block id; non-block ids start at kNativeItemBase.
 */
enum ItemFlags : uint32_t {
  ITEM_FLAG_NONE = 0,
  ITEM_FLAG_BLOCK = 1u << 0,
  ITEM_FLAG_TOOL = 1u << 1,
  ITEM_FLAG_FOOD = 1u << 2,
  ITEM_FLAG_FUEL = 1u << 3,
  ITEM_FLAG_PLACEABLE = 1u << 4
};

enum class ToolClass : uint8_t { none = 0, pickaxe = 1, axe = 2, shovel = 3, sword = 4, hoe = 5 };

constexpr uint32_t kNativeItemBase = 100000u;

struct ItemDef {
  uint32_t id = 0;
  std::string name;
  std::string display;
  uint32_t max_stack = 64;
  int32_t max_damage = 0;
  uint32_t flags = ITEM_FLAG_NONE;
  uint32_t block_id = 0;       /* representative block id when placeable */
  ToolClass tool = ToolClass::none;
  int tool_tier = 0;           /* 0 none, 1 wood, 2 stone, 3 iron, 4 diamond */
  float attack_damage = 1.0f;
  int food = 0;                /* hunger points restored */
  float saturation = 0.0f;
  int fuel_ticks = 0;

  bool is_block() const { return (flags & ITEM_FLAG_BLOCK) != 0; }
  bool is_tool() const { return (flags & ITEM_FLAG_TOOL) != 0; }
  bool is_food() const { return (flags & ITEM_FLAG_FOOD) != 0; }
};

struct ItemStack {
  uint32_t item_id = 0;
  int32_t count = 0;
  int32_t damage = 0;
  uint32_t flags = 0;

  bool empty() const { return item_id == 0 || count <= 0; }
  void clear() { item_id = 0; count = 0; damage = 0; flags = 0; }
  uint32_t damage_flag() const {
    return damage > 0 ? 0x00000001u : 0u;
  }
};

/* Copies a stack into the frozen Contract layout (count 0 = empty). */
void to_contract(const ItemStack& stack, ReimItemStack& out);

class ItemTable {
 public:
  bool load(const data::BlockTable& blocks,
            const std::filesystem::path& generated_dir, std::string& error);

  /* Empty def (id 0) when unknown. */
  const ItemDef& def(uint32_t item_id) const;
  uint32_t find(std::string_view name) const;
  uint32_t block_item(uint32_t block_id) const;
  uint32_t item_block(uint32_t item_id) const;
  std::string display_name(uint32_t item_id) const;
  std::string item_name(uint32_t item_id) const;

  uint32_t size() const { return static_cast<uint32_t>(defs_.size()); }
  const std::unordered_map<uint32_t, ItemDef>& defs() const { return defs_; }

 private:
  ItemDef& ensure(uint32_t id);
  void add_native(std::string_view name, uint32_t flags, int max_stack,
                  int max_damage, ToolClass tool, int tier, float attack_damage,
                  int food, float saturation, int fuel_ticks);

  std::unordered_map<uint32_t, ItemDef> defs_;
  std::unordered_map<std::string, uint32_t> by_name_;
  std::unordered_map<uint32_t, uint32_t> block_to_item_;
  std::unordered_map<uint32_t, uint32_t> item_to_block_;
  uint32_t next_native_ = kNativeItemBase;
};

struct BlockRules {
  float hardness = 1.0f;
  ToolClass tool = ToolClass::none;
  int required_tier = 0;
  bool needs_tool = false;
  bool unbreakable = false;
};

BlockRules block_rules(std::string_view block_name);

/* Effective harvest tier of a held stack (0 = no tool). */
int stack_tool_tier(const ItemTable& items, const ItemStack& held, ToolClass& out_class);

/* Harvest check: a required tool must be the right class and tier. */
bool can_harvest(const BlockRules& rules, const ItemStack& held,
                 const ItemTable& items);

/* Ticks to break a block with the held stack (>= 1 for breakable blocks). */
int break_ticks(const BlockRules& rules, const ItemStack& held,
                const ItemTable& items, bool can_harvest_now);

struct ItemDrop {
  uint32_t item_id = 0;
  int min_count = 1;
  int max_count = 1;
};

uint64_t next_random(uint64_t& state);

/* Deterministic block loot for the M3 subset. Empty when the block is not
 * harvestable (wrong tool) or has no item form. Container contents are spawned
 * separately by the gameplay layer. */
std::vector<ItemDrop> block_drops(const ItemTable& items,
                                  std::string_view block_name,
                                  bool can_harvest_now, uint64_t& rng);

const char* tool_class_name(ToolClass tool);

} // namespace reim::game
