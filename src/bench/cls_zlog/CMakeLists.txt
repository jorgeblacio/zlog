add_executable(zlog-cls-bench cls_zlog_bench.cc)
target_link_libraries(zlog-cls-bench
    cls_zlog_client
    ${LIBRADOS_LIBRARIES}
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    atomic
)
install(TARGETS zlog-cls-bench DESTINATION bin)
