#include "util/hash.h"

#include <cstring>

namespace reim::hash {
namespace {

constexpr uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

constexpr uint32_t rotl(uint32_t x, uint32_t n) {
  return (x << n) | (x >> (32 - n));
}

void sha256_block(uint32_t state[8], const uint8_t block[64]) {
  static constexpr uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void sha1_block(uint32_t state[5], const uint8_t block[64]) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f = 0, k = 0;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = temp;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

template <typename State, typename BlockFn, size_t N>
std::array<uint8_t, N> digest(const State& initial, BlockFn block_fn,
                              const uint8_t* data, size_t size, bool little_endian_len) {
  State state = initial;
  uint8_t buffer[64];
  size_t offset = 0;
  while (size - offset >= 64) {
    block_fn(state.data(), data + offset);
    offset += 64;
  }
  const size_t remaining = size - offset;
  std::memset(buffer, 0, sizeof(buffer));
  if (remaining) std::memcpy(buffer, data + offset, remaining);
  buffer[remaining] = 0x80;
  if (remaining >= 56) {
    block_fn(state.data(), buffer);
    std::memset(buffer, 0, sizeof(buffer));
  }
  const uint64_t bits = static_cast<uint64_t>(size) * 8ull;
  if (little_endian_len) {
    for (int i = 0; i < 8; ++i) {
      buffer[63 - i] = static_cast<uint8_t>(bits >> (8 * i));
    }
  } else {
    for (int i = 0; i < 8; ++i) {
      buffer[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    }
  }
  block_fn(state.data(), buffer);
  std::array<uint8_t, N> out{};
  for (size_t i = 0; i < state.size(); ++i) {
    out[i * 4 + 0] = static_cast<uint8_t>(state[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
  }
  return out;
}

} // namespace

Digest32 sha256(const uint8_t* data, size_t size) {
  const std::array<uint32_t, 8> initial = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  return digest<std::array<uint32_t, 8>, decltype(&sha256_block), 32>(
      initial, &sha256_block, data, size, false);
}

Digest32 sha256(std::string_view text) {
  return sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

Digest20 sha1(const uint8_t* data, size_t size) {
  const std::array<uint32_t, 5> initial = {
      0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  return digest<std::array<uint32_t, 5>, decltype(&sha1_block), 20>(
      initial, &sha1_block, data, size, false);
}

Digest20 sha1(std::string_view text) {
  return sha1(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::string hex(const Digest32& digest_value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.resize(64);
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = digits[digest_value[i] >> 4];
    out[i * 2 + 1] = digits[digest_value[i] & 0xF];
  }
  return out;
}

std::string hex(const Digest20& digest_value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.resize(40);
  for (size_t i = 0; i < 20; ++i) {
    out[i * 2] = digits[digest_value[i] >> 4];
    out[i * 2 + 1] = digits[digest_value[i] & 0xF];
  }
  return out;
}

} // namespace reim::hash
