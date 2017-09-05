add_executable(zlog-cls-zlog-bench bench.cc)
target_link_libraries(zlog-cls-zlog-bench
    libzlog
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_SYSTEM_LIBRARY})
