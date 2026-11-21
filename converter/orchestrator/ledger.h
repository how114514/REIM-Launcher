#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

/* M3-L3: validates converter/ledger/<profile>.json and writes a coverage
 * report (work/<profile>/analysis/port_ledger.json). The ledger maps Java
 * symbols to native modules, generated data or patches; it never claims a
 * translation that does not exist. */
int run_ledger(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
