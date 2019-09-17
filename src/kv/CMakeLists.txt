project(ccf_kv)

add_library(ccf_kv INTERFACE)
add_library(ccf::kv ALIAS ccf_kv)

target_include_directories(ccf_kv INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_kv INTERFACE
  ccf::ds
  msgpack::msgpack
)
