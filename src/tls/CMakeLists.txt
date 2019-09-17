project(ccf_tls)

add_library(ccf_tls INTERFACE)
add_library(ccf::tls ALIAS ccf_tls)

target_include_directories(ccf_tls INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_tls INTERFACE
  ccf::ds
  secp256k1::secp256k1
)
