# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
cmake_minimum_required(VERSION 3.11)

add_definitions(-DOE_API_VERSION=2)

set(CCF_DIR ${CMAKE_CURRENT_SOURCE_DIR})
include(${CCF_DIR}/cmake/preproject.cmake)

project(ccf C CXX)

# Use Find* modules defined in CCF/cmake
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Use Find* modules defined in CCF/cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

find_package(openenclave 0.6 REQUIRED CONFIG)
find_package(MbedTLS REQUIRED)

set(SERVICE_IDENTITY_CURVE_CHOICE "secp384r1" CACHE STRING "One of secp384r1, curve25519, secp256k1_mbedtls, secp256k1_bitcoin")
if (${SERVICE_IDENTITY_CURVE_CHOICE} STREQUAL "secp384r1")
  add_definitions(-DSERVICE_IDENTITY_CURVE_CHOICE_SECP384R1)
elseif (${SERVICE_IDENTITY_CURVE_CHOICE} STREQUAL "curve25519")
  add_definitions(-DSERVICE_IDENTITY_CURVE_CHOICE_CURVE25519)
elseif (${SERVICE_IDENTITY_CURVE_CHOICE} STREQUAL "secp256k1_mbedtls")
  add_definitions(-DSERVICE_IDENTITY_CURVE_CHOICE_SECP256K1_MBEDTLS)
elseif (${SERVICE_IDENTITY_CURVE_CHOICE} STREQUAL "secp256k1_bitcoin")
  add_definitions(-DSERVICE_IDENTITY_CURVE_CHOICE_SECP256K1_BITCOIN)
else ()
  message(FATAL_ERROR "Unsupported curve choice ${SERVICE_IDENTITY_CURVE_CHOICE}")
endif ()

set(OE_PREFIX "/opt/openenclave" CACHE PATH "Path to Open Enclave install")
set(OE_BIN_DIR "${OE_PREFIX}/bin")
set(OEGEN "${OE_BIN_DIR}/oeedger8r")

add_custom_command(
  COMMAND ${OEGEN} ${CCF_DIR}/src/edl/ccf.edl --trusted --trusted-dir ${CMAKE_BINARY_DIR} --untrusted --untrusted-dir ${CMAKE_BINARY_DIR}
  COMMAND mv ${CMAKE_BINARY_DIR}/ccf_t.c ${CMAKE_BINARY_DIR}/ccf_t.cpp
  COMMAND mv ${CMAKE_BINARY_DIR}/ccf_u.c ${CMAKE_BINARY_DIR}/ccf_u.cpp
  DEPENDS ${CCF_DIR}/src/edl/ccf.edl
  OUTPUT "${CMAKE_BINARY_DIR}/ccf_t.cpp" "${CMAKE_BINARY_DIR}/ccf_u.cpp"
  COMMENT "Generating code from EDL, and renaming to .cpp"
)

add_custom_target(
  ccf_generated
  DEPENDS
    "${CMAKE_BINARY_DIR}/ccf_t.cpp"
    "${CMAKE_BINARY_DIR}/ccf_u.cpp"
)

add_subdirectory(3rdparty/CLI11)
add_subdirectory(3rdparty/doctest)
add_subdirectory(3rdparty/evercrypt)
add_subdirectory(3rdparty/fmt)
add_subdirectory(3rdparty/msgpack-c)
add_subdirectory(3rdparty/nlohmann)
add_subdirectory(3rdparty/secp256k1)

add_subdirectory(src/consensus)
add_subdirectory(src/crypto)
add_subdirectory(src/ds)
add_subdirectory(src/enclave)
add_subdirectory(src/host)
add_subdirectory(src/node)
add_subdirectory(src/tls)
add_subdirectory(src/kv)
