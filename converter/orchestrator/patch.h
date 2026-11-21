#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

/* M3-L4: versioned, replayable Patch sets with JSON three-way merge.
 *
 *   patch apply   --set <dir> --target <dir> [--dry-run] [--out report.json]
 *   patch verify  --set <dir> [--out report.json]
 *   patch selftest [--out report.json]
 *
 * Conflicts fail loudly and never overwrite the target. Applying a set twice
 * is a no-op (entry already applied). */
int run_patch(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
