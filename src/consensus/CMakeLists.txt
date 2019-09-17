project(ccf_consensus)

add_library(ccf_consensus INTERFACE
)
add_library(ccf::consensus ALIAS ccf_consensus)

target_include_directories(ccf_consensus INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_consensus INTERFACE
  msgpack::msgpack
)
