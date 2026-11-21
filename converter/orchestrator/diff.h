#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

/* M3-L2/L5/L6: deterministic data/asset diff between two generated dirs.
 *
 * Outputs <out>/diff.json plus <out>/diff.md. Every changed subsystem gets a
 * work item with an owner stage; optional Java analysis dirs add class/method
 * changes. No timestamps or absolute input paths enter the report. */
int run_diff(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
