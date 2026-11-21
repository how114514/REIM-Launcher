#pragma once

#include "core/player.h"
#include "game/entity.h"
#include "game/inventory.h"
#include "game/item.h"
#include "game/particles.h"
#include "game/recipes.h"
#include "game/survival.h"
#include "game/world_access.h"
#include "reim_abi.h"
#include "reim_game.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reim::game {

/* One raycast hit; positions are world blocks and hit_* is the exact pick
 * point in blocks. */
struct Hit {
  bool valid = false;
  int x = 0;
  int y = 0;
  int z = 0;
  uint32_t block_id = 0;
  std::string block_name;
  uint32_t face = REIM_FACE_UP;
  float hit_x = 0.0f;
  float hit_y = 0.0f;
  float hit_z = 0.0f;
  float distance = 0.0f;
};

struct InteractionState {
  uint32_t target_type = REIM_INTERACTION_NONE;
  int x = 0;
  int y = 0;
  int z = 0;
  uint32_t block_id = 0;
  std::string block_name;
  uint32_t face = REIM_FACE_UP;
  float hit_x = 0.0f;
  float hit_y = 0.0f;
  float hit_z = 0.0f;
  bool can_break = false;
  bool can_place = false;
  float break_progress = 0.0f;
  float reach_distance = 4.5f;
  float attack_cooldown = 1.0f;
  uint64_t entity_id = 0;
  uint32_t entity_type = 0;
};

struct UiSlot {
  uint32_t slot = 0;
  ItemStack item;
};

struct FurnaceView {
  bool valid = false;
  int x = 0;
  int y = 0;
  int z = 0;
  ItemStack input;
  ItemStack fuel;
  ItemStack output;
  int burn_time = 0;
  int burn_total = 0;
  int cook_time = 0;
};

struct ChestView {
  bool valid = false;
  int x = 0;
  int y = 0;
  int z = 0;
  std::array<ItemStack, kChestSlots> slots{};
};

struct Probe {
  int x = 0;
  int y = 0;
  int z = 0;
  std::string block;
  std::string biome;
};

/* Immutable per-tick view consumed by the UI/status builders. Everything in
 * here is a copy; the simulation thread keeps ticking while the Host renders. */
struct GameView {
  uint32_t screen = REIM_SCREEN_HUD;
  uint32_t screen_revision = 1;
  uint32_t selected_slot = 0;
  ItemStack held;
  ItemStack cursor;
  std::vector<UiSlot> slots;         /* screen-relative */
  std::map<std::string, int> item_counts;
  InteractionState interaction;
  int hovered_slot = -1;             /* screen-relative, -1 = none */
  uint32_t hovered_element = 0;
  std::string hovered_tooltip;
  float pointer_x = 0.0f;            /* logical UI pixels */
  float pointer_y = 0.0f;
  uint32_t canvas_width = 1280;
  uint32_t canvas_height = 720;
  uint32_t ui_scale = 2;
  bool pointer_visible = false;
  int grid_size = 2;
  std::array<ItemStack, kCraftGridSlots> grid{};
  ItemStack craft_result;
  std::vector<DroppedItem> drops;
  FurnaceView furnace;
  ChestView chest;
  std::vector<Probe> probes;
  std::vector<EntityView> entities;
  std::vector<EntitySoundEvent> entity_sounds;
  float food = 20.0f;
  float health = 20.0f;
  float max_health = 20.0f;
  float saturation = 5.0f;
  float exhaustion = 0.0f;
  float air = 300.0f;
  float max_air = 300.0f;
  float xp_progress = 0.0f;
  uint32_t xp_level = 0;
  uint32_t xp_total = 0;
  bool dead = false;
  bool on_fire = false;
  uint32_t deaths = 0;
  float damage_taken = 0.0f;
  std::string damage_cause;
  bool break_held = false;
  bool use_held = false;
  uint32_t use_ticks = 0;
  uint64_t blocks_broken = 0;
  uint64_t blocks_placed = 0;
  uint64_t items_dropped = 0;
  uint64_t items_picked = 0;
  uint64_t crafts_taken = 0;
  uint64_t smelts_completed = 0;
  uint64_t mobs_killed = 0;
  uint64_t mobs_spawned = 0;
};

/*
 * Runtime gameplay authority for M3-D/E (M3-D01..D09). It owns the inventory,
 * screens, containers, crafting/furnace ticks and dropped items; the world is
 * read and edited only through WorldAccess. Hosts never call into this type:
 * they submit Contract actions/input and consume the published view.
 */
class Gameplay {
 public:
  bool initialize(WorldAccess& world, const std::string& generated_dir,
                  std::string& error);

