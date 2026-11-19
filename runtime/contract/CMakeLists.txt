add_library(reim_contract INTERFACE)
target_include_directories(reim_contract INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(reim_contract INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/reim_abi.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/reim_render.h"
)
