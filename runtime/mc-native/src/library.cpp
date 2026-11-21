/*
 * mc_26.3.dll: versioned C ABI entry points.
 *
 * S3 (M1-D/E) replaces the S1 contract stub. create() validates the Converter
 * manifest, opens the real world and generated block/texture tables; start()
 * launches the fixed 20 TPS simulation thread. The gameplay world and mesh
 * cache belong to the simulation thread only (core/simulation.h); the render
 * thread sees immutable frame snapshots through fetch_frame.
 */
#include "reim_abi.h"

#include "src/runtime_instance.h"
#include "util/log.h"

#include <cstdint>

namespace {

using reim::runtime::RuntimeInstance;

constexpr uint32_t kExpectedAbi = REIM_ABI_VERSION;

ReimInstance* REIM_CALL create_instance(const ReimInstanceConfig* config,
                                        const ReimHostApi* host,
                                        int32_t* out_error) {
  return reinterpret_cast<ReimInstance*>(
      RuntimeInstance::create(config, host, out_error));
}

void REIM_CALL destroy_instance(ReimInstance* instance) {
  delete reinterpret_cast<RuntimeInstance*>(instance);
}

int32_t REIM_CALL start_instance(ReimInstance* instance) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->start();
}

void REIM_CALL stop_instance(ReimInstance* instance) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->stop();
}

int32_t REIM_CALL fetch_frame(ReimInstance* instance, ReimRenderFrame* out) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->fetch_frame(out);
}

void REIM_CALL submit_input(ReimInstance* instance, const ReimInputEvent* event) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->submit_input(event);
}

void REIM_CALL set_simulation_paused(ReimInstance* instance, uint8_t paused) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->set_paused(paused != 0);
}

int32_t REIM_CALL get_status_json(ReimInstance* instance, char* buffer,
                                  uint32_t buffer_size) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->get_status_json(buffer,
                                                                       buffer_size);
}

void REIM_CALL release_frame(ReimInstance* instance, uint64_t frame_id) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->release_frame(frame_id);
}

int32_t REIM_CALL get_runtime_info(ReimInstance* instance, ReimRuntimeInfo* out) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->get_runtime_info(out);
}

int32_t REIM_CALL get_asset(ReimInstance* instance, uint32_t kind,
                            uint32_t asset_id, const void** out_data,
                            uint64_t* out_size, uint64_t* out_revision) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->get_asset(
      kind, asset_id, out_data, out_size, out_revision);
}

void REIM_CALL submit_action(ReimInstance* instance, const ReimActionEvent* event) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->submit_action(event);
}

int32_t REIM_CALL request_save(ReimInstance* instance, uint32_t flags) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->request_save(flags);
}

int32_t REIM_CALL get_save_status(ReimInstance* instance, ReimSaveStatus* out) {
  if (!instance) return REIM_ERR_INVALID_ARGUMENT;
  return reinterpret_cast<RuntimeInstance*>(instance)->get_save_status(out);
}

void REIM_CALL set_ui_canvas(ReimInstance* instance, uint32_t width,
                             uint32_t height, float scale) {
  if (!instance) return;
  reinterpret_cast<RuntimeInstance*>(instance)->set_ui_canvas(width, height,
                                                              scale);
}

const ReimRuntimeApi kApi = {
    sizeof(ReimRuntimeApi),
    REIM_ABI_VERSION,
    &create_instance,
    &destroy_instance,
    &start_instance,
    &stop_instance,
    &fetch_frame,
    &submit_input,
    &set_simulation_paused,
    &get_status_json,
    &release_frame,
    &get_runtime_info,
    &get_asset,
    &submit_action,
    &request_save,
    &get_save_status,
    &set_ui_canvas,
};

} // namespace

extern "C" REIM_EXPORT const ReimRuntimeApi* REIM_CALL
reim_get_api(uint32_t abi_version) {
  if (abi_version != REIM_ABI_VERSION) {
    reim::log::write(reim::log::Level::error, "abi",
                     "reim_get_api: ABI version mismatch");
    return nullptr;
  }
  return &kApi;
}
