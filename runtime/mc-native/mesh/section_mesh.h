#pragma once

#include "data/block_table.h"
#include "reim_render.h"
#include "world/chunk.h"

#include <cstdint>
#include <vector>

namespace reim::mesh {

/* Block id lookup spanning the 3x3 chunk neighbourhood. A chunk that is not
 * loaded answers 0 (air), which exposes a boundary wall that is remeshed once
 * its neighbour arrives. */
using BlockIdQuery = uint32_t (*)(const void* context, int x, int y, int z);

struct SectionMeshOutput {
  int section_y = 0;
  std::vector<ReimVertex> opaque_vertices;
  std::vector<uint32_t> opaque_indices;
  std::vector<ReimVertex> transparent_vertices;
  std::vector<uint32_t> transparent_indices;

  bool empty() const {
    return opaque_indices.empty() && transparent_indices.empty();
  }
};

/*
 * Meshes every stored section of one chunk (M1-E2/E3).
 *
 * - full-cube blocks only; blocks whose render_class is skip are omitted
 * - faces against an opaque full cube are culled, as are water/glass faces
 *   against the same block
 * - positions are world-space; uv/sprite/tint come from generated blocks.json
 *   and textures.json; light packs sky in the high byte and block light in the
 *   low byte of ReimVertex::light
 * - `section_block_ids` is parallel to chunk.sections: palette index to
 *   BlockTable id, so the per-block loop does no string lookups
 */
void mesh_chunk(int chunk_x, int chunk_z, const world::Chunk& chunk,
                const std::vector<std::vector<uint32_t>>& section_block_ids,
                const data::BlockTable& blocks, BlockIdQuery query,
                const void* context, std::vector<SectionMeshOutput>& out);

} // namespace reim::mesh
