#include "analyze.h"

#include "prepare.h"
#include "util/file.h"
#include "util/utf.h"

#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <process.h>
#include <stdlib.h>
#include <windows.h>
#endif

namespace reim::conv {
namespace {

std::filesystem::path java_from_environment() {
#ifdef _WIN32
  const wchar_t* home = _wgetenv(L"JAVA_HOME");
  if (home && home[0]) {
    const auto candidate = std::filesystem::path(home) / "bin" / "java.exe";
    if (file::exists(candidate)) return candidate;
  }
#else
  const char* home = std::getenv("JAVA_HOME");
  if (home && home[0]) {
    const auto candidate = std::filesystem::path(home) / "bin" / "java";
    if (file::exists(candidate)) return candidate;
  }
#endif
  return {};
}

std::wstring quote_arg(std::wstring_view value) {
  std::wstring out;
  out.reserve(value.size() + 2);
  out.push_back(L'"');
  int backslashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(static_cast<size_t>(backslashes * 2 + 1), L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(static_cast<size_t>(backslashes), L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(static_cast<size_t>(backslashes * 2), L'\\');
  out.push_back(L'"');
  return out;
}

bool run_process(const std::wstring& application, const std::wstring& command_line,
                 int& exit_code, std::string& error) {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring mutable_command = command_line;
  const wchar_t* app = application.empty() ? nullptr : application.c_str();
  if (!::CreateProcessW(app, mutable_command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &process)) {
    error = "CreateProcessW failed (" + std::to_string(::GetLastError()) + ")";
    return false;
  }
  ::WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  ::GetExitCodeProcess(process.hProcess, &code);
  ::CloseHandle(process.hThread);
  ::CloseHandle(process.hProcess);
  exit_code = static_cast<int>(code);
  return true;
}

bool has_file(const std::filesystem::path& dir, const char* name, uint64_t& size) {
  const auto path = dir / name;
  if (!file::exists(path)) return false;
  std::error_code ec;
  const auto value = std::filesystem::file_size(path, ec);
  size = ec ? 0 : static_cast<uint64_t>(value);
  return true;
}

} // namespace

int run_analyze(const Args& args, const std::filesystem::path& exe_dir) {
  const std::string profile = args.get("profile").value_or("26.3");
  const auto out_arg = args.get("out");
  if (!out_arg) {
    log_error("analyze requires --profile 26.3 --out <work/26.3>");
    return 2;
  }
  const auto out_dir = file::from_utf8(*out_arg);
  std::string error;
  if (!file::ensure_directory(out_dir, error)) {
    log_error(error);
    return 3;
  }

  std::filesystem::path client_jar;
  if (const auto jar = args.get("jar"); jar && !jar->empty()) {
    client_jar = file::from_utf8(*jar);
  } else {
    InputManifest manifest;
    if (!load_input_manifest(out_dir, manifest, error)) {
      log_error("analyze: " + error);
      return 4;
    }
    client_jar = manifest.client_jar;
    if (!manifest.profile.empty() && manifest.profile != profile) {
      log_warn("analyze: input manifest profile '" + manifest.profile +
               "' differs from requested '" + profile + "'");
    }
  }
  if (!file::exists(client_jar)) {
    log_error("analyze: client JAR not found: " + file::to_utf8(client_jar));
    return 5;
  }

  std::filesystem::path analyzer = args.get("analyzer").value_or("");
  if (analyzer.empty()) analyzer = exe_dir / "reim-analyze.jar";
  if (!file::exists(analyzer)) {
    log_error("analyze: reim-analyze.jar not found at " + file::to_utf8(analyzer) +
              " (build target: reim_analyze_jar)");
    return 6;
  }

  std::filesystem::path java;
  if (const auto explicit_java = args.get("java"); explicit_java && !explicit_java->empty()) {
    java = file::from_utf8(*explicit_java);
  } else {
    java = java_from_environment();
  }

#ifdef _WIN32
  log_info("analyze: " + file::to_utf8(analyzer) + " over " +
           file::to_utf8(client_jar));
  log_info("analyze: offline JDK command (Converter only; never the Runtime)");
  const std::wstring java_arg = java.empty() ? L"java" : java.wstring();
  std::wstring command_line = quote_arg(java_arg) +
      L" -jar " + quote_arg(analyzer.wstring()) +
      L" --jar " + quote_arg(client_jar.wstring()) +
      L" --out " + quote_arg(out_dir.wstring()) +
      L" --profile " + quote_arg(utf::utf8_to_wide(profile));
  int result = 0;
  std::string process_error;
  if (!run_process(java.empty() ? std::wstring() : java_arg, command_line, result,
                   process_error)) {
    log_error("analyze: " + process_error);
    return 7;
  }
  if (result != 0) {
    log_error("analyze: reim-analyze.jar failed with exit code " +
              std::to_string(result) +
              " (install JDK 25 or pass --java <java.exe>)");
    return 7;
  }
#else
  const std::string java_cmd = java.empty() ? "java" : "\"" + file::to_utf8(java) + "\"";
  const std::string command = java_cmd + " -jar \"" + file::to_utf8(analyzer) +
                              "\" --jar \"" + file::to_utf8(client_jar) +
                              "\" --out \"" + file::to_utf8(out_dir) +
                              "\" --profile \"" + profile + "\"";
  const int result = std::system(command.c_str());
  if (result != 0) {
    log_error("analyze: reim-analyze.jar failed with exit code " +
              std::to_string(result));
    return 7;
  }
#endif

  const auto analysis_dir = out_dir / "analysis";
  uint64_t classes_size = 0;
  uint64_t deps_size = 0;
  if (!has_file(analysis_dir, "classes.json", classes_size) ||
      !has_file(analysis_dir, "deps.json", deps_size)) {
    log_error("analyze: analyzer did not produce analysis/classes.json and deps.json");
    return 8;
  }
  log_info(std::format("analyze: analysis/classes.json {} bytes, analysis/deps.json {} bytes",
                       classes_size, deps_size));
  return 0;
}

} // namespace reim::conv
