#pragma once

#include "data/block_table.h"
#include "world/chunk.h"

#include <cstdint>
#include <string>

namespace reim::game {

/* Time/weather snapshot exposed by the world layer (M3-G7). Entity spawning
 * and daylight burning read it; it never mutates gameplay state. */
struct EnvironmentState {
  uint64_t time_of_day = 0;  /* absolute world ticks */
  uint32_t day = 0;
  uint32_t weather = 0;      /* ReimWeather */
  bool daylight = true;
  float sky_darken = 0.0f;
  uint32_t dimension = 0;    /* ReimDimension */
};

/*
 * The gameplay layer never owns world storage. It queries and edits the loaded
 * world through this interface, which the simulation thread implements. The
 * interface is deliberately narrow: reads return immutable block ids/states,
 * the single write path goes through set_block (which remeshes), and no
 * gameplay code sees chunk NBT.
 */
class WorldAccess {
 public:
  virtual ~WorldAccess() = default;

  virtual const data::BlockTable& blocks() const = 0;
  virtual uint32_t block_id_at(int x, int y, int z) const = 0;
  /* Copies the palette state (name + properties) at a position. false when the
   * position is outside a loaded chunk. */
  virtual bool block_state_at(int x, int y, int z, world::BlockState& out) const = 0;
  /* Replaces one block and schedules a remesh. Creates the section when the
   * chunk is loaded but the section is absent (air). */
  virtual bool set_block(int x, int y, int z, const world::BlockState& state,
                         std::string& error) = 0;
  virtual bool is_loaded(int x, int z) const = 0;
  virtual bool collides(int x, int y, int z) const = 0;

  /* Optional world-level facts used by survival/entities. The defaults keep
   * small test doubles compiling; the Runtime simulation overrides both. */
  virtual EnvironmentState environment() const { return {}; }
  virtual std::string biome_at(int x, int y, int z) const {
    (void)x;
    (void)y;
    (void)z;
    return {};
  }
};

} // namespace reim::game
