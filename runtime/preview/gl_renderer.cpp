#include "gl_renderer.h"

#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "stb_image.h"

namespace reim::preview {
namespace {

const char* const kChunkVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uint a_packed_light;
layout(location = 4) in vec4 a_tint;

uniform mat4 u_view_proj;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec2 v_light;
out vec4 v_tint;

void main() {
  v_world = a_position;
  v_normal = a_normal;
  v_uv = a_uv;
  // low byte: block light, high byte: sky light (ReimVertex::light)
  v_light = vec2(float((a_packed_light >> 8) & 0xFFu) / 15.0,
                 float(a_packed_light & 0xFFu) / 15.0);
  v_tint = a_tint;
  gl_Position = u_view_proj * vec4(a_position, 1.0);
}
)GLSL";

const char* const kChunkFragmentShader = R"GLSL(
#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_light;
in vec4 v_tint;

uniform sampler2D u_atlas;
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform vec3 u_camera;
uniform int u_transparent;

out vec4 frag_color;

void main() {
  vec4 texel = texture(u_atlas, v_uv);
  vec3 normal = normalize(v_normal);
  float diffuse = max(dot(normal, normalize(vec3(0.40, 1.0, 0.30))), 0.0);

  float light_level = max(v_light.x, v_light.y);
  // The M1 test save stores sections without light layers (light == 0); fall
  // back to a fully lit directional surface instead of rendering black.
  if (v_light.x <= 0.0 && v_light.y <= 0.0) light_level = 1.0;
  float brightness = (0.45 + 0.55 * light_level) * (0.62 + 0.38 * diffuse);

  vec3 color = texel.rgb * v_tint.rgb * brightness;
  float alpha = texel.a * v_tint.a;
  if (u_transparent != 0) alpha *= 0.72;

  float distance = length(v_world - u_camera);
  float fog = clamp((distance - u_fog_start) / max(u_fog_end - u_fog_start, 0.001),
                    0.0, 1.0);
  color = mix(color, u_fog_color, fog * 0.85);
  frag_color = vec4(color, alpha);
}
)GLSL";

const char* const kLineVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_view_proj;
void main() { gl_Position = u_view_proj * vec4(a_position, 1.0); }
)GLSL";

const char* const kLineFragmentShader = R"GLSL(
#version 330 core
uniform vec4 u_color;
out vec4 frag_color;
void main() { frag_color = u_color; }
)GLSL";

GLuint compile_shader(GLenum type, const char* source, std::string& error) {
  const GLuint shader = glCreateShader(type);
  if (shader == 0) {
    error = "glCreateShader returned 0";
    return 0;
  }
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint status = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error = std::string("shader compile failed: ") + log.data();
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint link_program(const char* vertex_source, const char* fragment_source,
                    std::string& error) {
  const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, error);
  if (vertex == 0) return 0;
  const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source, error);
  if (fragment == 0) {
    glDeleteShader(vertex);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(length > 1 ? length : 1), '\0');
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error = std::string("program link failed: ") + log.data();
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void color_from_rgb(uint32_t rgb, float out[3]) {
  out[0] = static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f;
  out[1] = static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f;
  out[2] = static_cast<float>(rgb & 0xFFu) / 255.0f;
}

} // namespace

Renderer::~Renderer() { shutdown(); }

Renderer::Key Renderer::make_key(int32_t chunk_x, int32_t chunk_z, int32_t section_y,
                                 uint32_t material) {
  return Key(chunk_x, chunk_z, section_y, material);
}

ReimVec3 Renderer::bounds_center(const ReimBounds& bounds) {
  return ReimVec3{(bounds.min.x + bounds.max.x) * 0.5f,
                  (bounds.min.y + bounds.max.y) * 0.5f,
                  (bounds.min.z + bounds.max.z) * 0.5f};
}

bool Renderer::initialize(const std::filesystem::path& generated_dir, std::string& error) {
  const std::filesystem::path atlas_path = generated_dir / "atlas.png";
  reim::file::Bytes bytes;
  if (!reim::file::read_all(atlas_path, bytes, error)) {
    error = "atlas: " + error;
    return false;
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                          &width, &height, &channels, 4);
  if (pixels == nullptr) {
    error = "cannot decode " + reim::file::to_utf8(atlas_path);
    return false;
  }

  glGenTextures(1, &atlas_texture_);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  stbi_image_free(pixels);

  stats_.atlas_width = static_cast<uint32_t>(width);
  stats_.atlas_height = static_cast<uint32_t>(height);

  if (!create_chunk_program(error)) return false;
  if (!create_line_program(error)) return false;

  glGenVertexArrays(1, &line_vao_);
  glGenBuffers(1, &line_vbo_);
  glBindVertexArray(line_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);

  reim::log::info("preview", "atlas " + std::to_string(width) + "x" +
                                 std::to_string(height) + " loaded from " +
                                 reim::file::to_utf8(atlas_path));
  return true;
}

