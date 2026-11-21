add_executable(reim-convert
  main.cpp
  common.cpp
  prepare.cpp
  data.cpp
  analyze.cpp
  extract.cpp
  ingest_v3.cpp
  diff.cpp
  ledger.cpp
  patch.cpp
  model_resolver.cpp
  inspect_world.cpp
  package.cpp
)
target_link_libraries(reim-convert PRIVATE
  reim_core
  reim_contract
  reim_thirdparty
)
target_include_directories(reim-convert PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(MSVC)
  target_compile_options(reim-convert PRIVATE /EHsc)
endif()

# reim-analyze.jar must be present next to the converter for `analyze`.
if(TARGET reim_analyze_jar)
  add_dependencies(reim-convert reim_analyze_jar)
endif()
