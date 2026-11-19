/*
 * mc_26.3.dll, S1 skeleton.
 *
 * M1-A/M1-B deliverable: the versioned C ABI is exported, LoadLibrary +
 * GetProcAddress("reim_get_api") works, and create() performs the
 * Converter -> Runtime contract check (profile, ABI, generated file hashes)
 * before failing with a clear "runtime core is scheduled for S3" diagnostic.
 *
 * The gameplay world/player/mesh pipeline is M1-D/E (S3) and is intentionally
 * absent here; no world data is simulated or fabricated in S1.
 */
#include "reim_abi.h"

#include "data/generated_manifest.h"
#include "util/log.h"
#include "util/utf.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace {

constexpr uint32_t kExpectedAbi = REIM_ABI_VERSION;
constexpr const char* kExpectedProfile = "26.3";

struct InstancePlaceholder {
  uint32_t magic = 0x5245494Du; // "REIM"
};

void log_to_host(const ReimHostApi* host, int32_t level, const char* category,
                 const char* message) {
  if (host && host->log) {
    host->log(level, category, message);
  }
}

ReimInstance* REIM_CALL create_instance(const ReimInstanceConfig* config,
                                        const ReimHostApi* host,
                                        int32_t* out_error) {
  if (out_error) *out_error = REIM_ERR_INVALID_ARGUMENT;
  if (!config || !out_error) {
    log_to_host(host, REIM_LOG_ERROR, "abi", "create: null config/out_error");
    return nullptr;
  }
  if (config->abi_version != kExpectedAbi) {
    *out_error = REIM_ERR_ABI_MISMATCH;
    log_to_host(host, REIM_LOG_ERROR, "abi",
                "create: instance config ABI mismatch");
    return nullptr;
  }
  if (config->struct_size < REIM_INSTANCE_CONFIG_MIN_SIZE) {
    *out_error = REIM_ERR_INVALID_ARGUMENT;
    log_to_host(host, REIM_LOG_ERROR, "abi",
                "create: instance config struct_size too small");
    return nullptr;
  }
  if (!config->generated_path) {
    *out_error = REIM_ERR_INVALID_ARGUMENT;
    log_to_host(host, REIM_LOG_ERROR, "abi",
                "create: generated_path is required");
    return nullptr;
  }

  const std::filesystem::path generated_dir(config->generated_path);
  reim::data::GeneratedManifest manifest;
  std::string error;
  if (!reim::data::validate_generated(generated_dir, kExpectedProfile,
                                      kExpectedAbi, manifest, error)) {
    *out_error = REIM_ERR_VERSION_MISMATCH;
    const std::string message = "create: generated data rejected: " + error;
    log_to_host(host, REIM_LOG_ERROR, "data", message.c_str());
    return nullptr;
  }

  const std::string message =
      "S1: ABI and generated manifest validated (" + manifest.profile +
      ", " + std::to_string(manifest.blocks_total) + " blocks, " +
      std::to_string(manifest.sprites) + " sprites); world/player runtime is M1-D (S3)";
  log_to_host(host, REIM_LOG_INFO, "runtime", message.c_str());
  *out_error = REIM_ERR_INTERNAL;
  return nullptr;
}

void REIM_CALL destroy_instance(ReimInstance* instance) {
  delete reinterpret_cast<InstancePlaceholder*>(instance);
}

int32_t REIM_CALL start_instance(ReimInstance*) {
  return REIM_ERR_NOT_STARTED;
}

void REIM_CALL stop_instance(ReimInstance*) {}

int32_t REIM_CALL fetch_frame(ReimInstance*, ReimRenderFrame* out) {
  if (!out) return REIM_ERR_INVALID_ARGUMENT;
  if (out->struct_size < REIM_RENDER_FRAME_MIN_SIZE) return REIM_ERR_INVALID_ARGUMENT;
  std::memset(out, 0, sizeof(*out));
  out->struct_size = sizeof(ReimRenderFrame);
  out->abi_version = kExpectedAbi;
  return REIM_ERR_NOT_STARTED;
}

void REIM_CALL submit_input(ReimInstance*, const ReimInputEvent*) {}

void REIM_CALL set_simulation_paused(ReimInstance*, uint8_t) {}

int32_t REIM_CALL get_status_json(ReimInstance*, char* buffer, uint32_t buffer_size) {
  static constexpr const char* kStatus =
      "{\"stage\":\"S1\",\"runtime\":\"abi-stub\",\"world\":false,"
      "\"note\":\"world/player/mesh runtime is M1-D (S3)\"}";
  const size_t length = std::strlen(kStatus);
  if (!buffer || buffer_size == 0) return static_cast<int32_t>(length + 1);
  if (buffer_size <= length) return REIM_ERR_INVALID_ARGUMENT;
  std::memcpy(buffer, kStatus, length + 1);
  return static_cast<int32_t>(length);
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
