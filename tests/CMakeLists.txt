# Tests and benchmarks.

include(FetchContent)

# GoogleTest. Pinned commit to keep CI deterministic.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
  GIT_SHALLOW    TRUE
)

# Avoid GoogleTest installing alongside our targets and avoid Windows runtime mismatch.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

if(DAFENG_BUILD_TESTS)
  add_subdirectory(unit)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt")
    add_subdirectory(integration)
  endif()
endif()

if(DAFENG_BUILD_BENCHMARK AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
  add_subdirectory(benchmarks)
endif()
