#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "data/generated_manifest.h"
#include "reim_abi.h"
#include "samples/packed_bits.h"
#include "util/file.h"
#include "util/hash.h"
#include "util/utf.h"

#include "json.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string write_manifest_case(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  struct Item {
    const char* name;
    std::string_view content;
  };
  const Item items[] = {
      {"blocks.json", "{\"blocks\":{}}\n"},
      {"textures.json", "{\"sprites\":{}}\n"},
      {"atlas.png", "not-a-real-png-for-manifest-test\n"},
      {"items.json", "{\"items\":{}}\n"},
      {"entities.json", "{\"entities\":{}}\n"},
      {"models.json", "{\"blocks\":{}}\n"},
      {"recipes.json", "{\"recipes\":{}}\n"},
      {"loot_tables.json", "{\"tables\":{}}\n"},
      {"tags.json", "{\"types\":{}}\n"},
      {"worldgen.json", "{\"types\":{}}\n"},
      {"sounds.json", "{\"m3\":[]}\n"},
      {"lang.json", "{\"language\":\"en_us\"}\n"},
      {"coverage_report.json", "{\"schema\":\"test\"}\n"},
  };
  nlohmann::json generated = nlohmann::json::array();
  for (const Item& item : items) {
    std::string error;
    REQUIRE(reim::file::write_text(dir / item.name, std::string(item.content), error));
    REQUIRE(error.empty());
    const auto digest = reim::hash::sha256(item.content);
    generated.push_back(nlohmann::json{{"path", item.name},
                                       {"size", item.content.size()},
                                       {"sha256", reim::hash::hex(digest)}});
  }
  nlohmann::json manifest;
  manifest["schema"] = "reim.generated.manifest";
  manifest["format"] = 3;
  manifest["profile"] = "26.3";
  manifest["abi_version"] = REIM_ABI_VERSION;
  manifest["tool"] = "m1-tests";
  manifest["tool_version"] = "1";
  manifest["capabilities"] = nlohmann::json::array(
      {"frame_blob", "interpolation", "atlas_asset", "r0"});
  manifest["counts"] = nlohmann::json{{"blocks_total", 0}, {"sprites", 0}};
  manifest["atlas"] = nlohmann::json{{"file", "atlas.png"},
                                     {"width", 16},
                                     {"height", 16},
                                     {"tile_size", 16},
                                     {"meta_version", 2},
                                     {"sha256", reim::hash::hex(
                                                    reim::hash::sha256(
                                                        "not-a-real-png-for-manifest-test\n"))}};
  manifest["generated"] = generated;
  std::string error;
  REQUIRE(reim::file::write_text(dir / "manifest.json", manifest.dump(2), error));
  REQUIRE(error.empty());
  return manifest.dump(2);
}

} // namespace

