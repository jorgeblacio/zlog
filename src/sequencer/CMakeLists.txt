add_executable(zlog-seqd seqd.cc)

set_target_properties(zlog-seqd PROPERTIES CXX_STANDARD 14)
target_compile_definitions(zlog-seqd
  PUBLIC $<TARGET_PROPERTY:Seastar::Seastar,INTERFACE_COMPILE_DEFINITIONS>)
target_include_directories(zlog-seqd
  PUBLIC $<TARGET_PROPERTY:Seastar::Seastar,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(zlog-seqd Seastar::Seastar)

add_executable(client client.cc)
set_target_properties(client PROPERTIES CXX_STANDARD 14)
target_compile_definitions(client
  PUBLIC $<TARGET_PROPERTY:Seastar::Seastar,INTERFACE_COMPILE_DEFINITIONS>)
target_include_directories(client
  PUBLIC $<TARGET_PROPERTY:Seastar::Seastar,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(client Seastar::Seastar)
