// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "kv/committable_tx.h"
#include "ccf/endpoint_context.h"

namespace ccf
{
  // Implementation of EndpointContext, private to the framework (not visible
  // to user apps)
  struct EndpointContextImpl : public ccf::endpoints::EndpointContext
  {
    std::unique_ptr<kv::CommittableTx> owned_tx = nullptr;

    EndpointContextImpl(
      const std::shared_ptr<ccf::RpcContext>& r,
      std::unique_ptr<kv::CommittableTx> t) :
      ccf::endpoints::EndpointContext(r, *t),
      owned_tx(std::move(t))
    {}
  };
}