TEST_CASE("ABI v3 layout is stable") {
  REQUIRE(REIM_ABI_VERSION == 3u);
  REQUIRE(sizeof(ReimVertex) == REIM_VERTEX_STRIDE);
  REQUIRE(offsetof(ReimVertex, position) == 0);
  REQUIRE(offsetof(ReimVertex, normal) == 12);
  REQUIRE(offsetof(ReimVertex, uv) == 24);
  REQUIRE(offsetof(ReimVertex, light) == 32);
  REQUIRE(offsetof(ReimVertex, sprite) == 34);
  REQUIRE(offsetof(ReimVertex, tint) == 36);

  /* v2 frame ownership fields are inside the required minimum size. */
  REQUIRE(REIM_RENDER_FRAME_MIN_SIZE >=
          offsetof(ReimRenderFrame, owned_bytes) + sizeof(uint64_t));
  REQUIRE(REIM_MAX_PENDING_FRAMES == 4u);
  REQUIRE(REIM_MAX_FRAME_UPDATES == 4096u);
  REQUIRE(REIM_MAX_FRAME_BYTES == 64ull * 1024ull * 1024ull);
  REQUIRE(REIM_INSTANCE_CONFIG_MIN_SIZE >=
          offsetof(ReimInstanceConfig, spawn_pitch) + sizeof(float));

  /* v3 append-only prefixes and the new channel layouts. */
  REQUIRE(REIM_INSTANCE_CONFIG_MIN_SIZE >=
          offsetof(ReimInstanceConfig, seed) + sizeof(uint64_t));
  REQUIRE(REIM_RENDER_FRAME_MIN_SIZE >=
          offsetof(ReimRenderFrame, reserved3) + sizeof(uint32_t) * 8);
  REQUIRE(offsetof(ReimRenderFrame, interaction) >
          offsetof(ReimRenderFrame, owned_bytes));
  REQUIRE(REIM_ACTION_EVENT_MIN_SIZE <= sizeof(ReimActionEvent));
  REQUIRE(REIM_ACTIONTYPE_WHEEL == 20u);
  REQUIRE(REIM_MAX_ACTION_QUEUE == 1024u);
  REQUIRE(REIM_MAX_UI_DRAWS == 8192u);
  REQUIRE(REIM_MAX_FRAME_ENTITIES == 8192u);
  REQUIRE(REIM_MAX_FRAME_PART_TRANSFORMS == 1048576u);
  REQUIRE(REIM_MAX_FRAME_AUDIO_EVENTS == 4096u);
  REQUIRE(offsetof(ReimUiDraw, text_offset) == 64u);
  REQUIRE(offsetof(ReimAudioEvent, sequence) == 40u);
  REQUIRE(sizeof(ReimPartTransform) == 48u);
}

TEST_CASE("DLL exports reim_get_api with the expected ABI") {
  const ReimRuntimeApi* api = reim_get_api(REIM_ABI_VERSION);
  REQUIRE(api != nullptr);
  REQUIRE(api->abi_version == REIM_ABI_VERSION);
  REQUIRE(api->struct_size == sizeof(ReimRuntimeApi));
  REQUIRE(api->create != nullptr);
  REQUIRE(api->fetch_frame != nullptr);
  REQUIRE(api->release_frame != nullptr);
  REQUIRE(api->get_runtime_info != nullptr);
  REQUIRE(api->get_asset != nullptr);
  REQUIRE(api->submit_action != nullptr);
  REQUIRE(api->request_save != nullptr);
  REQUIRE(api->get_save_status != nullptr);
  REQUIRE(api->set_ui_canvas != nullptr);
  REQUIRE(reim_get_api(REIM_ABI_VERSION + 1) == nullptr);
}

