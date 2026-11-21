#pragma once

#include "core/player.h"

#include <cstdint>
#include <vector>

namespace reim::game {

/* M3-J5: Runtime particle simulation. Particles are presentation-only; they
 * never feed back into gameplay. Types are stable for the Renderer. */
enum class ParticleType : uint32_t {
  block_break = 1,
  explosion = 2,
  rain = 3,
  snow = 4,
  smoke = 5,
};

struct Particle {
  uint32_t type = 1;
  double x = 0.0, y = 0.0, z = 0.0;
  double prev_x = 0.0, prev_y = 0.0, prev_z = 0.0;
  double vx = 0.0, vy = 0.0, vz = 0.0;
  float size = 0.1f;
  float age = 0.0f;
  float lifetime = 20.0f;
  uint32_t color = 0xFFFFFFFFu; /* RGBA8: low byte red */
  uint16_t texture = 0;
  uint16_t flags = 0;
};

class ParticleSystem {
 public:
  void clear();
  void tick();
  void emit_block_break(double x, double y, double z, uint32_t color, int count);
  void emit_explosion(double x, double y, double z);
  void emit_weather(const core::Player& player, uint32_t weather);

  const std::vector<Particle>& particles() const { return particles_; }
  uint64_t emitted() const { return emitted_; }

 private:
  uint32_t next_random();
  void add(Particle particle);

  std::vector<Particle> particles_;
  uint32_t rng_state_ = 0x9E3779B9u;
  uint32_t weather_accum_ = 0;
  uint64_t emitted_ = 0;
};

} // namespace reim::game
