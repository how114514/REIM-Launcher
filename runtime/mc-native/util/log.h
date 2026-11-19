#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace reim::log {

enum class Level : int {
  trace = 0,
  debug = 1,
  info = 2,
  warn = 3,
  error = 4
};

using Sink = std::function<void(Level, std::string_view category, std::string_view message)>;

void set_sink(Sink sink);
Sink get_sink();
void write(Level level, std::string_view category, std::string_view message);

inline void trace(std::string_view c, std::string_view m) { write(Level::trace, c, m); }
inline void debug(std::string_view c, std::string_view m) { write(Level::debug, c, m); }
inline void info(std::string_view c, std::string_view m) { write(Level::info, c, m); }
inline void warn(std::string_view c, std::string_view m) { write(Level::warn, c, m); }
inline void error(std::string_view c, std::string_view m) { write(Level::error, c, m); }

} // namespace reim::log
