#include "gl_renderer.h"

#include "util/file.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "json.hpp"
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
  // Opaque pass is alpha-masked (leaves, grass crosses, flowers); water and
  // other blended blocks keep their alpha.
  if (u_transparent == 0 && alpha < 0.5) discard;
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
  return initialize_from_png(bytes.data(), bytes.size(),
                             reim::file::to_utf8(atlas_path), error);
}

bool Renderer::initialize_from_png(const uint8_t* data, size_t size,
                                   const std::string& source, std::string& error) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size),
                                          &width, &height, &channels, 4);
  if (pixels == nullptr) {
    error = "cannot decode " + source;
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

  /* Dynamic VAO/VBO for Contract entity boxes (same ReimVertex layout). */
  glGenVertexArrays(1, &entity_vao_);
  glGenBuffers(1, &entity_vbo_);
  glBindVertexArray(entity_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, REIM_VERTEX_STRIDE, nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, REIM_VERTEX_STRIDE,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(12)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, REIM_VERTEX_STRIDE,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(24)));
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, REIM_VERTEX_STRIDE,
                         reinterpret_cast<const void*>(static_cast<uintptr_t>(32)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, REIM_VERTEX_STRIDE,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(36)));
  glEnableVertexAttribArray(4);
  glBindVertexArray(0);

  /* J5/J6: 1x1 white texture for untextured particles/sky billboards. */
  glGenTextures(1, &white_texture_);
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  const uint8_t white[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  reim::log::info("preview", "atlas " + std::to_string(width) + "x" +
                                 std::to_string(height) + " loaded from " + source);
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
  if (entity_vbo_ != 0) glDeleteBuffers(1, &entity_vbo_);
  if (entity_vao_ != 0) glDeleteVertexArrays(1, &entity_vao_);
  if (white_texture_ != 0) glDeleteTextures(1, &white_texture_);
  if (item_texture_ != 0) glDeleteTextures(1, &item_texture_);
  if (atlas_texture_ != 0) glDeleteTextures(1, &atlas_texture_);
  if (chunk_program_ != 0) glDeleteProgram(chunk_program_);
  if (line_program_ != 0) glDeleteProgram(line_program_);
  line_vbo_ = line_vao_ = entity_vbo_ = entity_vao_ = atlas_texture_ = 0;
  white_texture_ = 0;
  item_texture_ = 0;
  item_sprites_.clear();
  item_names_.clear();
  chunk_program_ = line_program_ = 0;
  entity_models_.clear();
}