bool Renderer::create_chunk_program(std::string& error) {
  chunk_program_ = link_program(kChunkVertexShader, kChunkFragmentShader, error);
  if (chunk_program_ == 0) return false;
  chunk_view_proj_ = glGetUniformLocation(chunk_program_, "u_view_proj");
  chunk_atlas_ = glGetUniformLocation(chunk_program_, "u_atlas");
  chunk_fog_color_ = glGetUniformLocation(chunk_program_, "u_fog_color");
  chunk_fog_start_ = glGetUniformLocation(chunk_program_, "u_fog_start");
  chunk_fog_end_ = glGetUniformLocation(chunk_program_, "u_fog_end");
  chunk_camera_ = glGetUniformLocation(chunk_program_, "u_camera");
  chunk_transparent_ = glGetUniformLocation(chunk_program_, "u_transparent");
  return true;
}

bool Renderer::create_line_program(std::string& error) {
  line_program_ = link_program(kLineVertexShader, kLineFragmentShader, error);
  if (line_program_ == 0) return false;
  line_view_proj_ = glGetUniformLocation(line_program_, "u_view_proj");
  line_color_ = glGetUniformLocation(line_program_, "u_color");
  return true;
}

void Renderer::destroy_mesh(GpuMesh& mesh) {
  if (mesh.ebo != 0) glDeleteBuffers(1, &mesh.ebo);
  if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
  if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
  mesh = GpuMesh{};
}

void Renderer::shutdown() {
  for (auto& entry : meshes_) destroy_mesh(entry.second);
  meshes_.clear();
  if (line_vbo_ != 0) glDeleteBuffers(1, &line_vbo_);
  if (line_vao_ != 0) glDeleteVertexArrays(1, &line_vao_);
  if (atlas_texture_ != 0) glDeleteTextures(1, &atlas_texture_);
  if (chunk_program_ != 0) glDeleteProgram(chunk_program_);
  if (line_program_ != 0) glDeleteProgram(line_program_);
  line_vbo_ = line_vao_ = atlas_texture_ = chunk_program_ = line_program_ = 0;
}

void Renderer::upload_mesh(const ReimSectionMeshUpdate& update) {
  const Key key = make_key(update.chunk_x, update.chunk_z, update.section_y,
                           update.material);
  GpuMesh& mesh = meshes_[key];
  if (mesh.vao == 0) {
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
  }
  glBindVertexArray(mesh.vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(static_cast<size_t>(update.vertex_count) *
                                       update.vertex_stride),
               update.vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(static_cast<size_t>(update.index_count) *
                                       sizeof(uint32_t)),
               update.indices, GL_STATIC_DRAW);

  const GLsizei stride = static_cast<GLsizei>(update.vertex_stride);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(12)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(24)));
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, stride,
                         reinterpret_cast<const void*>(static_cast<uintptr_t>(32)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(36)));
  glEnableVertexAttribArray(4);
  glBindVertexArray(0);

  mesh.index_count = update.index_count;
  mesh.revision = update.revision;
  mesh.material = update.material;
  mesh.bounds = update.bounds;
}

void Renderer::remove_section(int32_t chunk_x, int32_t chunk_z, int32_t section_y) {
  for (uint32_t material = 0; material < 2; ++material) {
    const auto it = meshes_.find(make_key(chunk_x, chunk_z, section_y, material));
    if (it == meshes_.end()) continue;
    destroy_mesh(it->second);
    meshes_.erase(it);
  }
}

void Renderer::apply_frame(const ReimRenderFrame& frame) {
  for (uint32_t i = 0; i < frame.remove_count; ++i) {
    const ReimSectionRemove& remove = frame.removes[i];
    remove_section(remove.chunk_x, remove.chunk_z, remove.section_y);
  }
  for (uint32_t i = 0; i < frame.update_count; ++i) {
    const ReimSectionMeshUpdate& update = frame.updates[i];
    // A zero-vertex update clears that (section, material).
    if (update.vertex_count == 0 || update.index_count == 0 || update.vertices == nullptr ||
        update.indices == nullptr) {
      const auto it = meshes_.find(
          make_key(update.chunk_x, update.chunk_z, update.section_y, update.material));
      if (it != meshes_.end()) {
        destroy_mesh(it->second);
        meshes_.erase(it);
      }
      continue;
    }
    const auto existing = meshes_.find(
        make_key(update.chunk_x, update.chunk_z, update.section_y, update.material));
    if (existing != meshes_.end() && existing->second.revision > update.revision) {
      continue;  // stale snapshot
    }
    upload_mesh(update);
  }
}

void Renderer::draw_meshes(const std::vector<GpuMesh*>& meshes) {
  for (const GpuMesh* mesh : meshes) {
    if (mesh->index_count == 0) continue;
    glBindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->index_count),
                   GL_UNSIGNED_INT, nullptr);
    ++stats_.draw_calls;
    stats_.triangles += mesh->index_count / 3;
  }
}

