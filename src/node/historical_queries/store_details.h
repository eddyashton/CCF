// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/hex.h"
#include "ccf/tx_id.h"
#include "kv/store.h"
#include "node/tx_receipt_impl.h"

#include <chrono>
#include <map>
#include <memory>

namespace ccf::historical
{
  enum class StoreStage : uint8_t
  {
    Fetching,
    Trusted,
  };

  struct StoreDetails
  {
    std::chrono::milliseconds time_until_fetch = {};
    StoreStage current_stage = StoreStage::Fetching;
    ccf::crypto::Sha256Hash entry_digest;
    ccf::ClaimsDigest claims_digest;
    ccf::kv::StorePtr store = nullptr;
    bool is_signature = false;
    TxReceiptImplPtr receipt = nullptr;
    ccf::TxID transaction_id;
    bool has_commit_evidence = false;

    ccf::crypto::HashBytes get_commit_nonce()
    {
      if (store != nullptr)
      {
        auto e = store->get_encryptor();
        return e->get_commit_nonce(
          {transaction_id.view, transaction_id.seqno}, true);
      }

      throw std::logic_error("Store pointer not set");
    }

    std::optional<std::string> get_commit_evidence()
    {
      if (has_commit_evidence)
      {
        return fmt::format(
          "ce:{}.{}:{}",
          transaction_id.view,
          transaction_id.seqno,
          ds::to_hex(get_commit_nonce()));
      }

      return std::nullopt;
    }
  };

  using StoreDetailsPtr = std::shared_ptr<StoreDetails>;
  using WeakStoreDetailsPtr = std::weak_ptr<StoreDetails>;
  using AllRequestedStores = std::map<ccf::SeqNo, WeakStoreDetailsPtr>;
}
