// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "node/rpc/frontend_factory.h"
#include "node/rpc/node_frontend.h"

namespace ccf
{
  std::unique_ptr<RpcFrontend> make_node_frontend(
    NetworkState& network, AbstractNodeContext& context)
  {
    return std::make_unique<NodeRpcFrontend>(network, context);
  }
}
