project(msgpack)

add_library(msgpack INTERFACE)
add_library(msgpack::msgpack ALIAS msgpack)

target_include_directories(msgpack INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
