#pragma once

#include "nbt/nbt.h"
#include "world/chunk.h"

#include <string>
#include <vector>

namespace reim::world {

/*
 * A7 spike: edits one block inside an already-parsed chunk NBT document while
 * preserving every other tag (including unknown fields). The section must
 * already exist; creating sections/heightmaps/light is part of M3-C/B.
 *
 * Coordinates are world coordinates of the chunk document's column.
 */
bool set_block(nbt::Document& document, int world_x, int y, int world_z,
               const BlockState& state, std::string& error);

} // namespace reim::world
