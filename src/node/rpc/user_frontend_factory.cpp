// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ccf/app_interface.h"
#include "node/rpc/frontend_factory.h"
#include "node/rpc/user_frontend.h"

namespace ccf
{
  std::unique_ptr<RpcFrontend> make_user_frontend(
    NetworkState& network, AbstractNodeContext& context)
  {
    return std::make_unique<UserRpcFrontend>(
      network, make_user_endpoints(context), context);
  }
}