TEST_CASE("SHA-256 and SHA-1 reference vectors") {
  REQUIRE(reim::hash::hex(reim::hash::sha256("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE(reim::hash::hex(reim::hash::sha1("abc")) ==
          "a9993e364706816aba3e25717850c26c9cd0d89d");
  REQUIRE(reim::hash::hex(reim::hash::sha256("")) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("modified UTF-8 matches Java DataInput semantics") {
  const char nul_encoded[] = {'A', static_cast<char>(0xC0), static_cast<char>(0x80), 'B'};
  REQUIRE(reim::utf::modified_utf8_to_utf8(nul_encoded, sizeof(nul_encoded)) ==
          std::string("A\0B", 3));

  const char surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0),
                            static_cast<char>(0xBD), static_cast<char>(0xED),
                            static_cast<char>(0xB8), static_cast<char>(0x80)};
  REQUIRE(reim::utf::modified_utf8_to_utf8(surrogate, sizeof(surrogate)) ==
          "\xF0\x9F\x98\x80");

  const char cjk[] = {static_cast<char>(0xE4), static_cast<char>(0xBD),
                      static_cast<char>(0xA0), static_cast<char>(0xE5),
                      static_cast<char>(0xA5), static_cast<char>(0xBD)};
  REQUIRE(reim::utf::modified_utf8_to_utf8(cjk, sizeof(cjk)) == "\xE4\xBD\xA0\xE5\xA5\xBD");
}

TEST_CASE("palette packed bits round-trip") {
  for (unsigned bits : {4u, 5u, 6u, 8u, 12u}) {
    std::vector<uint32_t> values(4096);
    const uint32_t mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    for (size_t i = 0; i < values.size(); ++i) {
      values[i] = static_cast<uint32_t>((i * 2654435761u) & mask);
    }
    const auto packed = reim::samples::pack_packed(values.data(), values.size(), bits);
    std::vector<uint32_t> unpacked;
    REQUIRE(reim::samples::unpack_packed(packed.data(), packed.size(), bits,
                                         values.size(), unpacked));
    REQUIRE(unpacked == values);
  }
  REQUIRE(reim::samples::bits_for_palette(1) == 4u);
  REQUIRE(reim::samples::bits_for_palette(16) == 4u);
  REQUIRE(reim::samples::bits_for_palette(17) == 5u);
  REQUIRE(reim::samples::bits_for_palette(4096) == 12u);
}

TEST_CASE("generated manifest accepts matching input and rejects mismatches") {
  const auto dir = std::filesystem::temp_directory_path() / "reim-m1-manifest-test";
  write_manifest_case(dir);

  reim::data::GeneratedManifest manifest;
  std::string error;
  REQUIRE(reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION, manifest, error));
  REQUIRE(manifest.profile == "26.3");
  REQUIRE(manifest.format == 3);
  REQUIRE(manifest.files.size() == 13);
  REQUIRE(manifest.atlas_meta_version == 2);

  /* ABI 3 rejects pre-v3 generated data instead of guessing. */
  nlohmann::json old_manifest = nlohmann::json::parse(write_manifest_case(dir));
  old_manifest["format"] = 1;
  std::string ignored;
  REQUIRE(reim::file::write_text(dir / "manifest.json", old_manifest.dump(2), ignored));
  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION, manifest, error));
  REQUIRE(error.find("format 1") != std::string::npos);

  /* ABI 3 also rejects the old v2 manifest with a clear schema error. */
  nlohmann::json v2_manifest = nlohmann::json::parse(write_manifest_case(dir));
  v2_manifest["format"] = 2;
  REQUIRE(reim::file::write_text(dir / "manifest.json", v2_manifest.dump(2), ignored));
  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION, manifest, error));
  REQUIRE(error.find("format 2") != std::string::npos);

  /* v3 requires the data-scale outputs, not just the block atlas. */
  nlohmann::json missing_file = nlohmann::json::parse(write_manifest_case(dir));
  auto& generated_list = missing_file["generated"];
  for (auto it = generated_list.begin(); it != generated_list.end(); ++it) {
    if (it->value("path", "") == "coverage_report.json") {
      generated_list.erase(it);
      break;
    }
  }
  REQUIRE(reim::file::write_text(dir / "manifest.json", missing_file.dump(2), ignored));
  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION, manifest, error));
  REQUIRE(error.find("coverage_report.json") != std::string::npos);
  REQUIRE(reim::file::write_text(dir / "manifest.json",
                                 nlohmann::json::parse(write_manifest_case(dir)).dump(2),
                                 ignored));

  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.2", REIM_ABI_VERSION, manifest, error));
  REQUIRE(error.find("profile mismatch") != std::string::npos);

  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION + 1, manifest, error));
  REQUIRE(error.find("ABI mismatch") != std::string::npos);

  std::string write_error;
  REQUIRE(reim::file::write_text(dir / "blocks.json", "tampered", write_error));
  REQUIRE_FALSE(
      reim::data::validate_generated(dir, "26.3", REIM_ABI_VERSION, manifest, error));
  REQUIRE((error.find("size mismatch") != std::string::npos ||
           error.find("sha256 mismatch") != std::string::npos));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
