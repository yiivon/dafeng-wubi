# dafeng_common — shared types, protocol codec, IPC primitives, logging.

add_library(dafeng_common STATIC
  protocol.cc
)

target_include_directories(dafeng_common
  PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(dafeng_common
  PUBLIC
    dafeng::compiler_flags
  PRIVATE
    dafeng::msgpack
)

add_library(dafeng::common ALIAS dafeng_common)
