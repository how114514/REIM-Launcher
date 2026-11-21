#pragma once

#include "game/item.h"

#include <array>
#include <cstdint>
#include <string>

namespace reim::game {

constexpr int kHotbarSlots = 9;
constexpr int kMainSlots = 27;
constexpr int kPlayerSlots = kHotbarSlots + kMainSlots;
constexpr int kCraftGridSlots = 9;

/* Player inventory slot numbering (M3-D4): 0..8 hotbar, 9..35 main. The
 * Contract inventory snapshot exposes screen-relative slots; this internal
 * numbering is stable across screens. */
class Inventory {
 public:
  std::array<ItemStack, kPlayerSlots> slots{};
  int selected = 0;

  ItemStack& hotbar(int index) {
    return slots[static_cast<size_t>(index < 0 ? 0 : (index > 8 ? 8 : index))];
  }
  const ItemStack& hotbar(int index) const {
    return slots[static_cast<size_t>(index < 0 ? 0 : (index > 8 ? 8 : index))];
  }
  ItemStack& held() { return hotbar(selected); }
  const ItemStack& held() const { return hotbar(selected); }

  int count(uint32_t item_id) const;
  int first_slot(uint32_t item_id) const;
  bool add(const ItemTable& items, ItemStack stack);
  /* Adds as much as fits; returns the remainder count (0 = all inserted). */
  int add_partial(const ItemTable& items, ItemStack stack);
  bool can_add(const ItemTable& items, const ItemStack& stack) const;
  void clear();
};

struct CraftingGrid {
  int size = 2; /* 2 or 3 */
  std::array<ItemStack, kCraftGridSlots> slots{};

  ItemStack& at(int row, int column) {
    return slots[static_cast<size_t>(row * size + column)];
  }
  const ItemStack& at(int row, int column) const {
    return slots[static_cast<size_t>(row * size + column)];
  }
  bool empty() const;
  void clear();
};

enum class ContainerKind : uint8_t { none = 0, furnace = 1, chest = 2 };

struct FurnaceState {
  ItemStack input;
  ItemStack fuel;
  ItemStack output;
  int burn_time = 0;  /* ticks of fuel left */
  int burn_total = 0; /* ticks the current fuel item lasts */
  int cook_time = 0;  /* ticks toward the next output */
  bool lit() const { return burn_time > 0; }
};

constexpr int kChestSlots = 27;

struct ChestState {
  std::array<ItemStack, kChestSlots> slots{};
};

struct BlockPos {
  int x = 0;
  int y = 0;
  int z = 0;
  bool operator==(const BlockPos& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
  bool operator<(const BlockPos& other) const {
    if (x != other.x) return x < other.x;
    if (y != other.y) return y < other.y;
    return z < other.z;
  }
};

struct BlockEntity {
  ContainerKind kind = ContainerKind::none;
  FurnaceState furnace;
  ChestState chest;
};

struct DroppedItem {
  uint64_t id = 0;
  ItemStack stack;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double vx = 0.0;
  double vy = 0.0;
  double vz = 0.0;
  int age = 0;
  int pickup_delay = 0;
};

} // namespace reim::game
