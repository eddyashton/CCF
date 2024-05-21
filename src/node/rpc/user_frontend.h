// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "node/rpc/frontend.h"
#include "node/network_state.h"
#include "ccf/node_context.h"
#include "ccf/app_interface.h"

namespace ccf
{
  class UserRpcFrontend : public RpcFrontend
  {
  protected:
    std::unique_ptr<ccf::endpoints::EndpointRegistry> endpoints;

  public:
    UserRpcFrontend(
      NetworkState& network,
      std::unique_ptr<ccf::endpoints::EndpointRegistry>&& endpoints_,
      ccfapp::AbstractNodeContext& context_) :
      RpcFrontend(*network.tables, *endpoints_, context_),
      endpoints(std::move(endpoints_))
    {}
  };
}
