# dafeng_client — IPC client library used by the IME process and the Lua
# bridge. C++ surface in dafeng/client.h, C ABI in dafeng/client_c.h.

add_library(dafeng_client STATIC
  client.cc
  client_c.cc
)

target_link_libraries(dafeng_client
  PUBLIC
    dafeng::common
  PRIVATE
    dafeng::compiler_flags
    Threads::Threads
)

add_library(dafeng::client ALIAS dafeng_client)
