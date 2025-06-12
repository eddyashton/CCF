// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/endpoint_registry.h"

#include <r3/r3.h>

class R3Registry : public ccf::endpoints::EndpointRegistry
{
private:
  R3Node* root = r3_tree_create(100);

  using Endpoint = ccf::endpoints::Endpoint;

  std::set<std::shared_ptr<Endpoint>> endpoints;

  bool dirty = true;

  int rest_verb_to_r3_method(const ccf::RESTVerb& verb)
  {
    return METHOD_GET;
  }

public:
  using ccf::endpoints::EndpointRegistry::EndpointRegistry;

  virtual ~R3Registry()
  {
    r3_tree_free(root);
  }

  void install(ccf::endpoints::Endpoint& endpoint) override
  {
    auto ptr = std::make_shared<Endpoint>(endpoint);

    fmt::print(
      "Inserting route {} {} into R3\n",
      endpoint.dispatch.verb.c_str(),
      endpoint.dispatch.uri_path);

    r3_tree_insert_routel(
      root,
      rest_verb_to_r3_method(endpoint.dispatch.verb),
      endpoint.dispatch.uri_path.c_str(),
      endpoint.dispatch.uri_path.size(),
      ptr.get());

    endpoints.insert(ptr);

    dirty = true;
  }

  ccf::endpoints::EndpointDefinitionPtr find_endpoint(
    ccf::kv::Tx&, ccf::RpcContext& rpc_ctx) override
  {
    if (dirty)
    {
      char* errstr = nullptr;
      int err = r3_tree_compile(root, &errstr);
      if (err != 0)
      {
        std::string err(errstr);
        free(errstr);
        throw std::logic_error(fmt::format("Error compiling R3 tree: {}", err));
      }

      dirty = false;
    }

    fmt::print(
      "Matching {} {} via R3\n",
      rpc_ctx.get_request_verb().c_str(),
      rpc_ctx.get_method());

    match_entry* entry = match_entry_createl(
      rpc_ctx.get_method().c_str(), rpc_ctx.get_method().size());
    entry->request_method = rest_verb_to_r3_method(rpc_ctx.get_request_verb());

    R3Node* matched = r3_tree_match_entry(root, entry);

    if (matched != nullptr)
    {
      auto ret =
        static_cast<ccf::endpoints::EndpointDefinition*>(matched->data);
      match_entry_free(entry);

      return ret->shared_from_this();
    }

    return nullptr;
  }
};
