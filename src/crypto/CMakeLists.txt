project(ccf_crypto)

add_library(ccf_hostcrypto STATIC
  ${PROJECT_SOURCE_DIR}/crypto/hash.cpp
  ${PROJECT_SOURCE_DIR}/crypto/symmkey.cpp
)
add_library(ccf::hostcrypto ALIAS ccf_hostcrypto)

target_include_directories(ccf_hostcrypto INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(ccf_hostcrypto PUBLIC
  ccf::ds
  ccf::tls
  evercrypt::evercrypt
)
