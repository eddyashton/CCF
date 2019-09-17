add_executable(cchost
  ${CCF_DIR}/src/host/main.cpp
)

target_sources(cchost PUBLIC
  ${CMAKE_BINARY_DIR}/ccf_u.cpp
)

set_source_files_properties(
  ${CMAKE_BINARY_DIR}/ccf_u.cpp
  PROPERTIES GENERATED TRUE
)

target_include_directories(cchost PUBLIC
  "${CMAKE_BINARY_DIR}"
  "${MBEDTLS_INCLUDE_DIRS}"
)

target_link_libraries(cchost PUBLIC
  ccf::ds
  ccf::enclave
  ccf::hostcrypto
  CLI11::CLI11
  openenclave::oehost
  "${MBEDTLS_LIBRARIES}"
  uv
)
