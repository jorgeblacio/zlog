add_executable(zlog-append-bench append.cc)
target_link_libraries(zlog-append-bench
    libzlog
    zlog_seqr
    zlog_backend_ceph
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
)
install(TARGETS zlog-append-bench DESTINATION bin)
