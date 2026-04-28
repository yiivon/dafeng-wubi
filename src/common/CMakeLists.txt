# dafeng_common — shared types, protocol codec, IPC primitives, logging.

set(DAFENG_COMMON_SOURCES
  protocol.cc
  logging.cc
)

if(APPLE OR (UNIX AND NOT WIN32))
  list(APPEND DAFENG_COMMON_SOURCES
    endpoint_unix.cc
    paths_mac.cc
  )
elseif(WIN32)
  list(APPEND DAFENG_COMMON_SOURCES
    endpoint_win.cc
    paths_win.cc
  )
else()
  message(FATAL_ERROR "Unsupported platform for dafeng_common")
endif()

add_library(dafeng_common STATIC ${DAFENG_COMMON_SOURCES})

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

if(WIN32)
  # SHGetKnownFolderPath (shell32, ole32 for CoTaskMemFree),
  # GetUserNameW (advapi32). kernel32 (CreateNamedPipeW etc.) is implicit.
  target_link_libraries(dafeng_common PRIVATE shell32 ole32 advapi32)
endif()

add_library(dafeng::common ALIAS dafeng_common)
