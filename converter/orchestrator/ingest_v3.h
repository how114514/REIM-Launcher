#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>

namespace reim::conv {

/* M3-K2..K9: turns the M3-K1 work profile into schema-v3 generated data.
 *
 * Inputs (produced by `reim-convert data`):
 *   <profile_dir>/reports/reports/*.json
 *   <profile_dir>/datapack/data/minecraft/**
 *   <profile_dir>/assets/minecraft/{sounds.json,lang,font,textures/font}
 *   <profile_dir>/assets_index.json
 *
 * Outputs into <out_dir>: items/entities/models/recipes/loot_tables/tags/
 * worldgen/sounds/lang/font/biomes/coverage_report and assets_index.json.
 * It also augments blocks.json with official state ids. Every output is
 * sorted and timestamp-free so two runs are byte-identical.
 */
struct V3Summary {
  uint64_t items = 0;
  uint64_t entities = 0;
  uint64_t native_entities = 0;
  uint64_t models = 0;
  uint64_t block_states = 0;
  uint64_t recipes = 0;
  uint64_t recipes_supported = 0;
  uint64_t loot_tables = 0;
  uint64_t loot_unsupported = 0;
  uint64_t tags = 0;
  uint64_t worldgen = 0;
  uint64_t sounds = 0;
  uint64_t lang_entries = 0;
  uint64_t fonts = 0;
  Json coverage;
};

bool augment_generated_v3(const std::filesystem::path& out_dir,
                          const std::string& profile,
                          const std::filesystem::path& profile_dir,
                          V3Summary& summary, std::string& error);

} // namespace reim::conv
