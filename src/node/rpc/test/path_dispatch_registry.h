// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/endpoint_registry.h"

struct RoutingTable
{
  using Path = std::string_view;
  using Data = ccf::endpoints::EndpointDefinitionPtr;

  struct SubTable
  {
    using Component = std::string_view;
    using Components = std::vector<Component>;

    std::unordered_map<Components, Data> paths_to_data;

    void insert(const Components& components, Data data)
    {
      paths_to_data[components] = data;
    }

    Data find(const Components& components)
    {
      auto it = paths_to_data.find(components);
      if (it == paths_to_data.end())
      {
        return nullptr;
      }

      return it->second;
    }
  };

  std::unordered_map<size_t, SubTable> all_endpoints;

  void insert(const Path& path, Data data)
  {
    const auto components = ccf::nonstd::split(path, "/");
    all_endpoints[components.size()].insert(components, data);
  }

  Data find(const Path& path)
  {
    const auto components = ccf::nonstd::split(path, "/");
    const auto it = all_endpoints.find(components.size());
    if (it == all_endpoints.end())
    {
      return nullptr;
    }

    return it->second.find(components);
  }
};

class NewRegistry : public ccf::endpoints::EndpointRegistry
{
private:
  RoutingTable routing_table;

  using Endpoint = ccf::endpoints::Endpoint;

public:
  using ccf::endpoints::EndpointRegistry::EndpointRegistry;

  void install(ccf::endpoints::Endpoint& endpoint) override
  {
    routing_table.insert(
      endpoint.dispatch.uri_path, std::make_shared<Endpoint>(endpoint));
  }

  ccf::endpoints::EndpointDefinitionPtr find_endpoint(
    ccf::kv::Tx&, ccf::RpcContext& rpc_ctx) override
  {
    return routing_table.find(rpc_ctx.get_method());
  }
};
