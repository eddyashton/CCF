// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "node/historical_queries/store_details.h"
#include "node/history.h"
#include "node/tx_receipt_impl.h"
#include "service/tables/node_signature.h"

#include <map>
#include <set>

namespace ccf::historical
{
  // A single entry tracked by a Request — either user-requested or a
  // "supporting" entry needed for receipt construction. The flag
  // distinguishes the two; all other code treats them uniformly.
  struct TrackedEntry
  {
    StoreDetailsPtr details;
    bool user_requested = false;
  };

  using TrackedEntries = std::map<ccf::SeqNo, TrackedEntry>;

  // Result of attempting to build receipts for a set of requested seqnos.
  struct ReceiptBuildResult
  {
    // Seqnos that need to be fetched to find signatures for receipt
    // construction. These are gaps discovered during the forward scan.
    std::set<SeqNo> supporting_seqnos;
  };

  // Helper functions for signature/tree extraction from a store.
  // These read from the KV tables written by the consensus signature process.

  static std::optional<ccf::PrimarySignature> get_signature(
    const ccf::kv::StorePtr& sig_store)
  {
    auto tx = sig_store->create_read_only_tx();
    auto* signatures = tx.ro<ccf::Signatures>(ccf::Tables::SIGNATURES);
    return signatures->get();
  }

  static std::optional<ccf::CoseSignature> get_cose_signature(
    const ccf::kv::StorePtr& sig_store)
  {
    auto tx = sig_store->create_read_only_tx();
    auto* signatures = tx.ro<ccf::CoseSignatures>(ccf::Tables::COSE_SIGNATURES);
    return signatures->get();
  }

  static std::optional<std::vector<uint8_t>> get_tree(
    const ccf::kv::StorePtr& sig_store)
  {
    auto tx = sig_store->create_read_only_tx();
    auto* tree =
      tx.ro<ccf::SerialisedMerkleTree>(ccf::Tables::SERIALISED_MERKLE_TREE);
    return tree->get();
  }

  // Given a signature transaction's StoreDetails, walk backwards through
  // tracked_stores and assign receipts (Merkle proofs) to any entries covered
  // by that signature's tree.
  //
  // If should_fill is set, returns true iff that specific seqno was covered.
  // Mutates StoreDetails::receipt and ::transaction_id on covered entries.
  static bool fill_receipts_from_signature(
    const StoreDetailsPtr& sig_details,
    const TrackedEntries& tracked_stores,
    std::optional<ccf::SeqNo> should_fill = std::nullopt)
  {
    const auto sig = get_signature(sig_details->store);
    if (!sig.has_value())
    {
      return false;
    }
    const auto cose_sig = get_cose_signature(sig_details->store);
    const auto serialised_tree = get_tree(sig_details->store);
    if (!serialised_tree.has_value())
    {
      return false;
    }
    ccf::MerkleTreeHistory tree(serialised_tree.value());

    auto sig_lower_bound_it =
      tracked_stores.lower_bound(sig_details->transaction_id.seqno);

    if (sig_lower_bound_it != tracked_stores.begin())
    {
      auto search_rit = std::reverse_iterator(sig_lower_bound_it);
      while (search_rit != tracked_stores.rend())
      {
        auto seqno = search_rit->first;
        if (tree.in_range(seqno))
        {
          auto& details = search_rit->second.details;
          if (details != nullptr && details->store != nullptr)
          {
            auto proof = tree.get_proof(seqno);
            details->transaction_id = {sig->view, seqno};
            details->receipt = std::make_shared<TxReceiptImpl>(
              sig->sig,
              cose_sig,
              proof.get_root(),
              proof.get_path(),
              sig->node,
              sig->cert,
              details->entry_digest,
              details->get_commit_evidence(),
              details->claims_digest);

            if (should_fill.has_value() && seqno == *should_fill)
            {
              should_fill.reset();
            }
          }

          ++search_rit;
        }
        else
        {
          break;
        }
      }
    }

    return !should_fill.has_value();
  }

  // For a single target seqno, attempt to find its covering signature by
  // scanning forward through all_stores. If the signature is found and already
  // fetched, assigns receipts. Otherwise returns the set of seqnos that need
  // to be fetched (gaps in the forward scan).
  //
  // Does NOT mutate all_stores or any bookkeeping state — only sets receipts
  // on the StoreDetails objects which are shared data.
  static ReceiptBuildResult build_receipt_for_seqno(
    ccf::SeqNo target_seqno,
    const AllRequestedStores& all_stores,
    const TrackedEntries& tracked_stores)
  {
    ReceiptBuildResult result;

    auto target_it = all_stores.find(target_seqno);
    auto target_details =
      target_it == all_stores.end() ? nullptr : target_it->second.lock();
    if (target_details == nullptr || target_details->store == nullptr)
    {
      return result;
    }

    if (target_details->receipt != nullptr)
    {
      return result;
    }

    if (target_details->is_signature)
    {
      fill_receipts_from_signature(target_details, tracked_stores);
      return result;
    }

    // Not a signature — scan forward for the next signature or gap
    auto next_seqno = target_seqno + 1;
    while (true)
    {
      auto all_it = all_stores.find(next_seqno);
      auto details =
        all_it == all_stores.end() ? nullptr : all_it->second.lock();

      if (details == nullptr)
      {
        // Gap — this seqno needs fetching. It might be the signature.
        result.supporting_seqnos.insert(next_seqno);
        return result;
      }

      if (details->store == nullptr)
      {
        // Entry exists but not yet fetched — record as needed
        result.supporting_seqnos.insert(next_seqno);
        return result;
      }

      if (details->is_signature)
      {
        const auto filled_this =
          fill_receipts_from_signature(details, tracked_stores, target_seqno);

        if (
          !filled_this &&
          tracked_stores.find(target_seqno) != tracked_stores.end())
        {
          throw std::logic_error(fmt::format(
            "Unexpected: Found a signature at {}, and contiguous range "
            "of transactions from {}, yet signature does not cover "
            "this seqno!",
            next_seqno,
            target_seqno));
        }

        return result;
      }

      // Normal fetched transaction — continue scanning
      ++next_seqno;
    }
  }
}
