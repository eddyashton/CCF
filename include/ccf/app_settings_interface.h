// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/node_subsystem_interface.h"

#include <nlohmann/json.hpp>

namespace ccf
{
  class AppSettings : public ccf::AbstractNodeSubSystem
  {
  public:
    virtual ~AppSettings() = default;

    static char const* get_subsystem_name()
    {
      return "AppSettings";
    }

    // Returns the `app_settings` passed to this node as part of its startup
    // config. This is an arbitrary JSON element, so can contain app-specific
    // configuration values. Note that this may vary between nodes, and cannot
    // be modified after startup. It should generally only be used for settings
    // which must be configured at node startup - if they should be updatable or
    // kept in-sync between nodes, then they should instead be stored in the KV.
    virtual const nlohmann::json& get_app_settings() = 0;
  };
}