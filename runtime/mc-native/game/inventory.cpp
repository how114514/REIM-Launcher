#include "game/inventory.h"

#include <algorithm>

namespace reim::game {
namespace {

int stack_limit(const ItemTable& items, uint32_t item_id) {
  const int max_stack = static_cast<int>(items.def(item_id).max_stack);
  return std::max(1, max_stack);
}

} // namespace

int Inventory::count(uint32_t item_id) const {
  int total = 0;
  for (const ItemStack& stack : slots) {
    if (!stack.empty() && stack.item_id == item_id) total += stack.count;
  }
  return total;
}

int Inventory::first_slot(uint32_t item_id) const {
  for (size_t i = 0; i < slots.size(); ++i) {
    if (!slots[i].empty() && slots[i].item_id == item_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int Inventory::add_partial(const ItemTable& items, ItemStack stack) {
  if (stack.empty()) return 0;
  const int limit = stack_limit(items, stack.item_id);
  for (ItemStack& slot : slots) {
    if (stack.count <= 0) break;
    if (slot.empty() || slot.item_id != stack.item_id) continue;
    if (slot.count >= limit) continue;
    if (limit <= 1 && slot.damage != stack.damage) continue;
    const int room = limit - slot.count;
    const int moved = std::min(room, stack.count);
    slot.count += moved;
    stack.count -= moved;
  }
  for (ItemStack& slot : slots) {
    if (stack.count <= 0) break;
    if (!slot.empty()) continue;
    slot = stack;
    slot.count = std::min(stack.count, limit);
    stack.count -= slot.count;
  }
  return stack.count;
}

bool Inventory::add(const ItemTable& items, ItemStack stack) {
  return add_partial(items, std::move(stack)) == 0;
}

bool Inventory::can_add(const ItemTable& items, const ItemStack& stack) const {
  if (stack.empty()) return true;
  const int limit = stack_limit(items, stack.item_id);
  int room = 0;
  for (const ItemStack& slot : slots) {
    if (slot.empty()) {
      room += limit;
    } else if (slot.item_id == stack.item_id && slot.count < limit &&
               (limit > 1 ? true : slot.damage == stack.damage)) {
      room += limit - slot.count;
    }
    if (room >= stack.count) return true;
  }
  return room >= stack.count;
}

void Inventory::clear() {
  for (ItemStack& stack : slots) stack.clear();
  selected = 0;
}

bool CraftingGrid::empty() const {
  for (int row = 0; row < size; ++row) {
    for (int column = 0; column < size; ++column) {
      if (!at(row, column).empty()) return false;
    }
  }
  return true;
}

void CraftingGrid::clear() {
  for (ItemStack& stack : slots) stack.clear();
  size = 2;
}

} // namespace reim::game
