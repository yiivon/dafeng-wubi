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
  user_dict.cc
  learning_round.cc
  auto_learn.cc
  wubi_codec.cc
)

if(DAFENG_ENABLE_GIT_SYNC)
  list(APPEND DAFENG_DAEMON_SOURCES git_sync.cc)
else()
  list(APPEND DAFENG_DAEMON_SOURCES git_sync_stub.cc)
endif()

if(DAFENG_ENABLE_MLX AND APPLE)
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx.cc)
else()
  list(APPEND DAFENG_DAEMON_SOURCES reranker_mlx_stub.cc)
endif()

if(DAFENG_ENABLE_LLAMA_CPP)
  list(APPEND DAFENG_DAEMON_SOURCES reranker_llamacpp.cc)
else()
  list(APPEND DAFENG_DAEMON_SOURCES reranker_llamacpp_stub.cc)
endif()

add_library(dafeng_daemon_core STATIC ${DAFENG_DAEMON_SOURCES})

find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
  target_link_libraries(dafeng_daemon_core PRIVATE SQLite3::SQLite3)
else()
  # vcpkg's port doesn't always materialize the SQLite3::SQLite3 IMPORTED
  # target via CMake's FindSQLite3 module; fall back to the variables it
  # always sets. Same effect, just not target-form.
  target_include_directories(dafeng_daemon_core PRIVATE ${SQLite3_INCLUDE_DIRS})
  target_link_libraries(dafeng_daemon_core PRIVATE ${SQLite3_LIBRARIES})
endif()

if(DAFENG_ENABLE_GIT_SYNC)
  find_path(GIT2_INCLUDE_DIR git2.h HINTS /opt/homebrew/include /usr/local/include)
  find_library(GIT2_LIBRARY git2 HINTS /opt/homebrew/lib /usr/local/lib)
  if(GIT2_INCLUDE_DIR AND GIT2_LIBRARY)
    message(STATUS "dafeng: libgit2 at ${GIT2_INCLUDE_DIR}")
    target_include_directories(dafeng_daemon_core PRIVATE ${GIT2_INCLUDE_DIR})
    target_link_libraries(dafeng_daemon_core PRIVATE ${GIT2_LIBRARY})
  else()
    message(WARNING
      "dafeng: DAFENG_ENABLE_GIT_SYNC=ON but libgit2 not found. "
      "Install via 'brew install libgit2'. Sync will be a no-op at runtime.")
  endif()
endif()

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

if(DAFENG_ENABLE_LLAMA_CPP)
  # llama.cpp from brew or system. We need libllama (high-level API) AND
  # libggml (which exposes ggml_backend_load_all — required to discover
  # Metal/CPU backends before model load).
  find_path(LLAMA_INCLUDE_DIR llama.h
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(LLAMA_LIBRARY llama
    HINTS /opt/homebrew/lib /usr/local/lib)
  find_library(GGML_LIBRARY ggml
    HINTS /opt/homebrew/lib /usr/local/lib)
  find_library(GGML_BASE_LIBRARY ggml-base
    HINTS /opt/homebrew/lib /usr/local/lib)
  if(LLAMA_INCLUDE_DIR AND LLAMA_LIBRARY AND GGML_LIBRARY AND GGML_BASE_LIBRARY)
    message(STATUS "dafeng: llama.cpp found at ${LLAMA_INCLUDE_DIR}")
    target_include_directories(dafeng_daemon_core PRIVATE ${LLAMA_INCLUDE_DIR})
    target_link_libraries(dafeng_daemon_core PRIVATE
      ${LLAMA_LIBRARY} ${GGML_LIBRARY} ${GGML_BASE_LIBRARY})
  else()
    message(WARNING "dafeng: DAFENG_ENABLE_LLAMA_CPP=ON but llama.cpp / ggml "
                    "not found. Install via 'brew install llama.cpp'. The "
                    "reranker will run via the deterministic fallback.")
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
