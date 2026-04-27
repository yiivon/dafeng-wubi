# librime Lua bridge plugin. Mac-only build for Phase 2.1; Windows variant
# lands when the Win port begins.

if(NOT APPLE)
  message(STATUS "dafeng plugin: skipping (Mac-only in Phase 2.1)")
  return()
endif()

include(FetchContent)

# Vendored Lua 5.4 sources — used both as headers for the deployable .dylib
# and as a static library for the in-tree test that exercises the bridge
# without a running Squirrel.
FetchContent_Declare(
  dafeng_lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG        v5.4.7
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dafeng_lua)

# lua/lua ships only a Makefile; build a static lib here.
file(GLOB _lua_sources "${dafeng_lua_SOURCE_DIR}/*.c")
list(FILTER _lua_sources EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")

add_library(dafeng_lua_static STATIC ${_lua_sources})
target_include_directories(dafeng_lua_static PUBLIC ${dafeng_lua_SOURCE_DIR})
target_compile_definitions(dafeng_lua_static PUBLIC
  LUA_USE_MACOSX  # implies LUA_USE_POSIX in luaconf.h
)
# Lua's own sources predate strict warning sets; silence them here.
target_compile_options(dafeng_lua_static PRIVATE -w)
set_target_properties(dafeng_lua_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(dafeng::lua_static ALIAS dafeng_lua_static)

# Common bridge object — compiled once, shared by the deployable dylib and
# the test static lib so the two never drift.
add_library(dafeng_lua_bridge_obj OBJECT
  dafeng_lua_bridge.c
)
target_include_directories(dafeng_lua_bridge_obj PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
  ${dafeng_lua_SOURCE_DIR}
)
target_link_libraries(dafeng_lua_bridge_obj PUBLIC dafeng::client)
set_target_properties(dafeng_lua_bridge_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Static convenience target for tests: links the bridge code with vendored Lua.
add_library(dafeng_lua_bridge_test STATIC
  $<TARGET_OBJECTS:dafeng_lua_bridge_obj>
)
target_include_directories(dafeng_lua_bridge_test PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
  ${dafeng_lua_SOURCE_DIR}
)
target_link_libraries(dafeng_lua_bridge_test PUBLIC
  dafeng::client
  dafeng::lua_static
)
add_library(dafeng::lua_bridge_test ALIAS dafeng_lua_bridge_test)

# Deployable shared library — Lua C-API symbols stay unresolved at link
# time and are satisfied at load time by the host (Squirrel/Weasel).
add_library(dafeng_lua_bridge MODULE
  $<TARGET_OBJECTS:dafeng_lua_bridge_obj>
)
target_include_directories(dafeng_lua_bridge PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
  ${dafeng_lua_SOURCE_DIR}
)
target_link_libraries(dafeng_lua_bridge PRIVATE dafeng::client)
set_target_properties(dafeng_lua_bridge PROPERTIES
  PREFIX ""           # no "lib" prefix; Lua require() expects bare name
  OUTPUT_NAME "dafeng_lua_bridge"
  SUFFIX ".so"        # librime-lua looks for .so on package.cpath even on Mac
  POSITION_INDEPENDENT_CODE ON
)
target_link_options(dafeng_lua_bridge PRIVATE -undefined dynamic_lookup)
