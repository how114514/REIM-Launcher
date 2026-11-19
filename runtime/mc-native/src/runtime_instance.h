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
 * ABI instance: owns the fixed-step simulation thread and the double-buffered
 * render snapshot. The simulation thread is the only writer to Simulation;
 * fetch_frame swaps the published snapshot under a short lock.
 */
class RuntimeInstance final : public core::SimulationSink {
 public:
  static RuntimeInstance* create(const ReimInstanceConfig* config,
                                 const ReimHostApi* host, int32_t* out_error);
  ~RuntimeInstance();

  int32_t start();
  void stop();
  int32_t fetch_frame(ReimRenderFrame* out);
  void submit_input(const ReimInputEvent* event);
  void set_paused(bool paused);
  int32_t get_status_json(char* buffer, uint32_t buffer_size);

  void publish_status(const core::SimStatus& status) override;
  void publish_mesh_events(std::vector<core::MeshEvent>& events) override;

 private:
  struct FrameState {
    core::SimStatus status;
    uint64_t frame_id = 0;
    std::vector<std::shared_ptr<mesh::SectionMeshOutput>> keep;
    std::vector<ReimSectionMeshUpdate> updates;
    std::vector<ReimSectionRemove> removes;
  };

  RuntimeInstance() = default;

  void run();
  void log_message(int32_t level, const char* category, const std::string& message);
  std::string build_status_json();

  const ReimHostApi* host_ = nullptr;
  /* Host registered in the DLL-wide log broadcast list (see runtime_instance.cpp). */
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

  std::chrono::steady_clock::time_point last_fetch_;
  uint64_t frame_counter_ = 0;
  uint32_t render_distance_ = 6;
};

} // namespace reim::runtime
