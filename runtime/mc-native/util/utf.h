#pragma once

#include <string>
#include <string_view>

namespace reim::utf {

std::string wide_to_utf8(std::wstring_view value);
std::wstring utf8_to_wide(std::string_view value);

/* Java "modified UTF-8" as used by NBT: NUL is C0 80 and supplementary
 * characters are encoded as two 3-byte surrogate sequences. */
std::string modified_utf8_to_utf8(const char* data, size_t size);

} // namespace reim::utf
