#include "analyze.h"
#include "common.h"
#include "extract.h"
#include "inspect_world.h"
#include "prepare.h"

#include "data/generated_manifest.h"
#include "util/file.h"
#include "util/log.h"

#include <cstdio>
#include <string>

namespace {

int run_verify(const reim::conv::Args& args) {
  const auto generated = args.get("generated");
  const std::string profile = args.get("profile").value_or("26.3");
  if (!generated) {
    reim::conv::log_error("verify requires --generated generated/26.3 [--profile 26.3]");
    return 2;
  }
  reim::data::GeneratedManifest manifest;
  std::string error;
  if (!reim::data::validate_generated(reim::file::from_utf8(*generated), profile, 1,
                                      manifest, error)) {
    reim::conv::log_error("verify: " + error);
    return 3;
  }
  reim::conv::log_info("verify: manifest OK (profile " + manifest.profile +
                       ", ABI " + std::to_string(manifest.abi_version) + ", " +
                       std::to_string(manifest.files.size()) + " generated files, " +
                       std::to_string(manifest.blocks_total) + " blocks)");
  return 0;
}

void print_usage() {
  std::printf(
      "reim-convert, M1 Converter (S1 + S2)\n"
      "\n"
      "  prepare --minecraft-dir <dir> --version 26.3 --out work/26.3 [--server-jar <jar>]\n"
      "  analyze --profile 26.3 --out work/26.3 [--jar <client.jar>] [--analyzer <jar>] [--java <java.exe>]\n"
      "  extract --profile 26.3 --out generated/26.3 [--profile-dir work/26.3] [--jar <client.jar>]\n"
      "  verify  --generated generated/26.3 [--profile 26.3]\n"
      "  inspect-world --world <save> [--out report.json]\n"
      "  version | help\n"
      "\n"
      "S2 adds read-only NBT/Anvil world inspection (level.dat, chunks, block\n"
      "samples). World data is never modified.\n");
}

} // namespace

int main(int argc, char** argv) {
  reim::log::set_sink([](reim::log::Level level, std::string_view category,
                         std::string_view message) {
    static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    const int index = static_cast<int>(level);
    std::FILE* stream = level >= reim::log::Level::warn ? stderr : stdout;
    std::fprintf(stream, "[%s] %.*s: %.*s\n",
                 names[index < 0 ? 0 : (index > 4 ? 4 : index)],
                 static_cast<int>(category.size()), category.data(),
                 static_cast<int>(message.size()), message.data());
    std::fflush(stream);
  });

  if (argc < 2) {
    print_usage();
    return 1;
  }
  const std::string command = argv[1];
  if (command == "help" || command == "--help" || command == "-h") {
    print_usage();
    return 0;
  }
  if (command == "version" || command == "--version") {
    std::printf("reim-convert 1.0.0-m1\n");
    return 0;
  }

  const auto exe_dir = reim::conv::exe_directory();
  const auto args = reim::conv::parse_args(argc, argv, 2);

  if (command == "prepare") return reim::conv::run_prepare(args, exe_dir);
  if (command == "analyze") return reim::conv::run_analyze(args, exe_dir);
  if (command == "extract") return reim::conv::run_extract(args, exe_dir);
  if (command == "verify") return run_verify(args);
  if (command == "inspect-world") return reim::conv::run_inspect_world(args, exe_dir);

  reim::conv::log_error("unknown command: " + command);
  print_usage();
  return 2;
}
