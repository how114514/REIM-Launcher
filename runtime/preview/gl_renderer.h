/*
 * Chunk renderer for REIMPreview (M1-F3..F5).
 *
 * Consumes the immutable ReimRenderFrame: uploads incremental section updates,
 * drops removed sections, frustum-culls and draws opaque terrain first and
 * translucent sections back-to-front. It never reads or modifies world state;
 * everything drawn comes from the Runtime snapshot.
 */
#pragma once

#include "gl_loader.h"
#include "render_math.h"
#include "reim_render.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace reim::preview {

struct RenderOptions {
  bool third_person = false;
  bool debug_player_aabb = false;
};

struct RenderStats {
  uint32_t draw_calls = 0;
  uint32_t triangles = 0;
  uint32_t culled_sections = 0;
  uint32_t meshes = 0;
  uint32_t atlas_width = 0;
  uint32_t atlas_height = 0;
};

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  bool initialize(const std::filesystem::path& generated_dir, std::string& error);
  /* ABI v2 asset path: atlas PNG bytes delivered by get_asset. `source` is a
   * diagnostic label used in the log line. */
  bool initialize_from_png(const uint8_t* data, size_t size,
                           const std::string& source, std::string& error);
  void shutdown();

  /* Applies the frame's update/remove lists. Call once per fetch_frame. */
  void apply_frame(const ReimRenderFrame& frame);
  void render(const ReimRenderFrame& frame, const RenderOptions& options, int width,
              int height);

  const RenderStats& stats() const { return stats_; }

 private:
  struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    uint32_t index_count = 0;
    uint32_t revision = 0;
    uint32_t material = 0;
    ReimBounds bounds{};
  };

  using Key = std::tuple<int32_t, int32_t, int32_t, uint32_t>;

  static Key make_key(int32_t chunk_x, int32_t chunk_z, int32_t section_y,
                      uint32_t material);
  static ReimVec3 bounds_center(const ReimBounds& bounds);

  void destroy_mesh(GpuMesh& mesh);
  void upload_mesh(const ReimSectionMeshUpdate& update);
  void remove_section(int32_t chunk_x, int32_t chunk_z, int32_t section_y);
  void draw_meshes(const std::vector<GpuMesh*>& meshes);
  void draw_player_box(const ReimRenderFrame& frame, const Mat4& view_proj);
  bool create_chunk_program(std::string& error);
  bool create_line_program(std::string& error);

  GLuint atlas_texture_ = 0;
  GLuint chunk_program_ = 0;
  GLuint line_program_ = 0;
  GLuint line_vao_ = 0;
  GLuint line_vbo_ = 0;

  GLint chunk_view_proj_ = -1;
  GLint chunk_atlas_ = -1;
  GLint chunk_fog_color_ = -1;
  GLint chunk_fog_start_ = -1;
  GLint chunk_fog_end_ = -1;
  GLint chunk_camera_ = -1;
  GLint chunk_transparent_ = -1;
  GLint line_view_proj_ = -1;
  GLint line_color_ = -1;

  std::map<Key, GpuMesh> meshes_;
  RenderStats stats_;
};

} // namespace reim::preview
