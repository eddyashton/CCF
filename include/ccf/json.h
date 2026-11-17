// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/json.h"
#include "ccf/json/serialization.h"

namespace ccf::json
{
  template <typename T>
  nlohmann::json schema()
  {
    return ccf::ds::json::build_schema<T>();
  }
}
