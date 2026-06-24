// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <memory>

namespace ccf
{
  struct NetworkState;
  struct AbstractNodeContext;
  class RpcFrontend;

  // Each RPC frontend pulls in a large amount of endpoint-registration and
  // schema-generation machinery (member_frontend.h, node_frontend.h and
  // user_frontend.h are each >20s to parse and instantiate). Constructing them
  // through these factories - each defined in its own translation unit - keeps
  // that cost out of the single enclave entry translation unit (main.cpp) and
  // lets the three frontends compile in parallel.
  std::unique_ptr<RpcFrontend> make_member_frontend(
    NetworkState& network, AbstractNodeContext& context);
  std::unique_ptr<RpcFrontend> make_user_frontend(
    NetworkState& network, AbstractNodeContext& context);
  std::unique_ptr<RpcFrontend> make_node_frontend(
    NetworkState& network, AbstractNodeContext& context);
}
