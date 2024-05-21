// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "service/network_tables.h"
#include "ledger_secrets.h"
#include "identity.h"

namespace ccf
{
  struct NetworkState : public NetworkTables
  {
    std::unique_ptr<NetworkIdentity> identity;
    std::shared_ptr<LedgerSecrets> ledger_secrets;

    NetworkState() = default;
  };
}