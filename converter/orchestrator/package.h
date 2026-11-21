#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

int run_package(const Args& args, const std::filesystem::path& exe_dir);
int run_assemble(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
