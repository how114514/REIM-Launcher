#pragma once

#include "core/simulation.h"
#include "game/gameplay.h"
#include "reim_abi.h"
#include "ui/game_ui.h"
#include "ui/ui_spike.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reim::runtime {

/*
 * ABI instance: owns the fixed-step simulation thread, the double-buffered
 * render snapshot and the host-owned frame blobs.
 *
 * The simulation thread is the only writer to Simulation and to the pending
 * frame state; fetch_frame runs on the Host render thread and copies the
 * pending frame into a single block obtained from ReimHostApi::alloc. The Host
 * returns that block through release_frame (any thread). At most
 * REIM_MAX_PENDING_FRAMES blocks may be outstanding (M2-B2).
 *
 * v3: submit_action queues intents applied on the next tick; UI state and the
 * Contract-spike channels are simulation-thread owned. The action queue is the
 * only cross-thread mutable state besides the published frame state.
 */
class RuntimeInstance final : public core::SimulationSink {
 public:
  static RuntimeInstance* create(const ReimInstanceConfig* config,
                                 const ReimHostApi* host, int32_t* out_error);
  ~RuntimeInstance();

  int32_t start();
  void stop();
  int32_t fetch_frame(ReimRenderFrame* out);
  void release_frame(uint64_t frame_id);
  void submit_input(const ReimInputEvent* event);
  void submit_action(const ReimActionEvent* event);
  void set_paused(bool paused);
  void set_ui_canvas(uint32_t width, uint32_t height, float scale);
  int32_t request_save(uint32_t flags);
  int32_t get_save_status(ReimSaveStatus* out);
  int32_t get_status_json(char* buffer, uint32_t buffer_size);
  int32_t get_runtime_info(ReimRuntimeInfo* out);
  int32_t get_asset(uint32_t kind, uint32_t asset_id, const void** out_data,
                    uint64_t* out_size, uint64_t* out_revision);

  void publish_status(const core::SimStatus& status) override;
  void publish_mesh_events(std::vector<core::MeshEvent>& events) override;

 private:
  struct FrameState {
    core::SimStatus status;
    std::vector<std::shared_ptr<mesh::SectionMeshOutput>> keep;
    std::vector<ReimSectionMeshUpdate> updates;
    std::vector<ReimSectionRemove> removes;
    ui::UiSpike::DrawList ui;
    game::GameView view;
    bool has_view = false;
    std::vector<ReimEntityRender> entities;
    std::vector<std::vector<ReimPartTransform>> entity_parts;
    std::vector<ReimParticle> particles;
    std::vector<ReimAudioEvent> audio;
  };

  struct OutstandingFrame {
    uint64_t frame_id = 0;
    void* block = nullptr;
  };

  RuntimeInstance() = default;

  void run();
  void log_message(int32_t level, const char* category, const std::string& message);
  std::string build_status_json();
  bool load_assets(const std::filesystem::path& generated_dir, std::string& error);
  bool build_contract_assets(std::string& error);
  void free_outstanding_frames();
  size_t outstanding_count();

  void process_actions();
  bool apply_action(const ReimActionEvent& event);
  void update_contract_channels();
  void queue_audio(uint32_t sound_id, uint32_t category, bool is_2d,
                   float volume);

  /* Copied by value: the Host's ReimHostApi struct is caller-scoped (for the UE
   * Bridge it is a stack local of Initialize) and must never be dereferenced
   * after create returns (M2-B6). */
  ReimHostApi host_{};
  /* Points at host_ while this instance is in the DLL-wide log broadcast list. */
  const ReimHostApi* registered_host_ = nullptr;
  core::Simulation simulation_;
  game::Gameplay gameplay_;
  ui::GameUi game_ui_;

  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> started_{false};
  std::atomic<float> partial_tick_{0.0f};

  std::mutex input_mutex_;
  std::vector<core::SimInput> queued_inputs_;

