#include "game/item.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace reim::game {
namespace {

std::string_view without_namespace(std::string_view name) {
  const size_t colon = name.find(':');
  return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

std::string display_from_name(std::string_view name) {
  const std::string_view bare = without_namespace(name);
  std::string out;
  out.reserve(bare.size());
  bool upper = true;
  for (char c : bare) {
    if (c == '_' || c == '/') {
      out.push_back(' ');
      upper = true;
      continue;
    }
    out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                        : c);
    upper = false;
  }
  return out;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

bool is_plain_item_block(std::string_view bare) {
  static constexpr std::array<std::string_view, 15> kSkip = {
      "air",          "water",          "lava",        "fire",
      "soul_fire",    "moving_piston",  "piston_head", "bubble_column",
      "light",        "barrier",        "structure_void", "end_portal",
      "nether_portal", "tall_seagrass", "seagrass"};
  return std::find(kSkip.begin(), kSkip.end(), bare) != kSkip.end();
}

int fuel_for_block(std::string_view bare) {
  if (ends_with(bare, "_planks") || ends_with(bare, "_log") || ends_with(bare, "_wood") ||
      ends_with(bare, "_fence") || ends_with(bare, "_stairs") ||
      ends_with(bare, "_slab")) {
    return 300;
  }
  if (ends_with(bare, "_sapling")) return 100;
  if (bare == "coal_block") return 16000;
  if (bare == "ladder" || bare == "crafting_table" || bare == "chest" ||
      bare == "barrel" || bare == "bookshelf") {
    return 300;
  }
  return 0;
}

float tier_speed(int tier) {
  switch (tier) {
    case 1: return 2.0f;
    case 2: return 4.0f;
    case 3: return 6.0f;
    case 4: return 8.0f;
    default: return 1.0f;
  }
}

} // namespace

const char* tool_class_name(ToolClass tool) {
  switch (tool) {
    case ToolClass::pickaxe: return "pickaxe";
    case ToolClass::axe: return "axe";
    case ToolClass::shovel: return "shovel";
    case ToolClass::sword: return "sword";
    case ToolClass::hoe: return "hoe";
    default: return "none";
  }
}

void to_contract(const ItemStack& stack, ReimItemStack& out) {
  std::memset(&out, 0, sizeof(out));
  out.struct_size = sizeof(ReimItemStack);
  if (stack.empty()) return;
  out.item_id = stack.item_id;
  out.count = static_cast<uint32_t>(std::max(0, stack.count));
  out.damage = stack.damage;
  out.flags = stack.flags | stack.damage_flag();
}

ItemDef& ItemTable::ensure(uint32_t id) {
  auto found = defs_.find(id);
  if (found != defs_.end()) return found->second;
  ItemDef def;
  def.id = id;
  return defs_.emplace(id, std::move(def)).first->second;
}

void ItemTable::add_native(std::string_view name, uint32_t flags, int max_stack,
                           int max_damage, ToolClass tool, int tier,
                           float attack_damage, int food, float saturation,
                           int fuel_ticks) {
  const std::string full(name);
  if (by_name_.find(full) != by_name_.end()) return;
  const uint32_t id = next_native_++;
  ItemDef def;
  def.id = id;
  def.name = full;
  def.display = display_from_name(full);
  def.max_stack = max_stack > 0 ? static_cast<uint32_t>(max_stack) : 64u;
  def.max_damage = max_damage;
  def.flags = flags;
  def.tool = tool;
  def.tool_tier = tier;
  def.attack_damage = attack_damage;
  def.food = food;
  def.saturation = saturation;
  def.fuel_ticks = fuel_ticks;
  by_name_.emplace(full, id);
  defs_.emplace(id, std::move(def));
}

bool ItemTable::load(const data::BlockTable& blocks,
                     const std::filesystem::path& /*generated_dir*/,
                     std::string& error) {
  defs_.clear();
  by_name_.clear();
  block_to_item_.clear();
  item_to_block_.clear();
  next_native_ = kNativeItemBase;
  error.clear();

  std::unordered_set<std::string> seen;
  for (uint32_t block_id = 0; block_id < blocks.size(); ++block_id) {
    const data::BlockDef& block = blocks.def(block_id);
    if (block.name.empty() || block.name == "minecraft:air") continue;
    if (!seen.insert(block.name).second) continue;
    const std::string_view bare = without_namespace(block.name);
    if (is_plain_item_block(bare)) continue;
    const uint32_t base = blocks.find(block.name);
    if (base == 0) continue;

    ItemDef def;
    def.id = base;
    def.name = block.name;
    def.display = display_from_name(block.name);
    def.max_stack = 64;
    def.flags = ITEM_FLAG_BLOCK | ITEM_FLAG_PLACEABLE;
    def.block_id = base;
    def.fuel_ticks = fuel_for_block(bare);
    if (def.fuel_ticks > 0) def.flags |= ITEM_FLAG_FUEL;
    block_to_item_[base] = base;
    item_to_block_[base] = base;
    by_name_.emplace(def.name, base);
    defs_.emplace(base, std::move(def));
  }

  /* Non-block items. Tools use the wood/stone/iron/diamond tiers of M3 1.3. */
  add_native("minecraft:stick", ITEM_FLAG_FUEL, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 100);
  add_native("minecraft:coal", ITEM_FLAG_FUEL, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 1600);
  add_native("minecraft:charcoal", ITEM_FLAG_FUEL, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 1600);
  add_native("minecraft:raw_iron", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:raw_gold", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:raw_copper", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:iron_ingot", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:gold_ingot", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:copper_ingot", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:diamond", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:emerald", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:redstone", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:lapis_lazuli", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:quartz", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:amethyst_shard", ITEM_FLAG_NONE, 64, 0, ToolClass::none,
             0, 1.0f, 0, 0.0f, 0);
  add_native("minecraft:wheat", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:wheat_seeds", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:apple", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0, 1.0f,
             4, 2.4f, 0);
  add_native("minecraft:bread", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0, 1.0f,
             5, 6.0f, 0);
  add_native("minecraft:beef", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0, 1.0f,
             3, 1.8f, 0);
  add_native("minecraft:cooked_beef", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0,
             1.0f, 8, 12.8f, 0);
  add_native("minecraft:porkchop", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0,
             1.0f, 3, 1.8f, 0);
  add_native("minecraft:cooked_porkchop", ITEM_FLAG_FOOD, 64, 0,
             ToolClass::none, 0, 1.0f, 8, 12.8f, 0);
  add_native("minecraft:chicken", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0,
             1.0f, 2, 1.2f, 0);
  add_native("minecraft:cooked_chicken", ITEM_FLAG_FOOD, 64, 0,
             ToolClass::none, 0, 1.0f, 6, 7.2f, 0);
  add_native("minecraft:mutton", ITEM_FLAG_FOOD, 64, 0, ToolClass::none, 0, 1.0f,
             2, 1.2f, 0);
  add_native("minecraft:cooked_mutton", ITEM_FLAG_FOOD, 64, 0, ToolClass::none,
             0, 1.0f, 6, 9.6f, 0);
  add_native("minecraft:flint", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:string", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:feather", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:leather", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:bone", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:rotten_flesh", ITEM_FLAG_FOOD, 64, 0, ToolClass::none,
             0, 1.0f, 2, 0.4f, 0);
  add_native("minecraft:gunpowder", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0,
             1.0f, 0, 0.0f, 0);
  add_native("minecraft:arrow", ITEM_FLAG_NONE, 64, 0, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);
  add_native("minecraft:bow", ITEM_FLAG_NONE, 1, 384, ToolClass::none, 0, 1.0f,
             0, 0.0f, 0);

  struct ToolSpec {
    const char* name;
    int tier;
    int durability;
    ToolClass tool;
    float damage;
  };
  static constexpr std::array<ToolSpec, 20> kTools{{
      {"minecraft:wooden_pickaxe", 1, 59, ToolClass::pickaxe, 2.0f},
      {"minecraft:stone_pickaxe", 2, 131, ToolClass::pickaxe, 3.0f},
      {"minecraft:iron_pickaxe", 3, 250, ToolClass::pickaxe, 4.0f},
      {"minecraft:diamond_pickaxe", 4, 1561, ToolClass::pickaxe, 5.0f},
      {"minecraft:wooden_axe", 1, 59, ToolClass::axe, 7.0f},
      {"minecraft:stone_axe", 2, 131, ToolClass::axe, 9.0f},
      {"minecraft:iron_axe", 3, 250, ToolClass::axe, 9.0f},
      {"minecraft:diamond_axe", 4, 1561, ToolClass::axe, 9.0f},
      {"minecraft:wooden_shovel", 1, 59, ToolClass::shovel, 2.5f},
      {"minecraft:stone_shovel", 2, 131, ToolClass::shovel, 3.5f},
      {"minecraft:iron_shovel", 3, 250, ToolClass::shovel, 4.5f},
      {"minecraft:diamond_shovel", 4, 1561, ToolClass::shovel, 5.5f},
      {"minecraft:wooden_sword", 1, 59, ToolClass::sword, 4.0f},
      {"minecraft:stone_sword", 2, 131, ToolClass::sword, 5.0f},
      {"minecraft:iron_sword", 3, 250, ToolClass::sword, 6.0f},
      {"minecraft:diamond_sword", 4, 1561, ToolClass::sword, 7.0f},
      {"minecraft:wooden_hoe", 1, 59, ToolClass::hoe, 1.0f},
      {"minecraft:stone_hoe", 2, 131, ToolClass::hoe, 1.0f},
      {"minecraft:iron_hoe", 3, 250, ToolClass::hoe, 1.0f},
      {"minecraft:diamond_hoe", 4, 1561, ToolClass::hoe, 1.0f},
  }};
  for (const ToolSpec& spec : kTools) {
    const int fuel = spec.tier == 1 ? 200 : 0;
    add_native(spec.name, ITEM_FLAG_TOOL | (fuel ? ITEM_FLAG_FUEL : 0), 1,
               spec.durability, spec.tool, spec.tier, spec.damage, 0, 0.0f, fuel);
  }

  if (defs_.size() < 32) {
    error = "item table has too few items";
    return false;
  }
  return true;
}

const ItemDef& ItemTable::def(uint32_t item_id) const {
  static const ItemDef kEmpty{};
  const auto found = defs_.find(item_id);
  return found == defs_.end() ? kEmpty : found->second;
}

uint32_t ItemTable::find(std::string_view name) const {
  const auto found = by_name_.find(std::string(name));
  return found == by_name_.end() ? 0u : found->second;
}

uint32_t ItemTable::block_item(uint32_t block_id) const {
  const auto found = block_to_item_.find(block_id);
  return found == block_to_item_.end() ? 0u : found->second;
}

uint32_t ItemTable::item_block(uint32_t item_id) const {
  const auto found = item_to_block_.find(item_id);
  return found == item_to_block_.end() ? 0u : found->second;
}

std::string ItemTable::display_name(uint32_t item_id) const {
  return def(item_id).display;
}

std::string ItemTable::item_name(uint32_t item_id) const {
  return def(item_id).name;
}

BlockRules block_rules(std::string_view block_name) {
  BlockRules rules;
  const std::string_view bare = without_namespace(block_name);
  const std::string_view name = bare;

  if (name == "bedrock" || name == "barrier" || name == "command_block" ||
      name == "chain_command_block" || name == "repeating_command_block" ||
      name == "structure_block" || name == "jigsaw" || name == "end_portal" ||
      name == "end_portal_frame" || name == "nether_portal" ||
      name == "moving_piston" || name == "piston_head" || name == "light" ||
      name == "structure_void" || name == "air" || name == "water" ||
      name == "lava" || name == "fire" || name == "soul_fire" ||
      name == "bubble_column") {
    rules.hardness = 0.0f;
    rules.unbreakable = true;
    return rules;
  }
  if (name == "obsidian" || name == "crying_obsidian" || name == "respawn_anchor") {
    rules.hardness = 50.0f;
    rules.tool = ToolClass::pickaxe;
    rules.required_tier = 4;
    rules.needs_tool = true;
    return rules;
  }
  if (name == "ancient_debris") {
    rules.hardness = 30.0f;
    rules.tool = ToolClass::pickaxe;
    rules.required_tier = 3;
    rules.needs_tool = true;
    return rules;
  }
  if (contains(name, "_ore") || name == "ore") {
    const bool deep = contains(name, "deepslate");
    rules.hardness = deep ? 4.5f : 3.0f;
    rules.tool = ToolClass::pickaxe;
    rules.needs_tool = true;
    if (contains(name, "diamond") || contains(name, "emerald") ||
        contains(name, "gold") || contains(name, "redstone")) {
      rules.required_tier = 3;
    } else if (contains(name, "iron") || contains(name, "copper") ||
               contains(name, "lapis")) {
      rules.required_tier = 2;
    } else {
      rules.required_tier = 1;
    }
    return rules;
  }
  if (ends_with(name, "_block") &&
      (contains(name, "iron") || contains(name, "gold") ||
       contains(name, "diamond") || contains(name, "emerald") ||
       contains(name, "lapis") || contains(name, "copper") ||
       contains(name, "redstone") || contains(name, "coal"))) {
    rules.hardness = 5.0f;
    rules.tool = ToolClass::pickaxe;
    rules.required_tier = 1;
    rules.needs_tool = true;
    if (contains(name, "diamond") || contains(name, "emerald")) {
      rules.required_tier = 3;
    } else if (contains(name, "iron") || contains(name, "gold") ||
               contains(name, "lapis") || contains(name, "copper")) {
      rules.required_tier = 2;
    }
    return rules;
  }
  if (name == "furnace" || name == "blast_furnace" || name == "smoker" ||
      name == "stonecutter") {
    rules.hardness = 3.5f;
    rules.tool = ToolClass::pickaxe;
    rules.needs_tool = true;
    return rules;
  }
  if (name == "deepslate" || name == "cobbled_deepslate" ||
      name == "polished_deepslate" || ends_with(name, "_deepslate") ||
      contains(name, "deepslate_")) {
    rules.hardness = 3.0f;
    rules.tool = ToolClass::pickaxe;
    rules.needs_tool = true;
    return rules;
  }
  static constexpr std::array<std::string_view, 26> kStone{
      "stone",          "cobblestone",      "mossy_cobblestone",
      "smooth_stone",   "stone_bricks",     "mossy_stone_bricks",
      "cracked_stone_bricks", "granite",     "diorite",
      "andesite",       "polished_granite", "polished_diorite",
      "polished_andesite", "tuff",           "calcite",
      "basalt",         "smooth_basalt",    "blackstone",
      "polished_blackstone", "dripstone_block", "pointed_dripstone",
      "amethyst_block", "bricks",           "nether_bricks",
      "quartz_block",   "end_stone"};
  if (std::find(kStone.begin(), kStone.end(), name) != kStone.end() ||
      ends_with(name, "_stone") || ends_with(name, "_bricks") ||
      ends_with(name, "_terracotta") || name == "terracotta") {
    rules.hardness = 1.5f;
    rules.tool = ToolClass::pickaxe;
    rules.needs_tool = true;
    return rules;
  }
  static constexpr std::array<std::string_view, 15> kSoft{
      "dirt",        "coarse_dirt", "rooted_dirt", "grass_block",
      "podzol",      "mycelium",    "farmland",    "sand",
      "red_sand",    "gravel",      "clay",        "mud",
      "snow_block",  "soul_sand",   "soul_soil"};
  if (std::find(kSoft.begin(), kSoft.end(), name) != kSoft.end()) {
    rules.hardness = 0.5f;
    rules.tool = ToolClass::shovel;
    return rules;
  }
  if (ends_with(name, "_log") || ends_with(name, "_wood") ||
      ends_with(name, "_planks")) {
    rules.hardness = 2.0f;
    rules.tool = ToolClass::axe;
    return rules;
  }
  if (ends_with(name, "_fence") || ends_with(name, "_fence_gate") ||
      ends_with(name, "_door") || ends_with(name, "_trapdoor") ||
      ends_with(name, "_stairs") || ends_with(name, "_slab")) {
    rules.hardness = 2.5f;
    rules.tool = ToolClass::axe;
    return rules;
  }
  if (name == "crafting_table" || name == "chest" || name == "barrel" ||
      name == "bookshelf" || name == "lectern" || name == "loom" ||
      name == "composter" || name == "cartography_table" ||
      name == "fletching_table" || name == "smithing_table" ||
      name == "ladder" || name == "note_block" || name == "jukebox") {
    rules.hardness = 2.5f;
    rules.tool = ToolClass::axe;
    return rules;
  }
  if (ends_with(name, "_leaves") || name == "leaves") {
    rules.hardness = 0.2f;
    rules.tool = ToolClass::hoe;
    return rules;
  }
  if (name == "glass" || name == "glass_pane" || ends_with(name, "_glass") ||
      ends_with(name, "_glass_pane")) {
    rules.hardness = 0.3f;
    return rules;
  }
  if (ends_with(name, "_wool") || name == "wool") {
    rules.hardness = 0.8f;
    return rules;
  }
  if (name == "torch" || name == "wall_torch" || name == "soul_torch" ||
      name == "soul_wall_torch" || name == "short_grass" ||
      name == "tall_grass" || name == "fern" || name == "large_fern" ||
      ends_with(name, "_sapling") || ends_with(name, "_flower") ||
      name == "dandelion" || name == "poppy" || name == "wheat" ||
      name == "carrots" || name == "potatoes" || name == "beetroots" ||
      name == "snow" || name == "vine") {
    rules.hardness = 0.0f;
    return rules;
  }
  return rules;
}

int stack_tool_tier(const ItemTable& items, const ItemStack& held, ToolClass& out_class) {
  out_class = ToolClass::none;
  if (held.empty()) return 0;
  const ItemDef& def = items.def(held.item_id);
  if (!def.is_tool()) return 0;
  out_class = def.tool;
  return def.tool_tier;
}

bool can_harvest(const BlockRules& rules, const ItemStack& held,
                 const ItemTable& items) {
  if (!rules.needs_tool) return true;
  if (held.empty()) return false;
  const ItemDef& def = items.def(held.item_id);
  if (!def.is_tool() || def.tool != rules.tool) return false;
  return def.tool_tier >= rules.required_tier;
}

int break_ticks(const BlockRules& rules, const ItemStack& held,
                const ItemTable& items, bool can_harvest_now) {
  if (rules.unbreakable) return -1;
  if (rules.hardness <= 0.0f) return 1;
  float speed = 1.0f;
  if (!held.empty()) {
    const ItemDef& def = items.def(held.item_id);
    if (def.is_tool()) {
      if (def.tool == rules.tool) {
        speed = tier_speed(def.tool_tier);
      } else if (def.tool == ToolClass::sword) {
        speed = 1.5f;
      }
    }
  }
  const float seconds = rules.hardness * (can_harvest_now ? 1.5f : 5.0f) / speed;
  const int ticks = static_cast<int>(std::ceil(seconds * 20.0f));
  return std::max(1, ticks);
}

uint64_t next_random(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ull;
  uint64_t value = state;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

namespace {

ItemDrop drop_item(const ItemTable& items, std::string_view name, int min_count,
                   int max_count) {
  ItemDrop drop;
  drop.item_id = items.find(name);
  drop.min_count = min_count;
  drop.max_count = max_count;
  return drop;
}

std::string replace_suffix(std::string_view name, std::string_view from,
                           std::string_view to) {
  std::string text(name);
  if (ends_with(text, from)) {
    text.replace(text.size() - from.size(), from.size(), to);
  }
  return text;
}

} // namespace

std::vector<ItemDrop> block_drops(const ItemTable& items,
                                  std::string_view block_name,
                                  bool can_harvest_now, uint64_t& rng) {
  if (!can_harvest_now) return {};
  const std::string_view bare = without_namespace(block_name);
  auto add = [&](std::vector<ItemDrop>& out, ItemDrop drop, float chance) {
    if (drop.item_id == 0) return;
    const bool hit = chance >= 1.0f || (next_random(rng) % 10000u) <
                                          static_cast<uint64_t>(chance * 10000.0f);
    if (!hit) return;
    const uint64_t span = static_cast<uint64_t>(
        std::max(0, drop.max_count - drop.min_count));
    drop.min_count += span == 0 ? 0 : static_cast<int>(next_random(rng) % (span + 1));
    out.push_back(drop);
  };

  std::vector<ItemDrop> out;
  if (bare == "stone") {
    add(out, drop_item(items, "minecraft:cobblestone", 1, 1), 1.0f);
    return out;
  }
  if (bare == "deepslate") {
    add(out, drop_item(items, "minecraft:cobbled_deepslate", 1, 1), 1.0f);
    return out;
  }
  if (bare == "grass_block" || bare == "mycelium" || bare == "podzol" ||
      bare == "farmland" || bare == "rooted_dirt") {
    add(out, drop_item(items, "minecraft:dirt", 1, 1), 1.0f);
    return out;
  }
  if (ends_with(bare, "_leaves")) {
    add(out, drop_item(items, replace_suffix(bare, "_leaves", "_sapling"), 1, 1),
        0.05f);
    add(out, drop_item(items, "minecraft:stick", 1, 2), 0.02f);
    return out;
  }
  if (bare == "gravel") {
    if ((next_random(rng) % 10u) == 0u) {
      add(out, drop_item(items, "minecraft:flint", 1, 1), 1.0f);
    } else {
      add(out, drop_item(items, "minecraft:gravel", 1, 1), 1.0f);
    }
    return out;
  }
  if (bare == "short_grass" || bare == "tall_grass" || bare == "fern" ||
      bare == "large_fern") {
    add(out, drop_item(items, "minecraft:wheat_seeds", 1, 1), 0.125f);
    return out;
  }
  if (bare == "wheat") {
    add(out, drop_item(items, "minecraft:wheat", 1, 1), 1.0f);
    add(out, drop_item(items, "minecraft:wheat_seeds", 1, 3), 1.0f);
    return out;
  }
  if (bare == "glass" || bare == "glass_pane" || ends_with(bare, "_glass") ||
      ends_with(bare, "_glass_pane")) {
    return out; /* Silk touch is out of M3 scope (M3_PLAN 1.3). */
  }
  if (bare == "wall_torch" || bare == "soul_wall_torch") {
    add(out, drop_item(items, bare == "wall_torch" ? "minecraft:torch"
                                                    : "minecraft:soul_torch",
                       1, 1),
        1.0f);
    return out;
  }

  struct OreDrop {
    const char* ore;
    const char* drop;
    int min_count;
    int max_count;
  };
  static constexpr std::array<OreDrop, 14> kOres{{
      {"coal_ore", "minecraft:coal", 1, 1},
      {"deepslate_coal_ore", "minecraft:coal", 1, 1},
      {"iron_ore", "minecraft:raw_iron", 1, 1},
      {"deepslate_iron_ore", "minecraft:raw_iron", 1, 1},
      {"copper_ore", "minecraft:raw_copper", 2, 5},
      {"deepslate_copper_ore", "minecraft:raw_copper", 2, 5},
      {"gold_ore", "minecraft:raw_gold", 1, 1},
      {"deepslate_gold_ore", "minecraft:raw_gold", 1, 1},
      {"redstone_ore", "minecraft:redstone", 4, 5},
      {"deepslate_redstone_ore", "minecraft:redstone", 4, 5},
      {"lapis_ore", "minecraft:lapis_lazuli", 4, 9},
      {"deepslate_lapis_ore", "minecraft:lapis_lazuli", 4, 9},
      {"diamond_ore", "minecraft:diamond", 1, 1},
      {"deepslate_diamond_ore", "minecraft:diamond", 1, 1},
  }};
  for (const OreDrop& ore : kOres) {
    if (bare == ore.ore) {
      add(out, drop_item(items, ore.drop, ore.min_count, ore.max_count), 1.0f);
      return out;
    }
  }
  if (bare == "emerald_ore" || bare == "deepslate_emerald_ore") {
    add(out, drop_item(items, "minecraft:emerald", 1, 1), 1.0f);
    return out;
  }
  if (bare == "nether_quartz_ore") {
    add(out, drop_item(items, "minecraft:quartz", 1, 1), 1.0f);
    return out;
  }

  const uint32_t item_id = items.find(block_name);
  if (item_id != 0) {
    add(out, ItemDrop{item_id, 1, 1}, 1.0f);
  }
  return out;
}

} // namespace reim::game
