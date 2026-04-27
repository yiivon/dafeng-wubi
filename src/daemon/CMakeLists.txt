# dafeng-daemon — main service process. Phase 2.1 carries only the IPC
# server, signal handling, and a deterministic mock reranker. MLX /
# llama.cpp / SQLite / libgit2 land in later phases through services.h.

# Daemon split into a library + thin executable so tests can drive the
# server in-process.

set(DAFENG_DAEMON_SOURCES
  server.cc
  mock_reranker.cc
  reranker_deterministic.cc
)

if(DAFENG_ENABLE_MLX AND APPLE)
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx.cc)
else()
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx_stub.cc)
endif()

add_library(dafeng_daemon_core STATIC ${DAFENG_DAEMON_SOURCES})

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
