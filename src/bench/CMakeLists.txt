if(BUILD_CEPH_BACKEND)
  add_subdirectory(cls_zlog)
endif(BUILD_CEPH_BACKEND)

add_executable(zlog-bench2 bench2.cc)
target_link_libraries(zlog-bench2
    libzlog zlog_seqr lmdb
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    atomic
)
install(TARGETS zlog-bench2 DESTINATION bin)
