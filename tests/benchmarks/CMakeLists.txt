add_executable(ipc_pingpong
  ipc_pingpong.cc
)

target_link_libraries(ipc_pingpong
  PRIVATE
    dafeng::client
    dafeng::daemon_core
    dafeng::compiler_flags
    Threads::Threads
)

# Phase 2.1 IPC benchmark: P99 < 5 ms is the contract from DESIGN §3.
add_test(NAME IpcPingpongBudget
         COMMAND ipc_pingpong --iterations 5000 --p99-budget-us 5000)
set_tests_properties(IpcPingpongBudget PROPERTIES TIMEOUT 30)

add_executable(reranker_latency
  reranker_latency.cc
)

target_link_libraries(reranker_latency
  PRIVATE
    dafeng::daemon_core
    dafeng::compiler_flags
    Threads::Threads
)

# Phase 2.2 reranker budget: P99 < 30 ms (DESIGN §3 LLM cap).
add_test(NAME RerankerLatencyBudgetDeterministic
         COMMAND reranker_latency
                  --backend deterministic
                  --iterations 5000
                  --p99-budget-us 30000)
set_tests_properties(RerankerLatencyBudgetDeterministic PROPERTIES TIMEOUT 30)
