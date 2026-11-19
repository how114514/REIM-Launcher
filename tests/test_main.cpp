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
  manifest["format"] = 1;
  manifest["profile"] = "26.3";
  manifest["abi_version"] = 1;
  manifest["tool"] = "m1-tests";
  manifest["tool_version"] = "1";
  manifest["counts"] = nlohmann::json{{"blocks_total", 0}, {"sprites", 0}};
  manifest["generated"] = generated;
  std::string error;
  REQUIRE(reim::file::write_text(dir / "manifest.json", manifest.dump(2), error));
  REQUIRE(error.empty());
  return manifest.dump(2);
}

} // namespace

TEST_CASE("ABI v1 layout is stable") {
  REQUIRE(REIM_ABI_VERSION == 1u);
  REQUIRE(sizeof(ReimVertex) == REIM_VERTEX_STRIDE);
  REQUIRE(offsetof(ReimVertex, position) == 0);
  REQUIRE(offsetof(ReimVertex, normal) == 12);
  REQUIRE(offsetof(ReimVertex, uv) == 24);
  REQUIRE(offsetof(ReimVertex, light) == 32);
  REQUIRE(offsetof(ReimVertex, sprite) == 34);
  REQUIRE(offsetof(ReimVertex, tint) == 36);
}

TEST_CASE("DLL exports reim_get_api with the expected ABI") {
  const ReimRuntimeApi* api = reim_get_api(REIM_ABI_VERSION);
  REQUIRE(api != nullptr);
  REQUIRE(api->abi_version == REIM_ABI_VERSION);
  REQUIRE(api->struct_size == sizeof(ReimRuntimeApi));
  REQUIRE(api->create != nullptr);
  REQUIRE(api->fetch_frame != nullptr);
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
  REQUIRE(reim::data::validate_generated(dir, "26.3", 1, manifest, error));
  REQUIRE(manifest.profile == "26.3");
  REQUIRE(manifest.files.size() == 3);

  REQUIRE_FALSE(reim::data::validate_generated(dir, "26.2", 1, manifest, error));
  REQUIRE(error.find("profile mismatch") != std::string::npos);

  REQUIRE_FALSE(reim::data::validate_generated(dir, "26.3", 2, manifest, error));
  REQUIRE(error.find("ABI mismatch") != std::string::npos);

  std::string write_error;
  REQUIRE(reim::file::write_text(dir / "blocks.json", "tampered", write_error));
  REQUIRE_FALSE(reim::data::validate_generated(dir, "26.3", 1, manifest, error));
  REQUIRE((error.find("size mismatch") != std::string::npos ||
           error.find("sha256 mismatch") != std::string::npos));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
