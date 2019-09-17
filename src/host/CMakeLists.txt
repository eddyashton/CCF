add_executable(cchost
  ${CCF_DIR}/src/host/main.cpp
)

target_sources(cchost PUBLIC
  ${CMAKE_BINARY_DIR}/ccf_u.cpp
)

target_include_directories(cchost PUBLIC
  ${CMAKE_BINARY_DIR}
)

target_link_libraries(cchost PUBLIC
  ccf::ds
  ccf::enclave
  ccf::hostcrypto
  CLI11::CLI11
  openenclave::oehost
)

#use_client_mbedtls(cchost)
#target_include_directories(cchost PRIVATE
#  ${OE_INCLUDE_DIR}
#  ${CMAKE_CURRENT_BINARY_DIR}
#)
#add_san(cchost)