  /* v3 action intents (M3-D04). POINTER is coalesced to its newest state. */
  std::mutex action_mutex_;
  std::deque<ReimActionEvent> queued_actions_;
  ReimActionEvent pending_pointer_{};
  bool has_pending_pointer_ = false;
  bool break_held_ = false;
  bool use_held_ = false;
  std::atomic<uint64_t> pointer_buttons_{0};
  std::atomic<uint64_t> last_action_sequence_{0};
  std::atomic<uint64_t> actions_submitted_{0};
  std::atomic<uint64_t> actions_applied_{0};
  std::atomic<uint64_t> actions_rejected_{0};
  std::atomic<uint64_t> actions_coalesced_{0};
  std::atomic<uint64_t> action_wheel_{0};

  std::mutex frame_mutex_;
  FrameState pending_;
  FrameState published_;
  core::SimStatus latest_status_;
  core::SimStatus prev_status_;
  bool has_prev_status_ = false;
  game::GameView latest_view_;
  bool has_latest_view_ = false;

  std::mutex frames_out_mutex_;
  std::vector<OutstandingFrame> outstanding_;

  /* v2 runtime identity and assets. Immutable after create(). */
  std::string build_id_;
  uint64_t manifest_hash_ = 0;
  uint32_t generated_schema_version_ = 0;
  std::vector<uint8_t> atlas_png_;
  std::string atlas_meta_json_;
  uint64_t atlas_revision_ = 0;

  /* v3 UI/action/asset state (simulation thread except the canvas atomics). */
  ui::UiSpike ui_spike_;
  std::atomic<uint32_t> ui_canvas_width_{1280};
  std::atomic<uint32_t> ui_canvas_height_{720};
  std::atomic<float> ui_scale_{2.0f};
  uint32_t applied_canvas_width_ = 0;
  uint32_t applied_canvas_height_ = 0;
  uint32_t applied_scale_ = 0;
  bool channel_spike_ = false;
  bool ui_spike_enabled_ = false;
  uint32_t gamemode_ = 0;
  uint32_t difficulty_ = 2;
  bool debug_controls_ = false;
  uint64_t channel_tick_ = 0;
  float last_entity_yaw_ = 0.0f;
  bool channel_sound_emitted_ = false;
  std::vector<ReimAudioEvent> tick_audio_;
  std::atomic<uint64_t> audio_sequence_{0};
  /* Published UI status for get_status_json (read from any thread). */
  std::atomic<uint32_t> ui_screen_status_{REIM_SCREEN_HUD};
  std::atomic<uint32_t> ui_revision_status_{1};
  std::atomic<uint32_t> ui_hovered_element_status_{0};
  std::atomic<uint32_t> ui_hovered_slot_status_{REIM_UI_SLOT_NONE};
  std::atomic<uint32_t> ui_selected_slot_status_{0};
  std::atomic<uint32_t> ui_button_status_{0};

  std::vector<uint8_t> font_png_;
  std::string font_meta_json_;
  std::string model_defs_json_;
  std::string sound_index_json_;
  std::string item_index_json_;
  std::string ui_index_json_;
  std::string lang_json_;
  std::vector<uint8_t> item_atlas_png_;
  std::string item_atlas_json_;
  std::string textures_json_;
  std::string worldgen_summary_json_;
  std::map<uint32_t, std::vector<uint8_t>> sound_ogg_cache_;
  std::vector<std::string> resource_pack_paths_;
  std::vector<std::string> data_pack_paths_;
  std::string resource_pack_report_json_;
  std::string data_pack_report_json_;
  bool apply_atlas_overrides(const std::map<std::string, std::vector<uint8_t>>& overrides,
                             std::string& error);

  /* Save API (A7/B). Idle until a writable world exists. */
  std::mutex save_mutex_;
  ReimSaveStatus save_status_{};
  std::atomic<uint64_t> save_request_count_{0};

  std::chrono::steady_clock::time_point last_fetch_;
  uint64_t frame_counter_ = 0;
  uint32_t render_distance_ = 6;
  uint32_t render_level_ = 0;
};

} // namespace reim::runtime
