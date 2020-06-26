// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "node/historical_queries_interface.h"
#include "node/rpc/notifier_interface.h"
#include "node/rpc/user_frontend.h"

namespace ccfapp
{
  struct AbstractNodeContext
  {
    virtual ~AbstractNodeContext() = default;

    virtual ccf::AbstractNotifier& get_notifier() = 0;
    virtual ccf::historical::AbstractStateCache& get_historical_state() = 0;
  };

  /** To be implemented by the application to be registered by CCF. Should
   * construct an Endpoints instance, installing the app's desired handlers
   * during construction.
   *
   * @param network Access to the network's replicated tables
   * @param context Access to node and host services
   *
   * @return Shared pointer to the application handler instance
   */
  std::shared_ptr<ccf::Endpoints> get_app_endpoints(
    ccf::NetworkTables& network, AbstractNodeContext& context);
}
