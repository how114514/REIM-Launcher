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
  if (neighbour.full_block && neighbour.render_class == data::RenderClass::opaque) {
    return true;
  }
  if (self.render_class == data::RenderClass::opaque) return false;
  // Transparent blocks hide only against the same block (water/water,
  // glass/glass) so a water surface stays visible next to stone.
  return neighbour.render_class == data::RenderClass::transparent &&
         neighbour.name == self.name;
}

uint32_t fallback_tint(data::TintKind kind) {
  // RGBA8, red in the low byte: vanilla's forest grass/foliage and the default
  // water colour, used only when generated data has no biome table.
  switch (kind) {
    case data::TintKind::grass:
    case data::TintKind::foliage:
      return 0xFF5AC079u; // 0x79C05A
    case data::TintKind::water:
      return 0xFFE4763Fu; // 0x3F76E4
    default:
      return 0xFFFFFFFFu;
  }
}

uint32_t resolve_tint(const data::BlockTable& blocks, const world::Section& section,
                      uint32_t baked, data::TintKind kind, int lx, int ly, int lz) {
  if (kind == data::TintKind::none) return baked;
  /* J2: 3x3 biome-cell tint blend (center + 4 samples, clamped to the
   * section). This removes the worst single-cell colour banding; cross-section
   * blending and full smooth-lighting AO are still follow-ups. */
  uint32_t sum_r = 0;
  uint32_t sum_g = 0;
  uint32_t sum_b = 0;
  uint32_t samples = 0;
  const int offsets[5][2] = {{0, 0}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}};
  for (const auto& offset : offsets) {
    const std::string biome = section.biome(lx + offset[0], ly, lz + offset[1]);
    uint32_t color = 0;
    if (!biome.empty() && blocks.biome_color(biome, kind, color)) {
      sum_r += color & 0xFFu;
      sum_g += (color >> 8) & 0xFFu;
      sum_b += (color >> 16) & 0xFFu;
      ++samples;
    }
  }
  if (samples == 0) return fallback_tint(kind);
  const uint32_t alpha = (fallback_tint(kind) >> 24) & 0xFFu;
  return (alpha << 24) | ((sum_b / samples) << 16) | ((sum_g / samples) << 8) |
         (sum_r / samples);
}

void push_quad(std::vector<ReimVertex>& vertices, std::vector<uint32_t>& indices,
               float world_x, float world_y, float world_z, const FaceGeometry& face,
               uint32_t tint, uint16_t sprite, uint16_t light) {
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  for (int corner = 0; corner < 4; ++corner) {
    ReimVertex vertex{};
    vertex.position[0] = world_x + face.corners[corner * 3 + 0];
    vertex.position[1] = world_y + face.corners[corner * 3 + 1];
    vertex.position[2] = world_z + face.corners[corner * 3 + 2];
    vertex.normal[0] = static_cast<float>(face.nx);
    vertex.normal[1] = static_cast<float>(face.ny);
    vertex.normal[2] = static_cast<float>(face.nz);
    vertex.uv[0] = face.uv[corner * 2 + 0];
    vertex.uv[1] = face.uv[corner * 2 + 1];
    vertex.sprite = sprite;
    vertex.light = light;
    vertex.tint = tint;
    vertices.push_back(vertex);
  }
  indices.push_back(base + 0);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
  indices.push_back(base + 0);
  indices.push_back(base + 2);
  indices.push_back(base + 3);
}

void push_geometry_quad(std::vector<ReimVertex>& vertices,
                        std::vector<uint32_t>& indices, float world_x, float world_y,
                        float world_z, const data::GeometryQuad& quad, uint32_t tint,
                        uint16_t light) {
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  for (int corner = 0; corner < 4; ++corner) {
    ReimVertex vertex{};
    vertex.position[0] = world_x + quad.corners[corner][0] / 16.0f;
    vertex.position[1] = world_y + quad.corners[corner][1] / 16.0f;
    vertex.position[2] = world_z + quad.corners[corner][2] / 16.0f;
    for (int c = 0; c < 3; ++c) vertex.normal[c] = quad.normal[c];
    vertex.uv[0] = quad.uv[corner][0];
    vertex.uv[1] = quad.uv[corner][1];
    vertex.sprite = quad.sprite;
    vertex.light = light;
    vertex.tint = tint;
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

          // Non-cube models (snow layers, crosses, thin boxes): explicit quads
          // from generated geometry instead of the six cube faces.
          if (!block.geometry.empty()) {
            for (const data::GeometryQuad& quad : block.geometry) {
              if (quad.cull_face >= 0 && quad.cull_face < data::kFaceCount) {
                const FaceGeometry& face = kFaces[quad.cull_face];
                const uint32_t neighbour_id =
                    query(context, world_x + face.nx, world_y + face.ny,
                          world_z + face.nz);
                // Identical neighbouring tiles (two snow layers of the same
                // height) share this side plane; keeping both z-fights into a
                // dark grid, so cull it.
                if (neighbour_id == block_id ||
                    cull_face(block, blocks.def(neighbour_id))) {
                  continue;
                }
              }
              const uint32_t tint = resolve_tint(
                  blocks, section, quad.tint, quad.tint_kind, lx, ly, lz);
              push_geometry_quad(vertices, indices, static_cast<float>(world_x),
                                 static_cast<float>(world_y),
                                 static_cast<float>(world_z), quad, tint, light);
            }
            continue;
          }

          for (int face = 0; face < data::kFaceCount; ++face) {
            const FaceGeometry& geometry = kFaces[face];
            const uint32_t neighbour_id =
                query(context, world_x + geometry.nx, world_y + geometry.ny,
                      world_z + geometry.nz);
            if (cull_face(block, blocks.def(neighbour_id))) continue;
            const data::FaceTexture& texture = block.faces[face];
            const uint32_t tint =
                resolve_tint(blocks, section, texture.tint, texture.tint_kind, lx,
                             ly, lz);
            push_quad(vertices, indices, static_cast<float>(world_x),
                      static_cast<float>(world_y), static_cast<float>(world_z),
                      geometry, tint, texture.sprite, light);
            // Apply the sprite's atlas rectangle to the quad's 0..1 UVs.
            const size_t first = vertices.size() - 4;
            for (size_t v = 0; v < 4; ++v) {
              const float u = vertices[first + v].uv[0];
              const float w = vertices[first + v].uv[1];
              vertices[first + v].uv[0] = texture.u0 + u * (texture.u1 - texture.u0);
              vertices[first + v].uv[1] = texture.v0 + w * (texture.v1 - texture.v0);
            }
          }
        }
      }
    }
    out.push_back(std::move(mesh));
  }
}

} // namespace reim::mesh
