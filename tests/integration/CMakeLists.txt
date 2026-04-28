add_executable(dafeng_integration_tests
  client_daemon_test.cc
)

target_link_libraries(dafeng_integration_tests
  PRIVATE
    dafeng::client
    dafeng::daemon_core
    GTest::gtest_main
    Threads::Threads
)
target_include_directories(dafeng_integration_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")

gtest_discover_tests(dafeng_integration_tests
  PROPERTIES TIMEOUT 30
)

# Lua bridge test — only when the plugin is enabled (Mac-only in Phase 2.1).
if(APPLE AND DAFENG_BUILD_PLUGIN AND TARGET dafeng::lua_bridge_test)
  add_executable(dafeng_lua_bridge_tests
    lua_bridge_test.cc
  )
  target_compile_definitions(dafeng_lua_bridge_tests PRIVATE
    DAFENG_REPO_ROOT="${CMAKE_SOURCE_DIR}"
  )
  target_link_libraries(dafeng_lua_bridge_tests
    PRIVATE
      dafeng::lua_bridge_test
      dafeng::daemon_core
      GTest::gtest_main
      Threads::Threads
  )
  gtest_discover_tests(dafeng_lua_bridge_tests
    PROPERTIES TIMEOUT 30
  )
endif()
