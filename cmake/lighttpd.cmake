# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

set(LIGHTTPD_PREFIX
    ${CCF_3RD_PARTY_INTERNAL_DIR}/lighttpd
    CACHE PATH "Prefix to the lighttpd directory"
)
message(STATUS "Using lighttpd at ${LIGHTTPD_PREFIX}")

set(LIGHTTPD_SRC ${LIGHTTPD_PREFIX}/src/common/radix.c)

if(COMPILE_TARGET STREQUAL "sgx")
  add_enclave_library_c(lighttpd.enclave ${LIGHTTPD_SRC})
  target_include_directories(
    lighttpd.enclave PUBLIC $<BUILD_INTERFACE:${LIGHTTPD_PREFIX}/include>
                            $<INSTALL_INTERFACE:include/3rdparty/lighttpd>
  )
  install(
    TARGETS lighttpd.enclave
    EXPORT ccf
    DESTINATION lib
  )
elseif(COMPILE_TARGET STREQUAL "snp")
  add_library(lighttpd.snp STATIC ${LIGHTTPD_SRC})
  target_include_directories(
    lighttpd.snp PUBLIC $<BUILD_INTERFACE:${LIGHTTPD_PREFIX}/include>
                        $<INSTALL_INTERFACE:include/3rdparty/lighttpd>
  )
  add_san(lighttpd.snp)
  set_property(TARGET lighttpd.snp PROPERTY POSITION_INDEPENDENT_CODE ON)
  install(
    TARGETS lighttpd.snp
    EXPORT ccf
    DESTINATION lib
  )
endif()

add_library(lighttpd.host STATIC ${LIGHTTPD_SRC})
target_include_directories(
  lighttpd.host PUBLIC $<BUILD_INTERFACE:${LIGHTTPD_PREFIX}/include>
                       $<INSTALL_INTERFACE:include/3rdparty/lighttpd>
)
add_san(lighttpd.host)
set_property(TARGET lighttpd.host PROPERTY POSITION_INDEPENDENT_CODE ON)

if(INSTALL_VIRTUAL_LIBRARIES)
  install(
    TARGETS lighttpd.host
    EXPORT ccf
    DESTINATION lib
  )
endif()
