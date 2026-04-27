# Third-party dependencies. Populated incrementally:
#   Phase 2.1 / S2 — msgpack-c (cpp_master, header-only)
#   Phase 2.4    — sqlite, libgit2
#   Phase 2.2+   — librime, MLX, llama.cpp

# msgpack-cxx (header-only C++ MessagePack codec).
# Tracked as the cpp_master branch of msgpack/msgpack-c.
set(_msgpack_dir "${CMAKE_CURRENT_SOURCE_DIR}/msgpack-c")
if(NOT EXISTS "${_msgpack_dir}/include/msgpack.hpp")
  message(FATAL_ERROR
    "msgpack-c submodule missing. Run from repo root:\n"
    "  git submodule update --init --recursive third_party/msgpack-c")
endif()

add_library(dafeng_msgpack INTERFACE)
target_include_directories(dafeng_msgpack SYSTEM INTERFACE
  "${_msgpack_dir}/include"
)
# msgpack-cxx ships with optional Boost adaptors. We don't need them.
target_compile_definitions(dafeng_msgpack INTERFACE
  MSGPACK_NO_BOOST
)
add_library(dafeng::msgpack ALIAS dafeng_msgpack)
