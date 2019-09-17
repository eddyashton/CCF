project(ccf_ds)

add_library(ccf_ds INTERFACE)
add_library(ccf::ds ALIAS ccf_ds)

target_include_directories(ccf_ds INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_ds INTERFACE
  fmt::fmt_header_only
  msgpack::msgpack
  nlohmann_json::nlohmann_json
)
