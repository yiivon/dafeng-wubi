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
# Wire it into ctest so regressions show up in CI.
add_test(NAME IpcPingpongBudget
         COMMAND ipc_pingpong --iterations 5000 --p99-budget-us 5000)
set_tests_properties(IpcPingpongBudget PROPERTIES TIMEOUT 30)
