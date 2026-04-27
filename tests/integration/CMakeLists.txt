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

gtest_discover_tests(dafeng_integration_tests
  PROPERTIES TIMEOUT 30
)
