cmake_minimum_required(VERSION 3.20)

## Version is mirrored in the VERSION file at the repo root. Keep them in
## sync: `cmake -B build` reads CMakeLists; release tooling reads VERSION.
project(dafeng
  VERSION 0.1.0
  DESCRIPTION "Dafeng Wubi — cross-platform learning Wubi IME"
  HOMEPAGE_URL "https://github.com/kevin/dafeng-wubi"
  LANGUAGES C CXX
)

# ---- Project-wide defaults ----------------------------------------------------

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Hide everything by default; modules opt-in via DAFENG_API.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(DafengCompilerFlags)

find_package(Threads REQUIRED)

# ---- Options ------------------------------------------------------------------

option(DAFENG_BUILD_TESTS     "Build unit and integration tests"      ON)
option(DAFENG_BUILD_BENCHMARK "Build IPC benchmarks"                  ON)
option(DAFENG_BUILD_PLUGIN    "Build librime Lua bridge plugin"       ON)
option(DAFENG_BUILD_CLI       "Build dafeng-cli debugging tool"       ON)
option(DAFENG_ENABLE_MLX      "Enable MLX inference backend (Mac)"    OFF)
option(DAFENG_ENABLE_LLAMA_CPP "Enable llama.cpp inference backend"   OFF)
option(DAFENG_ENABLE_GIT_SYNC "Enable libgit2 userdata sync"          ON)

# ---- Platform sanity ----------------------------------------------------------

if(APPLE)
  message(STATUS "dafeng: building for macOS (${CMAKE_HOST_SYSTEM_PROCESSOR})")
elseif(WIN32)
  message(STATUS "dafeng: building for Windows")
else()
  message(WARNING
    "dafeng: target platform '${CMAKE_SYSTEM_NAME}' is not officially supported. "
    "Mac and Windows are the targets; Linux is best-effort for CI only.")
endif()

# ---- Subdirectories ------------------------------------------------------------
# Order matters: common is the leaf everyone else depends on.

add_subdirectory(third_party)
add_subdirectory(src/common)
add_subdirectory(src/client)
add_subdirectory(src/daemon)

if(DAFENG_BUILD_CLI)
  add_subdirectory(src/cli)
endif()

if(DAFENG_BUILD_PLUGIN)
  add_subdirectory(src/plugin)
endif()

if(DAFENG_BUILD_TESTS OR DAFENG_BUILD_BENCHMARK)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Summary ------------------------------------------------------------------

message(STATUS "")
message(STATUS "dafeng configuration:")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests          : ${DAFENG_BUILD_TESTS}")
message(STATUS "  Benchmarks     : ${DAFENG_BUILD_BENCHMARK}")
message(STATUS "  Plugin (Lua)   : ${DAFENG_BUILD_PLUGIN}")
message(STATUS "  CLI tool       : ${DAFENG_BUILD_CLI}")
message(STATUS "  MLX backend    : ${DAFENG_ENABLE_MLX}")
message(STATUS "  llama.cpp      : ${DAFENG_ENABLE_LLAMA_CPP}")
message(STATUS "  Git sync       : ${DAFENG_ENABLE_GIT_SYNC}")
message(STATUS "")
