project(ccf_node)

add_library(ccf_node INTERFACE)
add_library(ccf::node ALIAS ccf_node)

target_include_directories(ccf_node INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_node INTERFACE
)