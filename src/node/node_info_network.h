// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#pragma once

#include "ds/json.h"

#include <string>

namespace ccf
{
  struct NodeInfoNetwork
  {
    std::string rpchost;
    std::string pubhost;
    std::string nodehost;
    std::string nodeport;
    std::string rpcport;
    std::string pubport;
  };
  DECLARE_JSON_TYPE(NodeInfoNetwork);
  DECLARE_JSON_REQUIRED_FIELDS(
    NodeInfoNetwork, rpchost, pubhost, nodehost, nodeport, rpcport, pubport);
}