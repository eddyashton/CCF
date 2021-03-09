// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "common_endpoint_registry.h"
#include "node/client_signatures.h"
#include "node/rpc/frontend.h"

namespace ccf
{
  class UserEndpointRegistry : public CommonEndpointRegistry
  {
  public:
    UserEndpointRegistry(ccf::AbstractNodeState& node) :
      CommonEndpointRegistry(
        get_actor_prefix(ActorsType::users), node)
    {}
  };

  class SimpleUserRpcFrontend : public RpcFrontend
  {
  protected:
    UserEndpointRegistry common_handlers;

  public:
    SimpleUserRpcFrontend(
      kv::Store& tables, ccf::AbstractNodeState& node_state) :
      RpcFrontend(tables, common_handlers),
      common_handlers(node_state)
    {}

    // Forward these methods so that apps can write foo(...); rather than
    // endpoints.foo(...);
    template <typename... Ts>
    ccf::EndpointRegistry::Endpoint make_endpoint(Ts&&... ts)
    {
      return endpoints.make_endpoint(std::forward<Ts>(ts)...);
    }

    template <typename... Ts>
    ccf::EndpointRegistry::Endpoint make_read_only_endpoint(Ts&&... ts)
    {
      return endpoints.make_read_only_endpoint(std::forward<Ts>(ts)...);
    }

    template <typename... Ts>
    ccf::EndpointRegistry::Endpoint make_command_endpoint(Ts&&... ts)
    {
      return endpoints.make_command_endpoint(std::forward<Ts>(ts)...);
    }
  };
}
