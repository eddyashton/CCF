// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/rpc/tx_status.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace ccf;

TEST_CASE("normal flow")
{
  constexpr auto target_view = 3;
  constexpr auto target_seqno = 10;

  // A tx id is unknown locally
  CHECK(get_tx_status(3, 10, 0, 1, 0) == TxStatus::TxUnknown);

  // The tx id remains unknown while a node makes progress
  CHECK(get_tx_status(3, 10, 0, 1, 1) == TxStatus::TxUnknown);
  CHECK(get_tx_status(3, 10, 0, 1, 2) == TxStatus::TxUnknown);
  CHECK(get_tx_status(3, 10, 0, 2, 3) == TxStatus::TxUnknown);
  CHECK(get_tx_status(3, 10, 0, 2, 4) == TxStatus::TxUnknown);
  CHECK(get_tx_status(3, 10, 0, 3, 5) == TxStatus::TxUnknown);
  CHECK(get_tx_status(3, 10, 0, 3, 6) == TxStatus::TxUnknown);

  // Eventually the tx id becomes known locally
  CHECK(get_tx_status(3, 10, 3, 3, 6) == TxStatus::Replicating);

  // The tx id remains known while a node makes progress
  CHECK(get_tx_status(3, 10, 3, 3, 7) == TxStatus::Replicating);
  CHECK(get_tx_status(3, 10, 3, 3, 8) == TxStatus::Replicating);

  // Until either...
  {
    // ...the tx is globally committed...
    CHECK(get_tx_status(3, 10, 3, 3, 9) == TxStatus::Replicating);
    CHECK(get_tx_status(3, 10, 3, 3, 10) == TxStatus::Committed);

    // The tx id remains permanently committed
    CHECK(get_tx_status(3, 10, 3, 3, 11) == TxStatus::Committed);
    CHECK(get_tx_status(3, 10, 3, 3, 12) == TxStatus::Committed);
    CHECK(get_tx_status(3, 10, 3, 3, 13) == TxStatus::Committed);
    CHECK(get_tx_status(3, 10, 3, 4, 14) == TxStatus::Committed);
    CHECK(get_tx_status(3, 10, 3, 5, 15) == TxStatus::Committed);
  }
  // ...or...
  {
    // ...an election occurs, and the local tx is rolled back
    CHECK(get_tx_status(3, 10, 0, 4, 9) == TxStatus::NotCommitted);

    // The tx id can never be committed
    CHECK(get_tx_status(3, 10, 4, 4, 10) == TxStatus::NotCommitted);
    CHECK(get_tx_status(3, 10, 4, 4, 11) == TxStatus::NotCommitted);
    CHECK(get_tx_status(3, 10, 4, 4, 12) == TxStatus::NotCommitted);
    CHECK(get_tx_status(3, 10, 4, 4, 13) == TxStatus::NotCommitted);
    CHECK(get_tx_status(3, 10, 4, 4, 14) == TxStatus::NotCommitted);
    CHECK(get_tx_status(3, 10, 4, 5, 15) == TxStatus::NotCommitted);
  }
}