  /* Gamemode/difficulty and the entity spawn seed come from the instance
   * config (Host reads instance.json; Runtime never reads Host files). */
  void set_rules(uint32_t gamemode, uint32_t difficulty, uint64_t seed);

  /* Parses the diagnostic scenario config (setup/probes). Called before the
   * world is loaded; apply_setup() runs after initial_load. */
  bool configure_scenario(const std::string& scenario_json, std::string& error);
  void apply_setup(const core::Player& player);
  bool scenario_active() const { return scenario_active_; }

  void set_canvas(uint32_t width, uint32_t height, uint32_t scale);

  /* UI/menu keys are Runtime-owned (M3_PLAN 8.10); Hosts only forward them. */
  void on_key(int32_t key, bool pressed, const core::Player& player);

  /* Applies one validated action. Returns false when the intent is illegal
   * (screen mismatch, empty target, invalid slot, ...). */
  bool handle_action(const ReimActionEvent& event, core::Player& player,
                     std::string& error);

  /* Advances survival, entities, combat, drops and containers. The player is
   * mutable because knockback, fall damage and respawn are Runtime-authoritative
   * (M3-D01/M3-F). */
  void tick(core::Player& player);

  GameView view(const core::Player& player) const;

  const SurvivalState& survival() const { return survival_; }
  const EntityManager& entity_manager() const { return entities_; }
  size_t entity_count() const { return entities_.size(); }
  std::map<std::string, int> mob_counts() const { return entities_.counts(); }

  /* Nether portal requests are consumed by the simulation after the gameplay
   * tick; -1 = none, otherwise a ReimDimension id. */
  int take_dimension_request();
  /* Runtime-side reset after a successful dimension change. */
  void on_dimension_changed();

  /* Pointer state is sent by the Host every frame when a modal screen is open
   * (or for HUD hotbar hover). Coordinates are physical frame pixels. */
  void pointer(float screen_x, float screen_y, uint32_t buttons);
  void release_pointer(uint32_t buttons);

  uint32_t screen() const { return screen_; }
  uint32_t screen_revision() const { return screen_revision_; }
  int hovered_slot() const { return hovered_slot_; }
  uint32_t selected_slot() const { return static_cast<uint32_t>(inventory_.selected); }
  const Inventory& inventory() const { return inventory_; }
  const ItemTable& items() const { return items_; }
  std::string item_name(uint32_t item_id) const { return items_.item_name(item_id); }
  std::string item_display(uint32_t item_id) const {
    return items_.display_name(item_id);
  }
  bool tooltip_for_slot(uint32_t screen, int slot, std::string& out) const;

  /* Debug/status counters. */
  uint64_t blocks_broken() const { return blocks_broken_; }
  uint64_t blocks_placed() const { return blocks_placed_; }
  const std::vector<Particle>& particles() const { return particles_.particles(); }
  uint64_t items_dropped() const { return items_dropped_; }
  uint64_t items_picked() const { return items_picked_; }
  uint64_t crafts_taken() const { return crafts_taken_; }
  uint64_t smelts_completed() const { return smelts_completed_; }
  uint64_t mobs_killed() const { return mobs_killed_; }
  uint64_t mobs_spawned() const { return mobs_spawned_; }
  size_t crafting_recipe_count() const { return recipes_.crafting_count(); }
  size_t smelting_recipe_count() const { return recipes_.smelting_count(); }
  bool has_item(std::string_view name) const { return items_.find(name) != 0; }

  Hit raycast(const core::Player& player) const;

 private:
  struct ScenarioSetupOp {
    enum class Kind {
      give,
      set_block,
      platform,
      clear,
      set_health,
      set_food,
      set_air,
      set_xp,
      damage_player,
      spawn_entity,
      clear_entities,
      natural_spawn,
      keep_inventory
    } kind = Kind::give;
    std::string name;
    int count = 1;
    int x = 0;
    int y = 0;
    int z = 0;
    float value = 0.0f;
  };

  ItemStack* slot_ref(uint32_t screen, int slot);
  const ItemStack* slot_ref(uint32_t screen, int slot) const;
  bool valid_slot(uint32_t screen, int slot) const;
  bool is_result_slot(uint32_t screen, int slot) const;
  bool is_output_only(uint32_t screen, int slot) const;
  void slot_numbers(uint32_t screen, std::vector<int>& out) const;

  bool click_slot(int slot, int button, uint32_t mods, std::string& error);
  bool quick_move(int slot, std::string& error);
  bool quick_craft_all(std::string& error);
  void left_click(ItemStack& target, bool output_only);
  void right_click(ItemStack& target, bool output_only);
  void recompute_grid();
  bool move_between(ItemStack& source, ItemStack* targets, size_t count);

