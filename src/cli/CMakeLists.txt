# dafeng-cli — debugging / smoke-test command-line tool.

add_executable(dafeng-cli
  main.cc
)

target_link_libraries(dafeng-cli
  PRIVATE
    dafeng::client
    dafeng::compiler_flags
)
