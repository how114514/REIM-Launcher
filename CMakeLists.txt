cmake_minimum_required(VERSION 3.28)

project(REIM LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(REIM_BUILD_TESTS "Build m1-tests" ON)

# All M1 binaries land in build/m1/bin (all configurations share the folder,
# matching docs/M1_PLAN.md section 4).
set(REIM_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${REIM_BIN_DIR}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} "${REIM_BIN_DIR}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}/lib")
endforeach()

set(REIM_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------------------
# Third-party: fixed inputs live in third_party/ (no system paths, no network).
# Versions and hashes are recorded in third_party/LICENSES.md.
# ---------------------------------------------------------------------------
add_library(reim_thirdparty STATIC
  "${REIM_THIRD_PARTY}/miniz/miniz.c"
  "${REIM_THIRD_PARTY}/miniz/miniz_tdef.c"
  "${REIM_THIRD_PARTY}/miniz/miniz_tinfl.c"
  "${REIM_THIRD_PARTY}/miniz/miniz_zip.c"
)
target_include_directories(reim_thirdparty PUBLIC
  "${REIM_THIRD_PARTY}/miniz"
  "${REIM_THIRD_PARTY}/stb"
  "${REIM_THIRD_PARTY}/nlohmann"
)
target_compile_definitions(reim_thirdparty PRIVATE MINIZ_NO_STDIO)

# GLFW 3.4 is unpacked from the pinned tarball into the build cache, never
# installed system-wide.
set(REIM_GLFW_DEST "${CMAKE_BINARY_DIR}/third_party")
set(REIM_GLFW_SRC "${REIM_GLFW_DEST}/glfw-3.4")
if(NOT EXISTS "${REIM_GLFW_SRC}/CMakeLists.txt")
  file(ARCHIVE_EXTRACT
    INPUT "${REIM_THIRD_PARTY}/glfw-3.4.tar.gz"
    DESTINATION "${REIM_GLFW_DEST}"
  )
endif()
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REIM_GLFW_SRC}" "${CMAKE_BINARY_DIR}/third_party/glfw-build" EXCLUDE_FROM_ALL)

add_library(reim_headers INTERFACE)
target_include_directories(reim_headers INTERFACE
  "${REIM_THIRD_PARTY}/nlohmann"
  "${REIM_THIRD_PARTY}/stb"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/contract"
)

if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()

add_subdirectory(runtime/contract)
add_subdirectory(runtime/mc-native)
add_subdirectory(runtime/preview)
add_subdirectory(converter/java-analyzer)
add_subdirectory(converter/orchestrator)

if(REIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
