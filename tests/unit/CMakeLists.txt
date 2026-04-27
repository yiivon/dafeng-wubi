add_executable(dafeng_unit_tests
  protocol_test.cc
)

target_link_libraries(dafeng_unit_tests
  PRIVATE
    dafeng::common
    GTest::gtest_main
)

gtest_discover_tests(dafeng_unit_tests)
