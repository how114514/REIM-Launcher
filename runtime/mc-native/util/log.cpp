#include "util/log.h"

#include <cstdio>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace reim::log {
namespace {

std::mutex& sink_mutex() {
  static std::mutex m;
  return m;
}

Sink& sink_storage() {
  static Sink sink = [](Level level, std::string_view category, std::string_view message) {
    std::fprintf(stderr, "[%d] %.*s: %.*s\n", static_cast<int>(level),
                 static_cast<int>(category.size()), category.data(),
                 static_cast<int>(message.size()), message.data());
  };
  return sink;
}

} // namespace

void set_sink(Sink sink) {
  std::lock_guard<std::mutex> lock(sink_mutex());
  sink_storage() = std::move(sink);
}

Sink get_sink() {
  std::lock_guard<std::mutex> lock(sink_mutex());
  return sink_storage();
}

void write(Level level, std::string_view category, std::string_view message) {
  Sink sink;
  {
    std::lock_guard<std::mutex> lock(sink_mutex());
    sink = sink_storage();
  }
  if (sink) {
    sink(level, category, message);
  }
}

} // namespace reim::log
