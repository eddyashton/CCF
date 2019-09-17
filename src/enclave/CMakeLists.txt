project(ccf_enclave)

add_library(ccf_enclave INTERFACE)
add_library(ccf::enclave ALIAS ccf_enclave)

target_include_directories(ccf_enclave INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_enclave INTERFACE
  ccf::consensus
  ccf::node
  ccf::kv
)