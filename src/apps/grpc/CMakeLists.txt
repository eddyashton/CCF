# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

cmake_minimum_required(VERSION 3.16)

project(external_executor LANGUAGES C CXX)

set(CCF_PROJECT "ccf_${COMPILE_TARGET}")

if(NOT TARGET "ccf")
  find_package(${CCF_PROJECT} REQUIRED)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/protobuf)

add_ccf_app(
  grpc_demo
  SRCS grpc_demo.cpp
  INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/protobuf"
  LINK_LIBS_ENCLAVE status.proto.enclave demo.proto.enclave protobuf.enclave
  LINK_LIBS_VIRTUAL status.proto.virtual demo.proto.virtual protobuf.virtual
  LINK_LIBS_SNP status.proto.virtual demo.proto.virtual protobuf.snp
)

# Generate an ephemeral signing key
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/signing_key.pem
  COMMAND openssl genrsa -out ${CMAKE_CURRENT_BINARY_DIR}/signing_key.pem -3
          3072
)
add_custom_target(
  grpc_demo_signing_key ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/signing_key.pem
)

sign_app_library(
  grpc_demo.enclave ${CMAKE_CURRENT_SOURCE_DIR}/oe_sign.conf
  ${CMAKE_CURRENT_BINARY_DIR}/signing_key.pem
)
