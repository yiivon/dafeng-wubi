# dafeng-daemon — main service process. Phase 2.1 carries only the IPC
# server, signal handling, and a deterministic mock reranker. MLX /
# llama.cpp / SQLite / libgit2 land in later phases through services.h.

# Daemon split into a library + thin executable so tests can drive the
# server in-process.

set(DAFENG_DAEMON_SOURCES
  server.cc
  mock_reranker.cc
  reranker_deterministic.cc
  history_store.cc
  ngram.cc
  new_word.cc
)

if(DAFENG_ENABLE_MLX AND APPLE)
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx.cc)
else()
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx_stub.cc)
endif()

add_library(dafeng_daemon_core STATIC ${DAFENG_DAEMON_SOURCES})

find_package(SQLite3 REQUIRED)
target_link_libraries(dafeng_daemon_core PRIVATE SQLite::SQLite3)

if(DAFENG_ENABLE_MLX AND APPLE)
  # mlx-c provides a C API for MLX; we link against it when present.
  # If brew has it: /opt/homebrew/include/mlx/c/mlx.h + /opt/homebrew/lib/libmlxc.dylib
  find_path(MLX_C_INCLUDE_DIR mlx/c/mlx.h
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(MLX_C_LIBRARY mlxc
    HINTS /opt/homebrew/lib /usr/local/lib)
  find_library(MLX_LIBRARY mlx
    HINTS /opt/homebrew/lib /usr/local/lib)
  if(MLX_C_INCLUDE_DIR AND MLX_C_LIBRARY AND MLX_LIBRARY)
    message(STATUS "dafeng: MLX found at ${MLX_C_INCLUDE_DIR}")
    target_include_directories(dafeng_daemon_core PRIVATE ${MLX_C_INCLUDE_DIR})
    target_link_libraries(dafeng_daemon_core PRIVATE ${MLX_C_LIBRARY} ${MLX_LIBRARY})
  else()
    message(WARNING "dafeng: DAFENG_ENABLE_MLX=ON but mlx / mlx-c not found. "
                    "Install via 'brew install mlx-c'. The reranker will run "
                    "via the deterministic fallback at runtime.")
  endif()
endif()

target_include_directories(dafeng_daemon_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(dafeng_daemon_core
  PUBLIC
    dafeng::common
  PRIVATE
    dafeng::compiler_flags
    Threads::Threads
)

add_library(dafeng::daemon_core ALIAS dafeng_daemon_core)

add_executable(dafeng-daemon
  main.cc
)

target_link_libraries(dafeng-daemon
  PRIVATE
    dafeng::daemon_core
    dafeng::compiler_flags
    Threads::Threads
)
