// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "consensus/ledger_enclave_types.h"
#include "kv/store.h"
#include "node/historical_queries_interface.h"
#include "node/history.h"
#include "node/rpc/node_interface.h"

#include <list>
#include <map>
#include <memory>
#include <set>

namespace ccf::historical
{
  class StateCache : public AbstractStateCache
  {
  protected:
    kv::Store& source_store;
    ringbuffer::WriterPtr to_host;

    enum class RequestStage
    {
      Fetching,
      Untrusted,
      Trusted,
    };

    using LedgerEntry = std::vector<uint8_t>;

    struct Request
    {
      RequestStage current_stage = RequestStage::Fetching;

      // After fetching, we count down through previous transactions until we
      // reach a signature. With this we start a history from it, and then we
      // count forward to the next signature, building the history as we go
      std::shared_ptr<ccf::MerkleTreeHistory> history = nullptr;
      consensus::Index next_history_entry = 0;

      StorePtr store = nullptr;
    };

    // These constitute a simple LRU, where only user queries will refresh an
    // entry's priority
    static constexpr size_t MAX_ACTIVE_REQUESTS = 10;
    std::map<consensus::Index, Request> requests;
    std::list<consensus::Index> recent_requests;

    // To trust an index, we currently need to fetch a sequence of entries
    // around it - these aren't user requests, so we don't store them, but we do
    // need to distinguish things-we-asked-for from junk-from-the-host
    std::set<consensus::Index> pending_fetches;

    void request_entry_at(consensus::Index idx)
    {
      // To avoid duplicates, remove index if it was already requested
      recent_requests.remove(idx);

      // Add request to front of list, most recently requested
      recent_requests.emplace_front(idx);

      // Cull old requests
      while (recent_requests.size() > MAX_ACTIVE_REQUESTS)
      {
        const auto old_idx = recent_requests.back();
        recent_requests.pop_back();
        requests.erase(old_idx);
      }

      // Try to insert new request
      const auto ib = requests.insert(std::make_pair(idx, Request{}));
      if (ib.second)
      {
        // If its a new request, begin fetching it
        fetch_entry_at(idx);
      }
    }

    void fetch_entry_at(consensus::Index idx)
    {
      const auto it =
        std::find(pending_fetches.begin(), pending_fetches.end(), idx);
      if (it != pending_fetches.end())
      {
        // Already fetching this index
        return;
      }

      RINGBUFFER_WRITE_MESSAGE(
        consensus::ledger_get,
        to_host,
        idx,
        consensus::LedgerRequestPurpose::HistoricalQuery);
      pending_fetches.insert(idx);
    }

    std::optional<ccf::Signature> get_signature(const StorePtr& sig_store)
    {
      kv::Tx tx;
      auto sig_table = sig_store->get<ccf::Signatures>(ccf::Tables::SIGNATURES);
      if (sig_table == nullptr)
      {
        throw std::logic_error(
          "Missing signatures table in signature transaction");
      }

      auto sig_view = tx.get_view(*sig_table);
      return sig_view->get(0);
    }

    std::optional<ccf::NodeInfo> get_node_info(ccf::NodeId node_id)
    {
      // Current solution: Use current state of Nodes table from real store.
      // This only works while entries are never deleted from this table, and
      // makes no check that the signing node was active at the point it
      // produced this signature
      kv::Tx tx;

      auto nodes_table = source_store.get<ccf::Nodes>(ccf::Tables::NODES);
      if (nodes_table == nullptr)
      {
        throw std::logic_error("Missing nodes table");
      }

      auto nodes_view = tx.get_view(*nodes_table);
      return nodes_view->get(node_id);
    }

    void handle_signature_transaction(
      consensus::Index sig_idx, const StorePtr& sig_store)
    {
      LOG_INFO_FMT("Considering signature at {}", sig_idx);
      const auto sig = get_signature(sig_store);
      if (!sig.has_value())
      {
        throw std::logic_error(
          "Missing signature value in signature transaction");
      }

      // Build tree from signature
      auto sig_tree = std::make_shared<ccf::MerkleTreeHistory>(sig->tree);
      const auto real_root = sig_tree->get_root();
      if (real_root != sig->root)
      {
        throw std::logic_error("Invalid signature: invalid root");
      }

      const auto node_info = get_node_info(sig->node);
      if (!node_info.has_value())
      {
        throw std::logic_error(fmt::format(
          "Signature {} claims it was produced by node {}: This node is "
          "unknown",
          sig_idx,
          sig->node));
      }

      auto verifier = tls::make_verifier(node_info->cert);
      const auto verified = verifier->verify_hash(
        real_root.h.data(),
        real_root.h.size(),
        sig->sig.data(),
        sig->sig.size());
      if (!verified)
      {
        throw std::logic_error(
          fmt::format("Signature at {} is invalid", sig_idx));
      }

      auto it = requests.begin();
      while (it != requests.end())
      {
        auto& request = it->second;
        const auto& untrusted_idx = it->first;

        LOG_INFO_FMT(
          "Considering request {} ({})", untrusted_idx, request.current_stage);

        if (request.current_stage != RequestStage::Untrusted)
        {
          // Already trusted or still fetching, skip it
          ++it;
          continue;
        }

        if (sig_idx <= untrusted_idx)
        {
          LOG_INFO_FMT("Maybe start of history?");

          // This signature can be used as the starting point to build a history
          // for this Untrusted entry
          if (request.history == nullptr)
          {
            LOG_INFO_FMT("Yes!");
            request.history =
              std::make_shared<ccf::MerkleTreeHistory>(sig_tree->serialise());
            request.next_history_entry = sig_idx + 1;
            fetch_entry_at(request.next_history_entry);
          }
          // TODO: How do we decide if _this_ tree is preferable? We could keep
          // the later tree, but it's not obvious how we determine that...
        }

        if (untrusted_idx <= sig_idx)
        {
          LOG_INFO_FMT("Maybe proof?");
          // This signature may allow us to trust this Untrusted entry
          const auto& untrusted_store = request.store;

          if (request.history == nullptr)
          {
            LOG_INFO_FMT("No history");
            // We haven't started building a history yet
            ++it;
            continue;
          }

          // Use try-catch to get a receipt from the history we have built
          ccf::Receipt receipt;
          try
          {
            receipt = request.history->get_receipt(untrusted_idx);
            LOG_INFO_FMT(
              "From history, constructed a receipt for {} (considering "
              "signature at {})",
              sig_idx,
              untrusted_idx);
          }
          catch (const std::exception& e)
          {
            LOG_INFO_FMT("No receipt");
            // We're not able to build a valid receipt yet
            ++it;
            continue;
          }

          const auto verified = sig_tree->verify(receipt);
          if (verified)
          {
            // Move store from untrusted to trusted
            LOG_DEBUG_FMT(
              "Now trusting {} due to signature at {}", untrusted_idx, sig_idx);
            request.current_stage = RequestStage::Trusted;
            ++it;
            continue;
          }
          else
          {
            // TODO: Does this mean we have a junk entry? Its possible that the
            // signature is just too new, and can't prove anything about such an
            // old entry...
            LOG_DEBUG_FMT(
              "Signature at {} did not prove {}", sig_idx, untrusted_idx);
            ++it;
            continue;
          }
        }
        ++it;
      }
    }

