#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

int run_analyze(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
