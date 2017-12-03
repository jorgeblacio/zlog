link_directories(${SEASTAR_LIBRARY_DIRS})

add_executable(zlog-seqd seqd.cc)
set_target_properties(zlog-seqd PROPERTIES
  CXX_STANDARD 14
  LINK_FLAGS -pthread)
target_compile_options(zlog-seqd PUBLIC ${SEASTAR_CFLAGS})
target_include_directories(zlog-seqd PUBLIC ${SEASTAR_INCLUDE_DIRS})
target_link_libraries(zlog-seqd -lseastar ${SEASTAR_LIBRARIES} -ldl)

add_executable(client client.cc)
set_target_properties(client PROPERTIES
  CXX_STANDARD 14
  LINK_FLAGS -pthread)
target_compile_options(client PUBLIC ${SEASTAR_CFLAGS})
target_include_directories(client PUBLIC ${SEASTAR_INCLUDE_DIRS})
target_link_libraries(client -lseastar ${SEASTAR_LIBRARIES} -ldl)