bool Renderer::load_model_defs(const void* data, size_t size, std::string& error) {
  const auto* text = static_cast<const char*>(data);
  const auto root = nlohmann::json::parse(text, text + size, nullptr, false);
  if (root.is_discarded() || !root.is_object() || !root.contains("models") ||
      !root["models"].is_array() || root["models"].empty()) {
    error = "MODEL_DEFS_JSON has no models";
    return false;
  }
  const nlohmann::json* model = nullptr;
  for (const auto& candidate : root["models"]) {
    if (candidate.value("id", -1) == 0) {
      model = &candidate;
      break;
    }
  }
  if (model == nullptr) model = &root["models"][0];
  if (!model->contains("parts") || !(*model)["parts"].is_array()) {
    error = "model 0 has no parts";
    return false;
  }
  entity_models_.clear();
  default_entity_model_ = 0;
  for (const auto& candidate : root["models"]) {
    if (!candidate.contains("parts") || !candidate["parts"].is_array()) continue;
    std::vector<EntityPartDef> parts;
    for (const auto& part : candidate["parts"]) {
      EntityPartDef def;
      if (part.contains("size") && part["size"].is_array() &&
          part["size"].size() >= 3) {
        for (int i = 0; i < 3; ++i) def.size[i] = part["size"][i].get<float>();
      }
      if (part.contains("pivot") && part["pivot"].is_array() &&
          part["pivot"].size() >= 3) {
        for (int i = 0; i < 3; ++i) def.pivot[i] = part["pivot"][i].get<float>();
      }
      if (part.contains("uv") && part["uv"].is_array() && part["uv"].size() >= 4) {
        for (int i = 0; i < 4; ++i) def.uv[i] = part["uv"][i].get<float>();
      }
      if (part.contains("color") && part["color"].is_number_unsigned()) {
        def.color = part["color"].get<uint32_t>() & 0xFFFFFFu;
      }
      parts.push_back(def);
    }
    if (parts.empty()) continue;
    const uint32_t id = candidate.value("id", 0u);
    entity_models_[id] = std::move(parts);
    if (id == 0) default_entity_model_ = 0;
  }
  if (entity_models_.empty()) {
    error = "MODEL_DEFS_JSON has no usable parts";
    return false;
  }
  if (entity_models_.find(0) == entity_models_.end()) {
    default_entity_model_ = entity_models_.begin()->first;
  }
  return true;
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

void Renderer::draw_entities(const ReimRenderFrame& frame) {
  if (frame.entities == nullptr || frame.entity_count == 0 ||
      entity_models_.empty() || entity_vao_ == 0) {
    return;
  }
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kDeg = kPi / 180.0f;
  auto rotate = [kDeg](const float in[3], const float rotation[3], float out[3]) {
    float x = in[0];
    float y = in[1];
    float z = in[2];
    const float rx = rotation[0] * kDeg;
    const float ry = rotation[1] * kDeg;
    const float rz = rotation[2] * kDeg;
    float ny = y * std::cos(rx) - z * std::sin(rx);
    float nz = y * std::sin(rx) + z * std::cos(rx);
    y = ny;
    z = nz;
    const float nx2 = x * std::cos(ry) + z * std::sin(ry);
    nz = -x * std::sin(ry) + z * std::cos(ry);
    x = nx2;
    z = nz;
    ny = x * std::sin(rz) + y * std::cos(rz);
    const float nx3 = x * std::cos(rz) - y * std::sin(rz);
    out[0] = nx3;
    out[1] = ny;
    out[2] = z;
  };
  auto model_for = [&](uint32_t model_id) -> const std::vector<EntityPartDef>* {
    auto found = entity_models_.find(model_id);
    if (found != entity_models_.end()) return &found->second;
    found = entity_models_.find(default_entity_model_);
    return found != entity_models_.end() ? &found->second : nullptr;
  };

  std::vector<ReimVertex> vertices;
  vertices.reserve(static_cast<size_t>(frame.entity_count) * 36 * 4);
  const int faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6},
                           {0, 4, 7, 3}, {3, 7, 6, 2}, {0, 1, 5, 4}};
  const float base_normals[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                                    {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
  const int order[6] = {0, 1, 2, 0, 2, 3};

  for (uint32_t index = 0; index < frame.entity_count; ++index) {
    const ReimEntityRender& entity = frame.entities[index];
    if ((entity.flags & REIM_ENTITY_FLAG_INVISIBLE) != 0) continue;
    const std::vector<EntityPartDef>* model = model_for(entity.model_id);
    if (model == nullptr) continue;
    const float yaw = entity.yaw * kDeg;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const uint32_t count =
        std::min<uint32_t>(static_cast<uint32_t>(model->size()),
                           entity.parts != nullptr && entity.part_count != 0
                               ? entity.part_count
                               : static_cast<uint32_t>(model->size()));
    for (uint32_t p = 0; p < count; ++p) {
      const EntityPartDef& part = (*model)[p];
      ReimPartTransform transform{};
      if (entity.parts != nullptr && p < entity.part_count) {
        transform = entity.parts[p];
      }
      const uint32_t color = part.color;
      int r = static_cast<int>(color & 0xFF);
      int g = static_cast<int>((color >> 8) & 0xFF);
      int b = static_cast<int>((color >> 16) & 0xFF);
      if ((entity.flags & REIM_ENTITY_FLAG_HURT) != 0) {
        r = std::min(255, r + 120);
      }
      if ((entity.flags & REIM_ENTITY_FLAG_ON_FIRE) != 0) {
        r = 255;
        g = std::max(0, g - 80);
      }
      const uint32_t tint = 0xFF000000u | (static_cast<uint32_t>(b) << 16) |
                            (static_cast<uint32_t>(g) << 8) |
                            static_cast<uint32_t>(r);
      const float hx = part.size[0] * 0.5f * (transform.scale[0] != 0.0f ? transform.scale[0] : 1.0f);
      const float hy = part.size[1] * 0.5f * (transform.scale[1] != 0.0f ? transform.scale[1] : 1.0f);
      const float hz = part.size[2] * 0.5f * (transform.scale[2] != 0.0f ? transform.scale[2] : 1.0f);
      const float corners[8][3] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz},
                                   {-hx, hy, -hz},  {-hx, -hy, hz}, {hx, -hy, hz},
                                   {hx, hy, hz},    {-hx, hy, hz}};
      float local[8][3];
      for (int c = 0; c < 8; ++c) {
        float rotated[3];
        rotate(corners[c], transform.rotation, rotated);
        local[c][0] = part.pivot[0] + rotated[0] + transform.translation[0];
        local[c][1] = part.pivot[1] + rotated[1] + transform.translation[1];
        local[c][2] = part.pivot[2] + rotated[2] + transform.translation[2];
      }
      for (int face = 0; face < 6; ++face) {
        float normal[3];
        rotate(base_normals[face], transform.rotation, normal);
        const float nx = normal[0] * cy + normal[2] * sy;
        const float nz = -normal[0] * sy + normal[2] * cy;
        const float uv[4][2] = {{part.uv[0], part.uv[3]},
                                {part.uv[2], part.uv[3]},
                                {part.uv[2], part.uv[1]},
                                {part.uv[0], part.uv[1]}};
        for (int corner = 0; corner < 6; ++corner) {
          const int slot = order[corner];
          const float lx = local[faces[face][slot]][0];
          const float ly = local[faces[face][slot]][1];
          const float lz = local[faces[face][slot]][2];
          ReimVertex vertex{};
          vertex.position[0] = entity.position.x + lx * cy + lz * sy;
          vertex.position[1] = entity.position.y + ly;
          vertex.position[2] = entity.position.z - lx * sy + lz * cy;
          vertex.normal[0] = nx;
          vertex.normal[1] = normal[1];
          vertex.normal[2] = nz;
          vertex.uv[0] = uv[slot][0];
          vertex.uv[1] = uv[slot][1];
          vertex.light = 0x0F0Fu;
          vertex.sprite = 0;
          vertex.tint = tint;
          vertices.push_back(vertex);
        }
      }
    }
  }
  if (vertices.empty()) return;
  glBindVertexArray(entity_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(ReimVertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
  ++stats_.draw_calls;
  stats_.triangles += static_cast<uint32_t>(vertices.size() / 3);
}

bool Renderer::load_item_assets(const void* png, size_t png_size, const void* meta,
                                size_t meta_size, const void* item_index,
                                size_t item_index_size, std::string& error) {
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* pixels = stbi_load_from_memory(
      static_cast<const unsigned char*>(png), static_cast<int>(png_size), &width,
      &height, &channels, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    error = "item atlas PNG rejected by stb_image";
    if (pixels) stbi_image_free(pixels);
    return false;
  }
  if (item_texture_ != 0) glDeleteTextures(1, &item_texture_);
  glGenTextures(1, &item_texture_);
  glBindTexture(GL_TEXTURE_2D, item_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  stbi_image_free(pixels);

  const auto meta_json = nlohmann::json::parse(
      static_cast<const char*>(meta), static_cast<const char*>(meta) + meta_size,
      nullptr, false);
  if (meta_json.is_discarded() || !meta_json.is_object()) {
    error = "item atlas metadata is not JSON";
    return false;
  }
  const auto sprites = meta_json.find("sprites");
  if (sprites != meta_json.end() && sprites->is_object()) {
    for (auto it = sprites->begin(); it != sprites->end(); ++it) {
      if (!it.value().is_object()) continue;
      SpriteRect rect;
      rect.u0 = it.value().value("u0", 0.0f);
      rect.v0 = it.value().value("v0", 0.0f);
      rect.u1 = it.value().value("u1", 1.0f);
      rect.v1 = it.value().value("v1", 1.0f);
      item_sprites_[it.key()] = rect;
    }
  }
  const auto index_json = nlohmann::json::parse(
      static_cast<const char*>(item_index), static_cast<const char*>(item_index) +
                                                item_index_size, nullptr, false);
  if (!index_json.is_discarded() && index_json.is_object()) {
    const auto items = index_json.find("items");
    if (items != index_json.end() && items->is_array()) {
      for (const nlohmann::json& item : *items) {
        if (!item.is_object()) continue;
        item_names_[item.value("id", 0u)] = item.value("name", "");
      }
    }
  }
  return true;
}

void Renderer::draw_held_item(const ReimRenderFrame& frame) {
  if (item_texture_ == 0 || item_sprites_.empty() ||
      frame.player.held_item.item_id == 0) {
    return;
  }
  const auto name_it = item_names_.find(frame.player.held_item.item_id);
  if (name_it == item_names_.end()) return;
  std::string bare = name_it->second;
  const size_t colon = bare.find(':');
  if (colon != std::string::npos) bare = bare.substr(colon + 1);
  auto sprite = item_sprites_.find("item/" + bare);
  if (sprite == item_sprites_.end()) return;

  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = frame.player.yaw * kPi / 180.0f;
  const float pitch = frame.player.pitch * kPi / 180.0f;
  const float fx = std::sin(yaw) * std::cos(pitch);
  const float fy = -std::sin(pitch);
  const float fz = std::cos(yaw) * std::cos(pitch);
  const float rx = std::cos(yaw);
  const float rz = -std::sin(yaw);
  const float bob = std::sin(static_cast<float>(frame.tick) * 0.35f) * 0.012f;
  const float swing = static_cast<float>(frame.player.use_ticks) * 0.004f;
  const ReimVec3 eye = frame.camera.position;
  const float cx = eye.x + fx * 0.55f + rx * 0.32f;
  const float cy = eye.y + fy * 0.55f - 0.30f + bob - swing;
  const float cz = eye.z + fz * 0.55f + rz * 0.32f;
  const float half = 0.15f;
  const float uvs[4][2] = {{sprite->second.u0, sprite->second.v1},
                           {sprite->second.u1, sprite->second.v1},
                           {sprite->second.u1, sprite->second.v0},
                           {sprite->second.u0, sprite->second.v0}};
  const int order[6] = {0, 1, 2, 0, 2, 3};
  std::vector<ReimVertex> vertices;
  vertices.reserve(6);
  for (int i = 0; i < 6; ++i) {
    const int c = order[i];
    const float sx = (c == 0 || c == 3) ? -1.0f : 1.0f;
    const float sy = (c < 2) ? -1.0f : 1.0f;
    ReimVertex vertex{};
    vertex.position[0] = cx + rx * sx * half;
    vertex.position[1] = cy + sy * half;
    vertex.position[2] = cz + rz * sx * half;
    vertex.normal[2] = 1.0f;
    vertex.uv[0] = uvs[c][0];
    vertex.uv[1] = uvs[c][1];
    vertex.light = 0x0F0Fu;
    vertex.tint = 0xFFFFFFFFu;
    vertices.push_back(vertex);
  }
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindTexture(GL_TEXTURE_2D, item_texture_);
  glUniform1i(chunk_transparent_, 1);
  glBindVertexArray(entity_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(ReimVertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
  glUniform1i(chunk_transparent_, 0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  ++stats_.draw_calls;
  stats_.triangles += 2;
}

void Renderer::draw_sky(const ReimRenderFrame& frame, const ReimVec3& eye) {
  if (white_texture_ == 0) return;
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = frame.camera.yaw * kPi / 180.0f;
  const float rx = std::cos(yaw);
  const float rz = -std::sin(yaw);
  auto quad = [&](std::vector<ReimVertex>& out, float cx, float cy, float cz,
                  float half_w, float half_h, uint32_t color) {
    const float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
      const int c = order[i];
      ReimVertex vertex{};
      vertex.position[0] = cx + rx * corners[c][0] * half_w + ux * corners[c][1] * half_h;
      vertex.position[1] = cy + uy * corners[c][1] * half_h;
      vertex.position[2] = cz + rz * corners[c][0] * half_w + uz * corners[c][1] * half_h;
      vertex.normal[2] = 1.0f;
      vertex.uv[0] = corners[c][0] > 0 ? 1.0f : 0.0f;
      vertex.uv[1] = corners[c][1] > 0 ? 1.0f : 0.0f;
      vertex.light = 0x0F0Fu;
      vertex.tint = color;
      out.push_back(vertex);
    }
  };
  std::vector<ReimVertex> vertices;
  const float angle = frame.environment.sun_angle * kPi / 180.0f;
  const float radius = 180.0f;
  const float sun_x = eye.x + std::cos(angle) * radius;
  const float sun_y = eye.y + std::sin(angle) * radius;
  const float sun_z = eye.z + 40.0f;
  quad(vertices, sun_x, sun_y, sun_z, 11.0f, 11.0f, 0xFFFFFFFFu);
  quad(vertices, sun_x, sun_y, sun_z, 8.0f, 8.0f, 0xFF60D0FFu); // ABGR: warm sun core
  quad(vertices, eye.x - std::cos(angle) * radius, eye.y - std::sin(angle) * radius,
       eye.z - 40.0f, 8.0f, 8.0f, 0xFFE0E0E0u);
  if (frame.environment.sky_darken > 0.35f) {
    for (uint32_t i = 0; i < 120; ++i) {
      uint32_t h = i * 2654435761u;
      const float sx = static_cast<float>(h & 0xFFu) / 255.0f * 2.0f - 1.0f;
      h = h * 1103515245u + 12345u;
      const float sy = static_cast<float>(h & 0xFFu) / 255.0f;
      h = h * 1103515245u + 12345u;
      const float sz = static_cast<float>(h & 0xFFu) / 255.0f * 2.0f - 1.0f;
      const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
      if (len < 0.001f) continue;
      quad(vertices, eye.x + sx / len * 190.0f, eye.y + sy / len * 190.0f,
           eye.z + sz / len * 190.0f, 0.7f, 0.7f, 0xD0FFFFFFu);
    }
  }
  const float scroll = static_cast<float>(frame.environment.time_of_day % 24000) * 0.01f;
  for (int i = 0; i < 20; ++i) {
    const float x = (static_cast<float>(i % 5) - 2.0f) * 36.0f + scroll;
    const float z = (static_cast<float>(i / 5) - 2.0f) * 36.0f - scroll;
    quad(vertices, eye.x + x, eye.y + 90.0f, eye.z + z, 18.0f, 8.0f, 0x70FFFFFFu);
  }
  if (vertices.empty()) return;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform1i(chunk_transparent_, 1);
  glBindVertexArray(entity_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(ReimVertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
  glUniform1i(chunk_transparent_, 0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  stats_.draw_calls += 1;
  stats_.triangles += static_cast<uint32_t>(vertices.size() / 3);
}

void Renderer::draw_particles(const ReimRenderFrame& frame) {
  if (frame.particles == nullptr || frame.particle_count == 0 ||
      white_texture_ == 0 || entity_vao_ == 0) {
    return;
  }
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = frame.camera.yaw * kPi / 180.0f;
  const float rx = std::cos(yaw);
  const float rz = -std::sin(yaw);
  const float alpha = std::clamp(frame.tick_alpha, 0.0f, 1.0f);
  std::vector<ReimVertex> vertices;
  vertices.reserve(static_cast<size_t>(frame.particle_count) * 6);
  for (uint32_t i = 0; i < frame.particle_count; ++i) {
    const ReimParticle& particle = frame.particles[i];
    if (particle.age >= particle.lifetime) continue;
    const float px = particle.prev_position.x +
                     (particle.position.x - particle.prev_position.x) * alpha;
    const float py = particle.prev_position.y +
                     (particle.position.y - particle.prev_position.y) * alpha;
    const float pz = particle.prev_position.z +
                     (particle.position.z - particle.prev_position.z) * alpha;
    const float dx = px - frame.camera.position.x;
    const float dy = py - frame.camera.position.y;
    const float dz = pz - frame.camera.position.z;
    if (dx * dx + dy * dy + dz * dz > 128.0f * 128.0f) continue;
    float half_w = particle.size;
    float half_h = particle.size;
    if (particle.type == 3) {
      half_w = particle.size * 0.45f;
      half_h = particle.size * 5.0f;
    } else if (particle.type == 2) {
      half_w = particle.size * 0.8f;
      half_h = particle.size * 0.8f;
    }
    uint32_t color = particle.color;
    const float fade = 1.0f - particle.age / std::max(1.0f, particle.lifetime);
    const uint32_t a = static_cast<uint32_t>(
        std::clamp(((color >> 24) & 0xFFu) * fade, 0.0f, 255.0f));
    color = (color & 0x00FFFFFFu) | (a << 24);
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int c = 0; c < 6; ++c) {
      const int slot = order[c];
      ReimVertex vertex{};
      vertex.position[0] = px + rx * corners[slot][0] * half_w;
      vertex.position[1] = py + corners[slot][1] * half_h;
      vertex.position[2] = pz + rz * corners[slot][0] * half_w;
      vertex.normal[2] = 1.0f;
      vertex.uv[0] = corners[slot][0] > 0 ? 1.0f : 0.0f;
      vertex.uv[1] = corners[slot][1] > 0 ? 1.0f : 0.0f;
      vertex.light = 0x0F0Fu;
      vertex.tint = color;
      vertices.push_back(vertex);
    }
  }
  if (vertices.empty()) return;
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform1i(chunk_transparent_, 1);
  glBindVertexArray(entity_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, entity_vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(ReimVertex)),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindVertexArray(0);
  glUniform1i(chunk_transparent_, 0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  ++stats_.draw_calls;
  stats_.triangles += static_cast<uint32_t>(vertices.size() / 3);
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
  const bool underwater = (frame.player.flags & REIM_PLAYER_IN_WATER) != 0;
  const bool sprinting = (frame.player.flags & REIM_PLAYER_SPRINTING) != 0;
  float fov = frame.camera.fov_y_degrees > 1.0f ? frame.camera.fov_y_degrees : 70.0f;
  if (sprinting) fov += 4.0f;
  if (underwater) fov -= 4.0f;
  if (underwater) {
    sky[0] = 0.06f;
    sky[1] = 0.18f;
    sky[2] = 0.34f;
    fog[0] = 0.05f;
    fog[1] = 0.14f;
    fog[2] = 0.28f;
    glClearColor(sky[0], sky[1], sky[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  const Mat4 projection = perspective(fov, aspect, near_z, far_z);
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
  glUniform1f(chunk_fog_start_, underwater ? 0.0f : frame.environment.fog_start);
  glUniform1f(chunk_fog_end_,
              underwater ? 18.0f
                         : (frame.environment.fog_end > frame.environment.fog_start
                                ? frame.environment.fog_end
                                : far_z));
  glUniform3f(chunk_camera_, eye.x, eye.y, eye.z);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, atlas_texture_);
  glUniform1i(chunk_atlas_, 0);
  glUniform1i(chunk_transparent_, 0);
  draw_sky(frame, eye);
  draw_meshes(opaque);
  /* Entity boxes are Contract instances; animation comes from the Runtime. */
  draw_entities(frame);

  if (!transparent.empty()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1i(chunk_transparent_, 1);
    draw_meshes(transparent);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
  }

  draw_particles(frame);
  draw_held_item(frame);

  if (options.debug_player_aabb) {
    glDisable(GL_CULL_FACE);
    draw_player_box(frame, view_proj);
  }
  glBindVertexArray(0);
  glUseProgram(0);
}

} // namespace reim::preview
