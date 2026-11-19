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
