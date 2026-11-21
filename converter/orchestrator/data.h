#pragma once

#include "common.h"

#include <filesystem>

namespace reim::conv {

/* M3-K1: offline data orchestration.
 *
 * one command produces:
 *   <out>/reports/reports     official data generator reports (JDK only)
 *   <out>/datapack            server.jar built-in data/minecraft/**
 *   <out>/assets              sounds/lang/font assets selected for M3
 *   <out>/assets_index.json   complete client asset index (paths + hashes)
 *   <out>/data_report.json    counts/digests for extract + coverage
 *
 * It never starts a Minecraft client/server; the data generator is an offline
 * JVM tool used by the Converter only.
 */
int run_data(const Args& args, const std::filesystem::path& exe_dir);

} // namespace reim::conv
