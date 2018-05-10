add_library(zlog_eviction_lru SHARED lru.cc)
set_target_properties(zlog_eviction_lru PROPERTIES
  OUTPUT_NAME zlog_eviction_lru
  VERSION 1.0.0
  SOVERSION 1)
install(TARGETS zlog_eviction_lru LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})