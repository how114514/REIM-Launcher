#include "mesh/section_mesh.h"

#include <array>

namespace reim::mesh {
namespace {

struct FaceGeometry {
  int nx, ny, nz;
  float corners[12];  // 4 corners, xyz each
  float uv[8];        // 4 corners, uv each
};

/* Winding is counter-clockwise when viewed from outside the block, matching
 * OpenGL's default front-face rule. Index order matches blocks.json. */
static const FaceGeometry kFaces[data::kFaceCount] = {
    // down (-Y)
    {0, -1, 0,
     {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
     {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}},
    // up (+Y)
    {0, 1, 0,
     {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f},
     {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
    // north (-Z)
    {0, 0, -1,
     {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f},
     {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
    // south (+Z)
    {0, 0, 1,
     {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
     {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f}},
    // west (-X)
    {-1, 0, 0,
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
     {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f}},
    // east (+X)
    {1, 0, 0,
     {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
     {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f}},
};

bool cull_face(const data::BlockDef& self, const data::BlockDef& neighbour) {
  if (neighbour.render_class == data::RenderClass::opaque) return true;
  if (self.render_class == data::RenderClass::opaque) return false;
  // Transparent blocks hide only against the same block (water/water,
  // glass/glass) so a water surface stays visible next to stone.
  return neighbour.render_class == data::RenderClass::transparent &&
         neighbour.name == self.name;
}

void push_quad(std::vector<ReimVertex>& vertices, std::vector<uint32_t>& indices,
               float world_x, float world_y, float world_z, const FaceGeometry& face,
               const data::FaceTexture& texture, uint16_t light) {
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  for (int corner = 0; corner < 4; ++corner) {
    ReimVertex vertex{};
    vertex.position[0] = world_x + face.corners[corner * 3 + 0];
    vertex.position[1] = world_y + face.corners[corner * 3 + 1];
    vertex.position[2] = world_z + face.corners[corner * 3 + 2];
    vertex.normal[0] = static_cast<float>(face.nx);
    vertex.normal[1] = static_cast<float>(face.ny);
    vertex.normal[2] = static_cast<float>(face.nz);
    vertex.uv[0] = texture.u0 + face.uv[corner * 2 + 0] * (texture.u1 - texture.u0);
    vertex.uv[1] = texture.v0 + face.uv[corner * 2 + 1] * (texture.v1 - texture.v0);
    vertex.light = light;
    vertex.sprite = texture.sprite;
    vertex.tint = texture.tint;
    vertices.push_back(vertex);
  }
  indices.push_back(base + 0);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
  indices.push_back(base + 0);
  indices.push_back(base + 2);
  indices.push_back(base + 3);
}

} // namespace

void mesh_chunk(int chunk_x, int chunk_z, const world::Chunk& chunk,
                const std::vector<std::vector<uint32_t>>& section_block_ids,
                const data::BlockTable& blocks, BlockIdQuery query,
                const void* context, std::vector<SectionMeshOutput>& out) {
  const int base_x = chunk_x * 16;
  const int base_z = chunk_z * 16;

  for (size_t section_index = 0; section_index < chunk.sections.size();
       ++section_index) {
    const world::Section& section = chunk.sections[section_index];
    if (!section.non_empty()) continue;
    if (section_index >= section_block_ids.size()) continue;
    const std::vector<uint32_t>& ids = section_block_ids[section_index];
    if (ids.empty()) continue;

    SectionMeshOutput mesh;
    mesh.section_y = section.y;
    const int base_y = section.y * 16;

    for (int ly = 0; ly < 16; ++ly) {
      for (int lz = 0; lz < 16; ++lz) {
        for (int lx = 0; lx < 16; ++lx) {
          const size_t index =
              (static_cast<size_t>(ly) << 8) | (static_cast<size_t>(lz) << 4) |
              static_cast<size_t>(lx);
          const uint32_t palette_index =
              section.indices.empty() ? 0u : section.indices[index];
          if (palette_index >= ids.size()) continue;
          const uint32_t block_id = ids[palette_index];
          if (block_id == 0) continue;
          const data::BlockDef& block = blocks.def(block_id);
          if (block.render_class == data::RenderClass::skip) continue;

          const int world_x = base_x + lx;
          const int world_y = base_y + ly;
          const int world_z = base_z + lz;
          const uint16_t light = static_cast<uint16_t>(
              (static_cast<uint16_t>(section.sky_light_at(lx, ly, lz)) << 8) |
              static_cast<uint16_t>(section.block_light_at(lx, ly, lz)));

          std::vector<ReimVertex>& vertices =
              block.render_class == data::RenderClass::transparent
                  ? mesh.transparent_vertices
                  : mesh.opaque_vertices;
          std::vector<uint32_t>& indices =
              block.render_class == data::RenderClass::transparent
                  ? mesh.transparent_indices
                  : mesh.opaque_indices;

          for (int face = 0; face < data::kFaceCount; ++face) {
            const FaceGeometry& geometry = kFaces[face];
            const uint32_t neighbour_id =
                query(context, world_x + geometry.nx, world_y + geometry.ny,
                      world_z + geometry.nz);
            if (cull_face(block, blocks.def(neighbour_id))) continue;
            push_quad(vertices, indices, static_cast<float>(world_x),
                      static_cast<float>(world_y), static_cast<float>(world_z),
                      geometry, block.faces[face], light);
          }
        }
      }
    }
    out.push_back(std::move(mesh));
  }
}

} // namespace reim::mesh
