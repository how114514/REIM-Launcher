#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace reim::hash {

using Digest32 = std::array<uint8_t, 32>;
using Digest20 = std::array<uint8_t, 20>;

Digest32 sha256(const uint8_t* data, size_t size);
Digest32 sha256(std::string_view text);
Digest20 sha1(const uint8_t* data, size_t size);
Digest20 sha1(std::string_view text);

std::string hex(const Digest32& digest);
std::string hex(const Digest20& digest);

} // namespace reim::hash
