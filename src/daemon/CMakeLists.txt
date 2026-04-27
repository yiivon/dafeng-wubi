# dafeng-daemon — main service process. Phase 2.1 carries only the IPC
# server, signal handling, and a deterministic mock reranker. MLX /
# llama.cpp / SQLite / libgit2 land in later phases through services.h.

add_executable(dafeng-daemon
  main.cc
  server.cc
  mock_reranker.cc
)

target_include_directories(dafeng-daemon
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(dafeng-daemon
  PRIVATE
    dafeng::common
    dafeng::compiler_flags
    Threads::Threads
)
