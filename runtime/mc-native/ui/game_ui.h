#pragma once

#include "game/gameplay.h"
#include "ui/ui_spike.h"

namespace reim::ui {

/*
 * Runtime HUD/menu display-list builder (M3-H1..H7, non-UE part). It reads an
 * immutable GameView and produces Contract draw items only; it owns no
 * inventory/recipe/world logic and no Host widget tree.
 */
class GameUi {
 public:
  void build(const game::GameView& view, const game::ItemTable& items,
             const data::BlockTable& blocks, UiSpike::DrawList& out) const;
};

} // namespace reim::ui
