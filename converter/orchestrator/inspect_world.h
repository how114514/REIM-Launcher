#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

/* M1-C / C6: reads a real world (read-only) and writes a deterministic JSON
 * inspection report: level metadata, per-dimension chunk statistics, block
 * samples and skipped/damaged chunks. */
int run_inspect_world(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