  void open_inventory();
  void open_crafting_table();
  bool open_container(const Hit& hit, std::string& error);
  void open_pause();
  void close_screen(const core::Player& player);
  void return_grid_to_player(const core::Player& player);

  BlockEntity* container_at(const BlockPos& pos);
  BlockEntity& ensure_container(const BlockPos& pos, ContainerKind kind);
  void tick_containers();
  void drop_container_contents(const BlockPos& pos);
  bool remove_block_and_drop(const Hit& hit, const core::Player& player,
                             std::string& error);
  bool break_block(const Hit& hit, const core::Player& player, std::string& error);
  bool place_block(const Hit& hit, const core::Player& player, std::string& error);
  bool use_once(const core::Player& player, std::string& error);
  bool drop_from_selected(const core::Player& player, bool whole);
  void spawn_drop(ItemStack stack, double x, double y, double z, double vx,
                  double vy, double vz, int pickup_delay);
  void tick_drops(const core::Player& player);
  void damage_held(int amount);
  void set_target_from_hit(const Hit& hit);
  bool resolve_hit(const ReimActionEvent& event, const core::Player& player, Hit& out,
                   std::string& error) const;

  /* M3-F survival/entity hooks. */
  void apply_entity_events(EntityEvents& events, core::Player& player);
  void tick_survival(core::Player& player);
  void finish_death(const core::Player& player);
  void drop_inventory_on_death(const core::Player& player);
  void do_respawn(core::Player& player);
  SurvivalContext survival_context(const core::Player& player) const;
  void apply_player_damage(core::Player& player, float amount, DamageCause cause);
  void emit_sound(uint32_t id, double x, double y, double z, float volume,
                  float pitch);
  bool attack_entity(const EntityHit& hit, core::Player& player,
                     std::string& error);

  WorldAccess* world_ = nullptr;
  ItemTable items_;
  RecipeBook recipes_;
  Inventory inventory_;
  CraftingGrid grid_;
  ItemStack craft_result_;
  ItemStack cursor_;
  std::map<BlockPos, BlockEntity> containers_;
  std::vector<DroppedItem> drops_;
  std::vector<ScenarioSetupOp> setup_ops_;
  std::vector<BlockPos> probe_positions_;
  std::vector<EntitySoundEvent> tick_sounds_;
  std::vector<EntitySoundEvent> pending_sounds_;
  EntityManager entities_;
  ParticleSystem particles_;
  SurvivalState survival_;
  core::Player spawn_point_{};
  bool has_spawn_point_ = false;
  bool want_respawn_ = false;
  int dimension_request_ = -1;
  uint32_t gamemode_ = 0;
  uint32_t difficulty_ = 2;
  uint64_t entity_seed_ = 0xC0FFEEull;
  bool keep_inventory_ = false;
  bool break_held_saved_ = false;
  uint64_t next_drop_id_ = 1;
  uint64_t rng_ = 0x9E3779B97F4A7C15ull;

  uint32_t screen_ = REIM_SCREEN_HUD;
  uint32_t screen_revision_ = 1;
  BlockPos active_container_{};
  bool has_active_container_ = false;
  bool scenario_active_ = false;

  bool break_held_ = false;
  bool use_held_ = false;
  uint32_t use_ticks_ = 0;
  bool dragging_ = false;
  int drag_source_ = -1;
  bool use_consumed_ = false;
  Hit break_target_;
  int break_total_ticks_ = 0;
  float break_progress_ = 0.0f;
  Hit target_;
  EntityHit target_entity_;
  float attack_cooldown_ = 1.0f;

  /* Pointer state (physical pixels as sent by the Host). */
  float raw_pointer_x_ = 0.0f;
  float raw_pointer_y_ = 0.0f;
  uint32_t pointer_buttons_ = 0;
  float pointer_x_ = 0.0f;
  float pointer_y_ = 0.0f;
  int hovered_slot_ = -1;
  uint32_t hovered_element_ = 0;

  uint32_t canvas_width_ = 1280;
  uint32_t canvas_height_ = 720;
  uint32_t ui_scale_ = 2;

  uint64_t blocks_broken_ = 0;
  uint64_t blocks_placed_ = 0;
  uint64_t items_dropped_ = 0;
  uint64_t items_picked_ = 0;
  uint64_t crafts_taken_ = 0;
  uint64_t smelts_completed_ = 0;
  uint64_t mobs_killed_ = 0;
  uint64_t mobs_spawned_ = 0;
  uint64_t tick_ = 0;
  double last_step_x_ = 0.0;
  double last_step_z_ = 0.0;
  int step_timer_ = 0;
};

} // namespace reim::game