    void deserialise_ledger_entry(
      consensus::Index idx, const LedgerEntry& entry)
    {
      StorePtr store = std::make_shared<kv::Store>(false);
      store->set_encryptor(source_store.get_encryptor());
      store->clone_schema(source_store);

      const auto deserialise_result = store->deserialise_views(entry);
      switch (deserialise_result)
      {
        case kv::DeserialiseSuccess::FAILED:
        {
          throw std::logic_error("Deserialise failed!");
          break;
        }
        case kv::DeserialiseSuccess::PASS:
        case kv::DeserialiseSuccess::PASS_SIGNATURE:
        {
          LOG_DEBUG_FMT(
            "Processed transaction at {} ({})", idx, deserialise_result);

          auto request_it = requests.find(idx);
          if (request_it != requests.end())
          {
            auto& request = request_it->second;
            if (request.current_stage == RequestStage::Fetching)
            {
              // We were looking for this entry. Store the produced store
              request.current_stage = RequestStage::Untrusted;
              request.store = store;
            }
            else
            {
              LOG_DEBUG_FMT(
                "Not fetching ledger entry {}: already have it in stage {}",
                request_it->first,
                request.current_stage);
            }
          }

          // Add hash of this entry to any Untrusted entries which are looking
          // for it
          for (auto it = requests.begin(); it != requests.end(); ++it)
          {
            auto& request = it->second;
            if (request.history != nullptr && request.next_history_entry == idx)
            {
              LOG_INFO_FMT(
                "Appending hash of {} to history for {}", idx, it->first);
              crypto::Sha256Hash entry_hash(entry);
              request.history->append(entry_hash);
              request.next_history_entry += 1;
              fetch_entry_at(request.next_history_entry);
            }
          }

          if (deserialise_result == kv::DeserialiseSuccess::PASS_SIGNATURE)
          {
            // This looks like a valid signature - try to use this signature to
            // move some stores from untrusted to trusted
            handle_signature_transaction(idx, store);
          }

          // TODO: Actually check these things
          if (idx > 0)
          {
            // There is an Untrusted store at a higher idx still looking for a
            // base signature - keep looking backwards
            LOG_INFO_FMT("Fetching previous idx: {}", idx - 1);
            fetch_entry_at(idx - 1);
          }
          else
          {
            LOG_FAIL_FMT("Uh oh, hit 0");
          }
          break;
        }
        default:
        {
          throw std::logic_error("Unexpected deserialise result");
        }
      }
    }

  public:
    StateCache(kv::Store& store, const ringbuffer::WriterPtr& host_writer) :
      source_store(store),
      to_host(host_writer)
    {}

    StorePtr get_store_at(consensus::Index idx) override
    {
      const auto it = requests.find(idx);
      if (it == requests.end())
      {
        // Treat this as a hint and start fetching it
        request_entry_at(idx);

        return nullptr;
      }

      if (it->second.current_stage == RequestStage::Trusted)
      {
        // Have this store and trust it
        return it->second.store;
      }

      // Still fetching this store or don't trust it yet
      return nullptr;
    }

    bool handle_ledger_entry(consensus::Index idx, const LedgerEntry& data)
    {
      const auto it =
        std::find(pending_fetches.begin(), pending_fetches.end(), idx);
      if (it == pending_fetches.end())
      {
        // Unexpected entry - ignore it?
        return false;
      }

      pending_fetches.erase(it);

      try
      {
        deserialise_ledger_entry(idx, data);
      }
      catch (const std::exception& e)
      {
        LOG_FAIL_FMT("Unable to deserialise entry {}: {}", idx, e.what());
        return false;
      }

      return true;
    }

    void handle_no_entry(consensus::Index idx)
    {
      const auto request_it = requests.find(idx);
      if (request_it != requests.end())
      {
        if (request_it->second.current_stage == RequestStage::Fetching)
        {
          requests.erase(request_it);
        }
      }

      const auto it =
        std::find(pending_fetches.begin(), pending_fetches.end(), idx);
      if (it != pending_fetches.end())
      {
        // The host failed or refused to give this entry. Currently just forget
        // about it - don't have a mechanism for remembering this failure and
        // reporting it to users.
        pending_fetches.erase(it);
      }
    }
  };
}