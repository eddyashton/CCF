# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

add_ccf_app(
  grpc_dispatcher
  SRCS ${CMAKE_CURRENT_LIST_DIR}/app/grpc_dispatcher.cpp
)
sign_app_library(
  grpc_dispatcher.enclave ${CMAKE_CURRENT_LIST_DIR}/app/oe_sign.conf
  ${CMAKE_CURRENT_BINARY_DIR}/signing_key.pem
)
