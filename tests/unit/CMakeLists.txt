add_executable(dafeng_unit_tests
  protocol_test.cc
  endpoint_test.cc
  mock_reranker_test.cc
  logging_test.cc
  reranker_deterministic_test.cc
  history_store_test.cc
  ngram_test.cc
  new_word_test.cc
  user_dict_test.cc
  git_sync_test.cc
)

target_link_libraries(dafeng_unit_tests
  PRIVATE
    dafeng::daemon_core  # transitively pulls in dafeng::common
    GTest::gtest_main
)

gtest_discover_tests(dafeng_unit_tests)
