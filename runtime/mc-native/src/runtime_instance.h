#pragma once

#include "core/simulation.h"
#include "reim_abi.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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
  void set_paused(bool paused);
  int32_t get_status_json(char* buffer, uint32_t buffer_size);
  int32_t get_runtime_info(ReimRuntimeInfo* out);
  int32_t get_asset(uint32_t kind, const void** out_data, uint64_t* out_size,
                    uint64_t* out_revision);

  void publish_status(const core::SimStatus& status) override;
  void publish_mesh_events(std::vector<core::MeshEvent>& events) override;

 private:
  struct FrameState {
    core::SimStatus status;
    std::vector<std::shared_ptr<mesh::SectionMeshOutput>> keep;
    std::vector<ReimSectionMeshUpdate> updates;
    std::vector<ReimSectionRemove> removes;
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
  void free_outstanding_frames();
  size_t outstanding_count();

  /* Copied by value: the Host's ReimHostApi struct is caller-scoped (for the UE
   * Bridge it is a stack local of Initialize) and must never be dereferenced
   * after create returns (M2-B6). */
  ReimHostApi host_{};
  /* Points at host_ while this instance is in the DLL-wide log broadcast list. */
  const ReimHostApi* registered_host_ = nullptr;
  core::Simulation simulation_;

  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> started_{false};
  std::atomic<float> partial_tick_{0.0f};

  std::mutex input_mutex_;
  std::vector<core::SimInput> queued_inputs_;

  std::mutex frame_mutex_;
  FrameState pending_;
  FrameState published_;
  core::SimStatus latest_status_;
  core::SimStatus prev_status_;
  bool has_prev_status_ = false;

  std::mutex frames_out_mutex_;
  std::vector<OutstandingFrame> outstanding_;

  /* v2 runtime identity and assets. Immutable after create(). */
  std::string build_id_;
  uint64_t manifest_hash_ = 0;
  std::vector<uint8_t> atlas_png_;
  std::string atlas_meta_json_;
  uint64_t atlas_revision_ = 0;

  std::chrono::steady_clock::time_point last_fetch_;
  uint64_t frame_counter_ = 0;
  uint32_t render_distance_ = 6;
};

} // namespace reim::runtime
