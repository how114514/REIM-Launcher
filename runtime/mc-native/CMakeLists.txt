add_library(reim_core STATIC
  util/log.cpp
  util/utf.cpp
  util/file.cpp
  util/hash.cpp
  util/zip.cpp
  util/png.cpp
  util/compression.cpp
  samples/packed_bits.cpp
  data/generated_manifest.cpp
  nbt/nbt.cpp
  anvil/region.cpp
  world/chunk.cpp
  world/level.cpp
  world/world.cpp
)
target_include_directories(reim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(reim_core PUBLIC reim_thirdparty reim_headers)

if(MSVC)
  # Core is shared by the DLL and offline tools. The DLL boundary itself never
  # propagates a C++ exception (docs/ARCHITECTURE.md section 8).
  target_compile_options(reim_core PRIVATE /EHs-c-)
endif()

add_library(mc_26_3 SHARED
  src/library.cpp
)
target_link_libraries(mc_26_3 PRIVATE reim_core reim_contract)
target_compile_definitions(mc_26_3 PRIVATE REIM_BUILD_DLL)
set_target_properties(mc_26_3 PROPERTIES
  OUTPUT_NAME "mc_26.3"
  PREFIX ""
)
if(MSVC)
  target_compile_options(mc_26_3 PRIVATE /EHs-c-)
endif()
