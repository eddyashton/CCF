// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/app_settings_interface.h"

namespace ccf
{
  class AppSettingsHolder : public ccf::AppSettings
  {
  protected:
    const nlohmann::json app_settings;

  public:
    AppSettingsHolder(nlohmann::json&& as) : app_settings(std::move(as)) {}

    virtual const nlohmann::json& get_app_settings()
    {
      return app_settings;
    }
  };
}