void Renderer::draw_player_box(const ReimRenderFrame& frame, const Mat4& view_proj) {
  const float half_width = frame.player.half_width > 0.0f ? frame.player.half_width : 0.3f;
  const float height = frame.player.height > 0.0f ? frame.player.height : 1.8f;
  const float x = frame.player.position.x;
  const float y = frame.player.position.y;
  const float z = frame.player.position.z;
  const float x0 = x - half_width;
  const float x1 = x + half_width;
  const float z0 = z - half_width;
  const float z1 = z + half_width;
  const float y0 = y;
  const float y1 = y + height;

  const std::array<std::array<float, 3>, 8> corners{{
      {{x0, y0, z0}}, {{x1, y0, z0}}, {{x1, y0, z1}}, {{x0, y0, z1}},
      {{x0, y1, z0}}, {{x1, y1, z0}}, {{x1, y1, z1}}, {{x0, y1, z1}},
  }};
  static constexpr int kEdges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  std::array<float, 12 * 2 * 3> lines{};
  size_t offset = 0;
  for (const auto& edge : kEdges) {
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
      const auto& corner = corners[edge[endpoint]];
      lines[offset++] = corner[0];
      lines[offset++] = corner[1];
      lines[offset++] = corner[2];
    }
  }

  glUseProgram(line_program_);
  glUniformMatrix4fv(line_view_proj_, 1, GL_FALSE, view_proj.m);
  glUniform4f(line_color_, 0.15f, 1.0f, 0.35f, 1.0f);
  glBindVertexArray(line_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(lines)), lines.data(),
               GL_DYNAMIC_DRAW);
  glLineWidth(2.0f);
  glDrawArrays(GL_LINES, 0, 24);
  glLineWidth(1.0f);
  glBindVertexArray(0);
}

void Renderer::render(const ReimRenderFrame& frame, const RenderOptions& options,
                      int width, int height) {
  stats_.draw_calls = 0;
  stats_.triangles = 0;
  stats_.culled_sections = 0;
  stats_.meshes = static_cast<uint32_t>(meshes_.size());

  float sky[3];
  float fog[3];
  color_from_rgb(frame.environment.sky_color, sky);
  color_from_rgb(frame.environment.fog_color, fog);
  glViewport(0, 0, width, height);
  glClearColor(sky[0], sky[1], sky[2], 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (width <= 0 || height <= 0) return;

  ReimVec3 eye{frame.camera.position.x, frame.camera.position.y,
               frame.camera.position.z};
  if (options.third_person) {
    // The Runtime casts backwards against world collision so the camera stops
    // at walls instead of clipping through terrain.
    const float backoff = std::clamp(frame.third_person_distance, 0.0f, 4.0f);
    eye = third_person_eye(eye, frame.camera.yaw, frame.camera.pitch, backoff);
  }

  const float aspect = static_cast<float>(width) / static_cast<float>(height);
  const float far_z = frame.camera.far_z > 1.0f ? frame.camera.far_z : 256.0f;
  const float near_z = frame.camera.near_z > 0.0f ? frame.camera.near_z : 0.05f;
  const Mat4 projection =
      perspective(frame.camera.fov_y_degrees > 1.0f ? frame.camera.fov_y_degrees : 70.0f,
                  aspect, near_z, far_z);
  const Mat4 view = view_from_angles(eye, frame.camera.yaw, frame.camera.pitch);
  const Mat4 view_proj = multiply(projection, view);
  const Frustum frustum = frustum_from_view_proj(view_proj);

  std::vector<GpuMesh*> opaque;
  std::vector<GpuMesh*> transparent;
  opaque.reserve(meshes_.size());
  transparent.reserve(meshes_.size());
  for (auto& entry : meshes_) {
    GpuMesh& mesh = entry.second;
    if (!box_in_frustum(frustum, mesh.bounds)) {
      ++stats_.culled_sections;
      continue;
    }
    if (mesh.material == REIM_MATERIAL_TRANSPARENT) {
      transparent.push_back(&mesh);
    } else {
      opaque.push_back(&mesh);
    }
  }
  const auto near_first = [&](const GpuMesh* a, const GpuMesh* b) {
    return distance_squared(bounds_center(a->bounds), eye) <
           distance_squared(bounds_center(b->bounds), eye);
  };
  std::sort(opaque.begin(), opaque.end(), near_first);
  std::sort(transparent.begin(), transparent.end(),
            [&](const GpuMesh* a, const GpuMesh* b) { return near_first(b, a); });

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  glDisable(GL_BLEND);

  glUseProgram(chunk_program_);
  glUniformMatrix4fv(chunk_view_proj_, 1, GL_FALSE, view_proj.m);
  glUniform3f(chunk_fog_color_, fog[0], fog[1], fog[2]);
  glUniform1f(chunk_fog_start_, frame.environment.fog_start);
  glUniform1f(chunk_fog_end_,
              frame.environment.fog_end > frame.environment.fog_start
                  ? frame.environment.fog_end
                  : far_z);
  glUniform3f(chunk_camera_, eye.x, eye.y, eye.z);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glUniform1i(chunk_atlas_, 0);
  glUniform1i(chunk_transparent_, 0);
  draw_meshes(opaque);

  if (!transparent.empty()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1i(chunk_transparent_, 1);
    draw_meshes(transparent);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  if (options.debug_player_aabb) {
    glDisable(GL_CULL_FACE);
    draw_player_box(frame, view_proj);
  }
  glBindVertexArray(0);
  glUseProgram(0);
}

} // namespace reim::preview
