#include "world/level.h"

#include "nbt/nbt.h"
#include "util/file.h"

#include <optional>

namespace reim::world {
namespace {

std::optional<int64_t> int_of(const nbt::Value& value) { return nbt::as_int(value); }
std::optional<double> number_of(const nbt::Value& value) {
  return nbt::as_number(value);
}

/* 26.3 stores the spawn position as a TAG_Int_Array; older data and player
 * state use a TAG_List of doubles. Accept both. */
bool read_position(const nbt::Value& pos, double& x, double& y, double& z) {
  if (pos.type == nbt::Tag::int_array && pos.ints.size() >= 3) {
    x = pos.ints[0];
    y = pos.ints[1];
    z = pos.ints[2];
    return true;
  }
  if (pos.type == nbt::Tag::list && pos.list && pos.list->items.size() >= 3) {
    const auto px = number_of(pos.list->items[0]);
    const auto py = number_of(pos.list->items[1]);
    const auto pz = number_of(pos.list->items[2]);
    if (!px || !py || !pz) return false;
    x = *px;
    y = *py;
    z = *pz;
    return true;
  }
  return false;
}

bool read_spawn(const nbt::Value& spawn, SpawnPoint& out) {
  const nbt::Value* pos = nbt::find(spawn, "pos");
  if (pos == nullptr) return false;
  if (!read_position(*pos, out.x, out.y, out.z)) return false;
  if (const nbt::Value* yaw = nbt::find(spawn, "yaw")) {
    if (const auto value = number_of(*yaw)) out.yaw = static_cast<float>(*value);
  }
  if (const nbt::Value* pitch = nbt::find(spawn, "pitch")) {
    if (const auto value = number_of(*pitch)) out.pitch = static_cast<float>(*value);
  }
  if (const nbt::Value* dimension = nbt::find(spawn, "dimension")) {
    if (const auto* text = nbt::as_string(*dimension)) out.dimension = *text;
  }
  out.present = true;
  return true;
}

bool read_player(const nbt::Value& player, PlayerState& out) {
  const nbt::Value* pos = nbt::find(player, "Pos");
  if (pos == nullptr) return false;
  if (!read_position(*pos, out.x, out.y, out.z)) return false;
  if (const nbt::Value* rotation = nbt::find(player, "Rotation")) {
    if (rotation->type == nbt::Tag::list && rotation->list &&
        rotation->list->items.size() >= 2) {
      if (const auto yaw = number_of(rotation->list->items[0])) {
        out.yaw = static_cast<float>(*yaw);
      }
      if (const auto pitch = number_of(rotation->list->items[1])) {
        out.pitch = static_cast<float>(*pitch);
      }
    }
  }
  if (const nbt::Value* dimension = nbt::find(player, "Dimension")) {
    if (const auto* text = nbt::as_string(*dimension)) out.dimension = *text;
  }
  out.present = true;
  return true;
}

} // namespace

bool load_level(const std::filesystem::path& world_dir, LevelInfo& out,
                std::string& error) {
  out = LevelInfo{};
  const std::filesystem::path path = world_dir / "level.dat";
  if (!file::exists(path)) {
    error = "level.dat not found in " + file::to_utf8(world_dir);
    return false;
  }
  nbt::Document document;
  if (!nbt::parse_file(path, document, error)) return false;
  if (document.root.type != nbt::Tag::compound || !document.root.compound) {
    error = "level.dat root is not a compound";
    return false;
  }
  const nbt::Value& root = document.root;
  const nbt::Value* data = nbt::find(root, "Data");
  const nbt::Value& state =
      (data != nullptr && data->type == nbt::Tag::compound) ? *data : root;

  if (const nbt::Value* root_version = nbt::find(root, "DataVersion")) {
    if (const auto number = int_of(*root_version)) {
      out.data_version = static_cast<int>(*number);
    }
  } else if (const nbt::Value* state_version = nbt::find(state, "DataVersion")) {
    if (const auto number = int_of(*state_version)) {
      out.data_version = static_cast<int>(*number);
    }
  }

  if (const nbt::Value* version = nbt::find(state, "Version")) {
    if (version->type == nbt::Tag::compound) {
      if (const nbt::Value* name = nbt::find(*version, "Name")) {
        if (const auto* text = nbt::as_string(*name)) {
          out.version_name = *text;
          out.has_version = true;
        }
      }
      if (const nbt::Value* id = nbt::find(*version, "Id")) {
        if (const auto number = int_of(*id)) {
          out.version_id = static_cast<int>(*number);
          out.has_version = true;
        }
      }
    }
  }
  if (const nbt::Value* version = nbt::find(state, "version")) {
    if (const auto number = int_of(*version)) {
      out.level_version = static_cast<int>(*number);
    }
  }
  if (const nbt::Value* history = nbt::find(state, "version_history")) {
    if (history->type == nbt::Tag::list && history->list) {
      for (const nbt::Value& item : history->list->items) {
        if (const auto number = int_of(item)) {
          out.version_history.push_back(static_cast<int>(*number));
        }
      }
    }
  }
  if (const nbt::Value* name = nbt::find(state, "LevelName")) {
    if (const auto* text = nbt::as_string(*name)) out.level_name = *text;
  }
  if (const nbt::Value* spawn = nbt::find(state, "spawn")) {
    if (spawn->type == nbt::Tag::compound) read_spawn(*spawn, out.spawn);
  }
  if (const nbt::Value* player = nbt::find(state, "Player")) {
    if (player->type == nbt::Tag::compound) read_player(*player, out.player);
  }
  return true;
}

} // namespace reim::world
