#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

int run_prepare(const Args& args, const std::filesystem::path& exe_dir);

/* Resolves and reads the input manifest written by `prepare`. */
struct InputManifest {
  Json json;
  std::string profile;
  std::filesystem::path client_jar;
  std::filesystem::path version_json;
  std::filesystem::path asset_index;
  std::filesystem::path minecraft_dir;
  std::filesystem::path version_dir;
  std::string version_id;
  std::string client_jar_sha256;
  std::string version_json_sha256;
  std::string asset_index_sha256;
};

bool load_input_manifest(const std::filesystem::path& profile_dir, InputManifest& out,
                         std::string& error);

} // namespace reim::conv
