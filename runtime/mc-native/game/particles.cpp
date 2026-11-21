#include "game/particles.h"

#include <algorithm>
#include <cmath>

namespace reim::game {
namespace {

constexpr size_t kMaxParticles = 6000;

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}

} // namespace

uint32_t ParticleSystem::next_random() {
  uint32_t x = rng_state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state_ = x;
  return x;
}

void ParticleSystem::clear() { particles_.clear(); }

void ParticleSystem::add(Particle particle) {
  if (particles_.size() >= kMaxParticles) {
    particles_.erase(particles_.begin(),
                     particles_.begin() + static_cast<ptrdiff_t>(
                                               particles_.size() - kMaxParticles + 1));
  }
  particles_.push_back(particle);
  ++emitted_;
}

void ParticleSystem::emit_block_break(double x, double y, double z,
                                      uint32_t color, int count) {
  for (int i = 0; i < count; ++i) {
    Particle particle;
    particle.type = static_cast<uint32_t>(ParticleType::block_break);
    particle.x = x + (static_cast<double>(next_random() % 1000) / 1000.0 - 0.5) * 0.7;
    particle.y = y + (static_cast<double>(next_random() % 1000) / 1000.0 - 0.5) * 0.7;
    particle.z = z + (static_cast<double>(next_random() % 1000) / 1000.0 - 0.5) * 0.7;
    particle.prev_x = particle.x;
    particle.prev_y = particle.y;
    particle.prev_z = particle.z;
    particle.vx = (static_cast<double>(next_random() % 1000) / 1000.0 - 0.5) * 0.12;
    particle.vy = 0.08 + static_cast<double>(next_random() % 1000) / 1000.0 * 0.12;
    particle.vz = (static_cast<double>(next_random() % 1000) / 1000.0 - 0.5) * 0.12;
    particle.size = 0.08f + static_cast<float>(next_random() % 100) / 1000.0f;
    particle.lifetime = 18.0f + static_cast<float>(next_random() % 20);
    particle.color = color;
    add(particle);
  }
}

void ParticleSystem::emit_explosion(double x, double y, double z) {
  for (int i = 0; i < 40; ++i) {
    Particle particle;
    particle.type = static_cast<uint32_t>(ParticleType::explosion);
    const double angle = static_cast<double>(next_random() % 6283) / 1000.0;
    const double speed = 0.05 + static_cast<double>(next_random() % 1000) / 4000.0;
    particle.x = particle.prev_x = x;
    particle.y = particle.prev_y = y;
    particle.z = particle.prev_z = z;
    particle.vx = std::cos(angle) * speed;
    particle.vy = std::sin(angle) * speed * 0.6 + 0.05;
    particle.vz = std::sin(angle * 1.7) * speed;
    particle.size = 0.2f + static_cast<float>(next_random() % 200) / 1000.0f;
    particle.lifetime = 20.0f + static_cast<float>(next_random() % 30);
    particle.color = (next_random() % 3 == 0) ? rgba(255, 240, 180) : rgba(255, 170, 60);
    add(particle);
  }
}

void ParticleSystem::emit_weather(const core::Player& player, uint32_t weather) {
  if (weather == 0) {
    weather_accum_ = 0;
    return;
  }
  const bool thunder = weather == 2;
  /* 6 rain columns per tick around the player; 2D-looking billboards fall to
   * roughly eye level. Snow is not a separate M3 weather state. */
  weather_accum_ += thunder ? 10 : 6;
  for (uint32_t i = 0; i < weather_accum_ / 6; ++i) {
    Particle particle;
    particle.type = static_cast<uint32_t>(ParticleType::rain);
    const double angle = static_cast<double>(next_random() % 6283) / 1000.0;
    const double radius = 2.0 + static_cast<double>(next_random() % 600) / 100.0;
    particle.x = particle.prev_x = player.x + std::cos(angle) * radius;
    particle.z = particle.prev_z = player.z + std::sin(angle) * radius;
    particle.y = particle.prev_y = player.y + 7.0 + static_cast<double>(next_random() % 30) / 10.0;
    particle.vx = 0.0;
    particle.vy = -0.7;
    particle.vz = 0.0;
    particle.size = 0.06f;
    particle.lifetime = 40.0f;
    particle.color = thunder ? rgba(180, 190, 255, 200) : rgba(150, 170, 255, 190);
    particle.flags = 0x0002;
    add(particle);
  }
  weather_accum_ %= 6;
}

void ParticleSystem::tick() {
  for (Particle& particle : particles_) {
    particle.prev_x = particle.x;
    particle.prev_y = particle.y;
    particle.prev_z = particle.z;
    particle.vy -= particle.type == static_cast<uint32_t>(ParticleType::rain) ? 0.0 : 0.04;
    particle.vx *= 0.98;
    particle.vz *= 0.98;
    particle.x += particle.vx;
    particle.y += particle.vy;
    particle.z += particle.vz;
    particle.age += 1.0f;
  }
  particles_.erase(
      std::remove_if(particles_.begin(), particles_.end(),
                     [](const Particle& particle) {
                       return particle.age >= particle.lifetime ||
                              particle.y < -32.0;
                     }),
      particles_.end());
}

} // namespace reim::game
