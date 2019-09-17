project(fmt)

add_library(fmt_header_only INTERFACE)
add_library(fmt::fmt_header_only ALIAS fmt_header_only)

target_compile_definitions(fmt_header_only INTERFACE FMT_HEADER_ONLY=1)

target_include_directories(fmt_header_only INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)
