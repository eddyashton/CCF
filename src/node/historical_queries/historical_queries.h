// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/historical_queries_interface.h"
#include "ccf/pal/locking.h"
#include "consensus/ledger_enclave_types.h"
#include "ds/ccf_assert.h"
#include "kv/store.h"
#include "node/encryptor.h"
#include "node/historical_queries/entry_store.h"
#include "node/historical_queries/receipt_builder.h"
#include "node/history.h"
#include "node/ledger_secrets.h"
#include "node/rpc/node_interface.h"
#include "node/tx_receipt_impl.h"
#include "service/tables/node_signature.h"

#include <list>
#include <map>
#include <memory>
#include <set>

#ifdef ENABLE_HISTORICAL_VERBOSE_LOGGING
#  define HISTORICAL_LOG(...) LOG_INFO_FMT(__VA_ARGS__)
#else
#  define HISTORICAL_LOG(...)
#endif

namespace ccf::historical
{
  static constexpr auto slow_fetch_threshold = std::chrono::milliseconds(1000);
  static constexpr size_t soft_to_raw_ratio{5};

  class StateCacheCore
  {
  protected:
    ccf::kv::Store& source_store;
    std::shared_ptr<ccf::LedgerSecrets> source_ledger_secrets;
    ringbuffer::WriterPtr to_host;

    std::shared_ptr<ccf::LedgerSecrets> historical_ledger_secrets;
    std::shared_ptr<ccf::NodeEncryptor> historical_encryptor;

    // whether to keep all the writes so that we can build a diff later
    bool track_deletes_on_missing_keys_v = false;

    using LedgerEntry = std::vector<uint8_t>;

    void update_earliest_known_ledger_secret()
    {
      if (earliest_secret_.secret == nullptr)
      {
        // Haven't worked out earliest known secret yet - work it out now
        if (historical_ledger_secrets->is_empty())
        {
          CCF_ASSERT_FMT(
            !source_ledger_secrets->is_empty(),
            "Source ledger secrets are empty");
          earliest_secret_ = source_ledger_secrets->get_first();
        }
        else
        {
          auto tx = source_store.create_read_only_tx();
          CCF_ASSERT_FMT(
            historical_ledger_secrets->get_latest(tx).first <
              source_ledger_secrets->get_first().first,
            "Historical ledger secrets are not older than main ledger secrets");

          earliest_secret_ = historical_ledger_secrets->get_first();
        }
      }
    }

    struct VersionedSecret
    {
      ccf::SeqNo valid_from = {};
      ccf::LedgerSecretPtr secret = nullptr;

      VersionedSecret() = default;
      // NB: Can't use VersionedLedgerSecret directly because the first element
      // is const, so the whole thing is non-copyable
      VersionedSecret(const ccf::VersionedLedgerSecret& vls) :
        valid_from(vls.first),
        secret(vls.second)
      {}
    };

    VersionedSecret earliest_secret_;
    StoreDetailsPtr next_secret_fetch_handle = nullptr;

    struct Request
    {
      LedgerEntryTracker& entry_store;

      // Single map of ALL entries this request is interested in.
      // Each entry is either user-requested or supporting (for receipts).
      TrackedEntries tracked_stores;
      std::chrono::milliseconds time_to_expiry{};

      bool include_receipts = false;

      // Only set when recovering ledger secrets
      std::optional<ccf::SeqNo> awaiting_ledger_secrets = std::nullopt;

      Request(LedgerEntryTracker& entry_store_) : entry_store(entry_store_) {}

      [[nodiscard]] StoreDetailsPtr get_store_details(ccf::SeqNo seqno) const
      {
        auto it = entry_store.all_stores.find(seqno);
        if (it != entry_store.all_stores.end())
        {
          return it->second.lock();
        }

        return nullptr;
      }

      [[nodiscard]] ccf::SeqNo first_requested_seqno() const
      {
        for (const auto& [seq, entry] : tracked_stores)
        {
          if (entry.user_requested)
          {
            return seq;
          }
        }
        return {};
      }

      // Returns seqnos removed and added from tracked_stores.
      // The caller should remove_ref for removed and add_ref for added.
      std::pair<std::vector<SeqNo>, std::vector<SeqNo>> adjust_ranges(
        const SeqNoCollection& new_seqnos,
        bool should_include_receipts,
        SeqNo earliest_ledger_secret_seqno)
      {
        std::vector<SeqNo> removed{};
        std::vector<SeqNo> added{};

        std::set<SeqNo> new_user_set(new_seqnos.begin(), new_seqnos.end());

        // Remove tracked entries that are no longer needed.
        // Keep supporting entries with fetched data when receipts are
        // requested — they may be part of a receipt chain.
        {
          auto it = tracked_stores.begin();
          while (it != tracked_stores.end())
          {
            const bool still_wanted = new_user_set.count(it->first) > 0;
            const bool has_data = it->second.details != nullptr &&
              it->second.details->store != nullptr;

            if (!still_wanted && !(should_include_receipts && has_data))
            {
              removed.push_back(it->first);
              it = tracked_stores.erase(it);
            }
            else
            {
              it->second.user_requested = still_wanted;
              ++it;
            }
          }
        }

        // Add new user-requested entries. If the seqno is earlier than
        // the earliest known ledger secret, store it with a nullptr
        // details (will be populated once secrets are recovered).
        {
          bool any_too_early = false;
          for (auto seq : new_seqnos)
          {
            if (tracked_stores.count(seq) > 0)
            {
              continue; // Already tracked — flag was set above
            }

            StoreDetailsPtr details = nullptr;
            if (!(seq < earliest_ledger_secret_seqno || any_too_early))
            {
              details = entry_store.get_or_create(seq);
            }
            else
            {
              any_too_early = true;
            }

            tracked_stores[seq] = {details, true};
            added.push_back(seq);
          }
        }

        include_receipts = should_include_receipts;

        // Build receipts and discover supporting entries.
        if (should_include_receipts)
        {
          for (auto seqno : new_seqnos)
          {
            auto build_result = build_receipt_for_seqno(
              seqno, entry_store.all_stores, tracked_stores);

            for (auto seq : build_result.supporting_seqnos)
            {
              if (tracked_stores.count(seq) == 0)
              {
                tracked_stores[seq] = {entry_store.get_or_create(seq), false};
                added.push_back(seq);
              }
            }
          }
        }

        return {removed, added};
      }
    };

    // Guard all access to internal state with this lock
    ccf::pal::Mutex requests_lock;

    // Track all things currently requested by external callers
    std::map<CompoundHandle, Request> requests;

    // Owns the global entry store (fetched/in-flight ledger entries),
    // their weak-pointer sharing map, and ref-counted size tracking.
    LedgerEntryTracker entry_store;

    ExpiryDuration default_expiry_duration = std::chrono::seconds(1800);

    // These two combine into an effective O(log(N)) lookup/add/remove by
    // handle.
    std::list<CompoundHandle> lru_requests;
    std::map<CompoundHandle, std::list<CompoundHandle>::iterator> lru_lookup;

    CacheSize soft_store_cache_limit{std::numeric_limits<size_t>::max()};
    CacheSize soft_store_cache_limit_raw =
      soft_store_cache_limit / soft_to_raw_ratio;

    void lru_promote(CompoundHandle handle)
    {
      auto it = lru_lookup.find(handle);
      if (it != lru_lookup.end())
      {
        lru_requests.erase(it->second);
        it->second = lru_requests.insert(lru_requests.begin(), handle);
      }
      else
      {
        lru_lookup[handle] = lru_requests.insert(lru_requests.begin(), handle);
        entry_store.add_refs_for(handle, requests.at(handle).tracked_stores);
      }
    }

    void lru_shrink_to_fit(size_t threshold)
    {
      while (entry_store.estimated_size() > threshold)
      {
        if (lru_requests.empty())
        {
          LOG_FAIL_FMT(
            "LRU shrink to {} requested but cache is already empty", threshold);
          return;
        }

        const auto handle = lru_requests.back();
        LOG_DEBUG_FMT(
          "Cache size shrinking (reached {} / {}). Dropping {}",
          entry_store.estimated_size(),
          threshold,
          handle);

        entry_store.remove_refs_for(handle, requests.at(handle).tracked_stores);
        lru_lookup.erase(handle);

        requests.erase(handle);
        lru_requests.pop_back();
      }
    }

    void lru_evict(CompoundHandle handle)
    {
      auto it = lru_lookup.find(handle);
      if (it != lru_lookup.end())
      {
        entry_store.remove_refs_for(handle, requests.at(handle).tracked_stores);
        lru_requests.erase(it->second);
        lru_lookup.erase(it);
      }
    }

    void fetch_entry_at(ccf::SeqNo seqno)
    {
      fetch_entries_range(seqno, seqno);
    }

    void fetch_entries_range(ccf::SeqNo from, ccf::SeqNo to)
    {
      LOG_TRACE_FMT("fetch_entries_range({}, {})", from, to);

      RINGBUFFER_WRITE_MESSAGE(
        ::consensus::ledger_get_range,
        to_host,
        static_cast<::consensus::Index>(from),
        static_cast<::consensus::Index>(to),
        ::consensus::LedgerRequestPurpose::HistoricalQuery);
    }

    std::optional<ccf::SeqNo> fetch_supporting_secret_if_needed(
      ccf::SeqNo seqno)
    {
      auto [earliest_ledger_secret_seqno, earliest_ledger_secret] =
        earliest_secret_;
      CCF_ASSERT_FMT(
        earliest_ledger_secret != nullptr,
        "Can't fetch without knowing earliest");

      const auto too_early = seqno < earliest_ledger_secret_seqno;

      auto previous_secret_stored_version =
        earliest_ledger_secret->previous_secret_stored_version;
      const auto is_next_secret =
        previous_secret_stored_version.value_or(0) == seqno;

      if (too_early || is_next_secret)
      {
        // Still need more secrets, fetch the next
        if (!previous_secret_stored_version.has_value())
        {
          throw std::logic_error(fmt::format(
            "Earliest known ledger secret at {} has no earlier secret stored "
            "version ({})",
            earliest_ledger_secret_seqno,
            seqno));
        }

        const auto seqno_to_fetch = previous_secret_stored_version.value();
        LOG_TRACE_FMT(
          "Requesting historical entry at {} but first known ledger "
          "secret is applicable from {}",
          seqno,
          earliest_ledger_secret_seqno);

        auto it = entry_store.all_stores.find(seqno_to_fetch);
        auto details =
          it == entry_store.all_stores.end() ? nullptr : it->second.lock();
        if (details == nullptr)
        {
          LOG_TRACE_FMT("Requesting older secret at {} now", seqno_to_fetch);
          details = entry_store.get_or_create(seqno_to_fetch);
          fetch_entry_at(seqno_to_fetch);
        }

        next_secret_fetch_handle = details;

        if (too_early)
        {
          return seqno_to_fetch;
        }
      }

      return std::nullopt;
    }

    void process_deserialised_store(
      const StoreDetailsPtr& details,
      const ccf::kv::StorePtr& store,
      const ccf::crypto::Sha256Hash& entry_digest,
      ccf::SeqNo seqno,
      bool is_signature,
      ccf::ClaimsDigest&& claims_digest,
      bool has_commit_evidence)
    {
      // Deserialisation includes a GCM integrity check, so all entries
      // have been verified by the time we get here.
      details->current_stage = StoreStage::Trusted;
      details->has_commit_evidence = has_commit_evidence;

      details->entry_digest = entry_digest;
      if (!claims_digest.empty())
      {
        details->claims_digest = std::move(claims_digest);
      }

      CCF_ASSERT_FMT(
        details->store == nullptr,
        "Cache already has store for seqno {}",
        seqno);
      details->store = store;

      details->is_signature = is_signature;
      if (is_signature)
      {
        // Construct a signature receipt.
        // We do this whether it was requested or not, because we have all
        // the state to do so already, and it's simpler than constructing
        // the receipt _later_ for an already-fetched signature
        // transaction.
        const auto sig = get_signature(details->store);
        const auto cose_sig = get_cose_signature(details->store);
        if (sig.has_value())
        {
          details->transaction_id = {sig->view, sig->seqno};
          details->receipt = std::make_shared<TxReceiptImpl>(
            sig->sig, cose_sig, sig->root.h, nullptr, sig->node, sig->cert);
        }
      }

      auto request_it = requests.begin();
      while (request_it != requests.end())
      {
        auto& [handle, request] = *request_it;

        // If this request was still waiting for a ledger secret, and this is
        // that secret
        if (
          request.awaiting_ledger_secrets.has_value() &&
          request.awaiting_ledger_secrets.value() == seqno)
        {
          LOG_TRACE_FMT(
            "{} is a ledger secret seqno this request was waiting for", seqno);

          request.awaiting_ledger_secrets =
            fetch_supporting_secret_if_needed(request.first_requested_seqno());
          if (!request.awaiting_ledger_secrets.has_value())
          {
            // Newly have all required secrets - begin fetching the actual
            // entries. Note this is adding them to `all_stores`, from where
            // they'll be requested on the next tick.
            for (auto& [store_seqno, entry] : request.tracked_stores)
            {
              if (!entry.user_requested || entry.details != nullptr)
              {
                continue;
              }
              entry.details = entry_store.get_or_create(store_seqno);
            }
          }

          // In either case, done with this request, try the next
          ++request_it;
          continue;
        }

        if (request.include_receipts)
        {
          const bool seqno_in_this_request =
            request.tracked_stores.find(seqno) != request.tracked_stores.end();
          if (seqno_in_this_request)
          {
            // Run the builder to discover gaps and ensure supporting
            // entries are tracked so the host will fetch them.
            auto build_result = build_receipt_for_seqno(
              seqno, entry_store.all_stores, request.tracked_stores);

            for (auto seq : build_result.supporting_seqnos)
            {
              if (request.tracked_stores.count(seq) == 0)
              {
                request.tracked_stores[seq] = {
                  entry_store.get_or_create(seq), false};
                entry_store.add_ref(seq, handle);
              }
            }
          }
        }

        ++request_it;
      }
    }

    bool handle_encrypted_past_ledger_secret(
      const ccf::kv::StorePtr& store, LedgerSecretPtr encrypting_secret)
    {
      // Read encrypted secrets from store
      auto tx = store->create_read_only_tx();
      auto* encrypted_past_ledger_secret_handle =
        tx.ro<ccf::EncryptedLedgerSecretsInfo>(
          ccf::Tables::ENCRYPTED_PAST_LEDGER_SECRET);
      if (encrypted_past_ledger_secret_handle == nullptr)
      {
        return false;
      }

      auto encrypted_past_ledger_secret =
        encrypted_past_ledger_secret_handle->get();
      if (!encrypted_past_ledger_secret.has_value())
      {
        return false;
      }

      // Construct description and decrypted secret
      auto previous_ledger_secret =
        encrypted_past_ledger_secret->previous_ledger_secret;
      if (!previous_ledger_secret.has_value())
      {
        // The only write to this table that should not contain a previous
        // secret is the initial service open
        CCF_ASSERT_FMT(
          encrypted_past_ledger_secret->next_version.has_value() &&
            encrypted_past_ledger_secret->next_version.value() == 1,
          "Write to ledger secrets table at {} should contain a next_version "
          "of 1",
          store->current_version());
        return true;
      }

      if (previous_ledger_secret->version >= earliest_secret_.valid_from)
      {
        LOG_INFO_FMT(
          "Skipping redundant ledger secret with version of {} when the "
          "earliest known secret is from {}",
          previous_ledger_secret->version,
          earliest_secret_.valid_from);
        return true;
      }

      auto recovered_ledger_secret = std::make_shared<LedgerSecret>(
        ccf::decrypt_previous_ledger_secret_raw(
          encrypting_secret, previous_ledger_secret->encrypted_data),
        previous_ledger_secret->previous_secret_stored_version);

      // Add recovered secret to historical secrets
      historical_ledger_secrets->set_secret(
        previous_ledger_secret->version, std::move(recovered_ledger_secret));

      // Update earliest_secret
      CCF_ASSERT(
        previous_ledger_secret->version < earliest_secret_.valid_from, "");
      earliest_secret_ = historical_ledger_secrets->get_first();

      return true;
    }

    SeqNoCollection collection_from_single_range(
      ccf::SeqNo start_seqno, ccf::SeqNo end_seqno)
    {
      if (end_seqno < start_seqno)
      {
        throw std::logic_error(fmt::format(
          "Invalid range for historical query: end {} is before start {}",
          end_seqno,
          start_seqno));
      }

      SeqNoCollection c(start_seqno, end_seqno - start_seqno);
      return c;
    }

    std::vector<StatePtr> get_states_internal(
      const CompoundHandle& handle,
      const SeqNoCollection& seqnos,
      ExpiryDuration seconds_until_expiry,
      bool include_receipts)
    {
      if (seqnos.empty())
      {
        throw std::logic_error(
          "Invalid range for historical query: Cannot request empty range");
      }

      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);

      const auto ms_until_expiry =
        std::chrono::duration_cast<std::chrono::milliseconds>(
          seconds_until_expiry);

      auto it = requests.find(handle);
      if (it == requests.end())
      {
        // This is a new handle - insert a newly created Request for it
        it = requests.emplace_hint(it, handle, Request(entry_store));
        HISTORICAL_LOG("First time I've seen handle {}", handle);
      }

      lru_promote(handle);

      Request& request = it->second;

      update_earliest_known_ledger_secret();

      // Update this Request to represent the currently requested ranges
      HISTORICAL_LOG(
        "Adjusting handle {} to cover {} seqnos starting at {} "
        "(include_receipts={})",
        handle,
        seqnos.size(),
        *seqnos.begin(),
        include_receipts);
      auto [removed, added] = request.adjust_ranges(
        seqnos, include_receipts, earliest_secret_.valid_from);

      for (auto seq : removed)
      {
        entry_store.remove_ref(seq, handle);
      }
      for (auto seq : added)
      {
        entry_store.add_ref(seq, handle);
      }

      // If the earliest target entry cannot be deserialised with the earliest
      // known ledger secret, record the target seqno and begin fetching the
      // previous historical ledger secret.
      request.awaiting_ledger_secrets =
        fetch_supporting_secret_if_needed(request.first_requested_seqno());

      // Reset the expiry timer as this has just been requested
      request.time_to_expiry = ms_until_expiry;

      std::vector<StatePtr> trusted_states;

      for (auto seqno : seqnos)
      {
        auto target_details = request.get_store_details(seqno);
        if (
          target_details != nullptr &&
          target_details->current_stage == StoreStage::Trusted &&
          (!request.include_receipts || target_details->receipt != nullptr))
        {
          // Have this store, associated txid and receipt and trust it - add
          // it to return list
          StatePtr state = std::make_shared<State>(
            target_details->store,
            target_details->receipt,
            target_details->transaction_id);
          trusted_states.push_back(state);
        }
        else
        {
          // Still fetching this store or don't trust it yet, so range is
          // incomplete - return empty vector
          return {};
        }
      }

      return trusted_states;
    }

    // Used when we received an invalid entry, to drop any requests which were
    // asking for it
    void delete_all_interested_requests(ccf::SeqNo seqno)
    {
      auto request_it = requests.begin();
      while (request_it != requests.end())
      {
        if (request_it->second.get_store_details(seqno) != nullptr)
        {
          lru_evict(request_it->first);
          request_it = requests.erase(request_it);
        }
        else
        {
          ++request_it;
        }
      }
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> states_to_stores(
      const std::vector<StatePtr>& states)
    {
      std::vector<ccf::kv::ReadOnlyStorePtr> stores;
      stores.reserve(states.size());
      for (const auto& state : states)
      {
        stores.push_back(state->store);
      }
      return stores;
    }

  public:
    StateCacheCore(
      ccf::kv::Store& store,
      const std::shared_ptr<ccf::LedgerSecrets>& secrets,
      ringbuffer::WriterPtr host_writer) :
      source_store(store),
      source_ledger_secrets(secrets),
      to_host(std::move(host_writer)),
      historical_ledger_secrets(std::make_shared<ccf::LedgerSecrets>()),
      historical_encryptor(
        std::make_shared<ccf::NodeEncryptor>(historical_ledger_secrets))
    {}

    ccf::kv::ReadOnlyStorePtr get_store_at(
      const CompoundHandle& handle,
      ccf::SeqNo seqno,
      ExpiryDuration seconds_until_expiry)
    {
      auto range = get_store_range(handle, seqno, seqno, seconds_until_expiry);
      if (range.empty())
      {
        return nullptr;
      }

      return range[0];
    }

    ccf::kv::ReadOnlyStorePtr get_store_at(
      const CompoundHandle& handle, ccf::SeqNo seqno)
    {
      return get_store_at(handle, seqno, default_expiry_duration);
    }

    StatePtr get_state_at(
      const CompoundHandle& handle,
      ccf::SeqNo seqno,
      ExpiryDuration seconds_until_expiry)
    {
      auto range = get_state_range(handle, seqno, seqno, seconds_until_expiry);
      if (range.empty())
      {
        return nullptr;
      }

      return range[0];
    }

    StatePtr get_state_at(const CompoundHandle& handle, ccf::SeqNo seqno)
    {
      return get_state_at(handle, seqno, default_expiry_duration);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_store_range(
      const CompoundHandle& handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno,
      ExpiryDuration seconds_until_expiry)
    {
      return states_to_stores(get_states_internal(
        handle,
        collection_from_single_range(start_seqno, end_seqno),
        seconds_until_expiry,
        false));
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_store_range(
      const CompoundHandle& handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno)
    {
      return get_store_range(
        handle, start_seqno, end_seqno, default_expiry_duration);
    }

    std::vector<StatePtr> get_state_range(
      const CompoundHandle& handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno,
      ExpiryDuration seconds_until_expiry)
    {
      return get_states_internal(
        handle,
        collection_from_single_range(start_seqno, end_seqno),
        seconds_until_expiry,
        true);
    }

    std::vector<StatePtr> get_state_range(
      const CompoundHandle& handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno)
    {
      return get_state_range(
        handle, start_seqno, end_seqno, default_expiry_duration);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_stores_for(
      const CompoundHandle& handle,
      const SeqNoCollection& seqnos,
      ExpiryDuration seconds_until_expiry)
    {
      return states_to_stores(
        get_states_internal(handle, seqnos, seconds_until_expiry, false));
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_stores_for(
      const CompoundHandle& handle, const SeqNoCollection& seqnos)
    {
      return get_stores_for(handle, seqnos, default_expiry_duration);
    }

    std::vector<StatePtr> get_states_for(
      const CompoundHandle& handle,
      const SeqNoCollection& seqnos,
      ExpiryDuration seconds_until_expiry)
    {
      if (seqnos.empty())
      {
        throw std::runtime_error("Cannot request empty range");
      }
      return get_states_internal(handle, seqnos, seconds_until_expiry, true);
    }

    std::vector<StatePtr> get_states_for(
      const CompoundHandle& handle, const SeqNoCollection& seqnos)
    {
      return get_states_for(handle, seqnos, default_expiry_duration);
    }

    void set_default_expiry_duration(ExpiryDuration duration)
    {
      default_expiry_duration = duration;
    }

    void set_soft_cache_limit(CacheSize cache_limit)
    {
      soft_store_cache_limit = cache_limit;
      soft_store_cache_limit_raw = soft_store_cache_limit / soft_to_raw_ratio;
    }

    void track_deletes_on_missing_keys(bool track)
    {
      track_deletes_on_missing_keys_v = track;
    }

    bool drop_cached_states(const CompoundHandle& handle)
    {
      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);
      lru_evict(handle);
      const auto erased_count = requests.erase(handle);
      return erased_count > 0;
    }

    bool handle_ledger_entry(ccf::SeqNo seqno, const std::vector<uint8_t>& data)
    {
      return handle_ledger_entry(seqno, data.data(), data.size());
    }

    bool handle_ledger_entry(ccf::SeqNo seqno, const uint8_t* data, size_t size)
    {
      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);
      const auto it = entry_store.all_stores.find(seqno);
      auto details =
        it == entry_store.all_stores.end() ? nullptr : it->second.lock();
      if (details == nullptr || details->current_stage != StoreStage::Fetching)
      {
        // Unexpected entry, we already have it or weren't asking for it -
        // ignore this resubmission
        return false;
      }

      ccf::kv::ApplyResult deserialise_result = ccf::kv::ApplyResult::FAIL;
      ccf::ClaimsDigest claims_digest;
      bool has_commit_evidence = false;
      auto store = deserialise_ledger_entry(
        seqno,
        data,
        size,
        deserialise_result,
        claims_digest,
        has_commit_evidence);

      if (deserialise_result == ccf::kv::ApplyResult::FAIL)
      {
        return false;
      }

      {
        // Confirm this entry is from a precursor of the current state, and not
        // a fork
        const auto tx_id = store->current_txid();
        if (tx_id.seqno != seqno)
        {
          LOG_FAIL_FMT(
            "Corrupt ledger entry received - claims to be {} but is actually "
            "{}.{}",
            seqno,
            tx_id.view,
            tx_id.seqno);
          return false;
        }

        auto consensus = source_store.get_consensus();
        if (consensus == nullptr)
        {
          LOG_FAIL_FMT("No consensus on source store");
          return false;
        }

        const auto actual_view = consensus->get_view(seqno);
        if (actual_view != tx_id.view)
        {
          LOG_FAIL_FMT(
            "Ledger entry comes from fork - contains {}.{} but this service "
            "expected {}.{}",
            tx_id.view,
            tx_id.seqno,
            actual_view,
            seqno);
          return false;
        }
      }

      const auto is_signature =
        deserialise_result == ccf::kv::ApplyResult::PASS_SIGNATURE;

      update_earliest_known_ledger_secret();

      auto [valid_from, secret] = earliest_secret_;

      if (secret != nullptr)
      {
        const auto& prev_version = secret->previous_secret_stored_version;
        if (prev_version.has_value() && *prev_version == seqno)
        {
          HISTORICAL_LOG(
            "Handling past ledger secret. Current earliest is valid from {}, "
            "now "
            "processing secret stored at {}",
            valid_from,
            seqno);
          handle_encrypted_past_ledger_secret(store, secret);
          next_secret_fetch_handle = nullptr;
        }
      }

      HISTORICAL_LOG(
        "Processing historical store at {} ({})",
        seqno,
        (size_t)deserialise_result);
      const auto entry_digest = ccf::crypto::Sha256Hash({data, size});
      process_deserialised_store(
        details,
        store,
        entry_digest,
        seqno,
        is_signature,
        std::move(claims_digest),
        has_commit_evidence);

      entry_store.update_raw_size(seqno, size);
      return true;
    }

    bool handle_ledger_entries(
      ccf::SeqNo from_seqno, ccf::SeqNo to_seqno, const LedgerEntry& data)
    {
      return handle_ledger_entries(
        from_seqno, to_seqno, data.data(), data.size());
    }

    bool handle_ledger_entries(
      ccf::SeqNo from_seqno,
      ccf::SeqNo to_seqno,
      const uint8_t* data,
      size_t size)
    {
      LOG_TRACE_FMT("handle_ledger_entries({}, {})", from_seqno, to_seqno);

      auto seqno = from_seqno;
      bool all_accepted = true;
      while (size > 0)
      {
        const auto header =
          serialized::peek<ccf::kv::SerialisedEntryHeader>(data, size);
        const auto whole_size =
          header.size + ccf::kv::serialised_entry_header_size;
        all_accepted &= handle_ledger_entry(seqno, data, whole_size);
        data += whole_size;
        size -= whole_size;
        ++seqno;
      }

      if (seqno != to_seqno + 1)
      {
        LOG_FAIL_FMT(
          "Claimed ledger entries: [{}, {}), actual [{}, {}]",
          from_seqno,
          to_seqno,
          from_seqno,
          seqno);
      }

      return all_accepted;
    }

    void handle_no_entry(ccf::SeqNo seqno)
    {
      handle_no_entry_range(seqno, seqno);
    }

    void handle_no_entry_range(ccf::SeqNo from_seqno, ccf::SeqNo to_seqno)
    {
      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);

      LOG_TRACE_FMT("handle_no_entry_range({}, {})", from_seqno, to_seqno);

      for (auto seqno = from_seqno; seqno <= to_seqno; ++seqno)
      {
        // The host failed or refused to give this entry. Currently just
        // forget about it and drop any requests which were looking for it -
        // don't have a mechanism for remembering this failure and reporting it
        // to users.
        const auto fetches_it = entry_store.all_stores.find(seqno);
        if (fetches_it != entry_store.all_stores.end())
        {
          delete_all_interested_requests(seqno);

          entry_store.all_stores.erase(fetches_it);
        }
      }
    }

    ccf::kv::StorePtr deserialise_ledger_entry(
      ccf::SeqNo seqno,
      const uint8_t* data,
      size_t size,
      ccf::kv::ApplyResult& result,
      ccf::ClaimsDigest& claims_digest,
      bool& has_commit_evidence)
    {
      // Create a new store and try to deserialise this entry into it
      ccf::kv::StorePtr store = std::make_shared<ccf::kv::Store>(
        false /* Do not start from very first seqno */,
        true /* Make use of historical secrets */);

      // If this is older than the node's currently known ledger secrets, use
      // the historical encryptor (which should have older secrets)
      if (seqno < source_ledger_secrets->get_first().first)
      {
        store->set_encryptor(historical_encryptor);
      }
      else
      {
        store->set_encryptor(source_store.get_encryptor());
      }

      try
      {
        // Encrypted ledger secrets are deserialised in public-only mode. Their
        // Merkle tree integrity is not verified: even if the recovered ledger
        // secret was bogus, the deserialisation of subsequent ledger entries
        // would fail.
        bool public_only = false;
        for (const auto& [_, request] : requests)
        {
          const auto& als = request.awaiting_ledger_secrets;
          if (als.has_value() && als.value() == seqno)
          {
            public_only = true;
            break;
          }
        }

        auto exec = store->deserialize({data, data + size}, public_only);
        if (exec == nullptr)
        {
          result = ccf::kv::ApplyResult::FAIL;
          return nullptr;
        }

        result = exec->apply(track_deletes_on_missing_keys_v);
        claims_digest = std::move(exec->consume_claims_digest());

        auto commit_evidence_digest =
          std::move(exec->consume_commit_evidence_digest());
        has_commit_evidence = commit_evidence_digest.has_value();
      }
      catch (const std::exception& e)
      {
        LOG_FAIL_FMT(
          "Exception while attempting to deserialise entry {}: {}",
          seqno,
          e.what());
        result = ccf::kv::ApplyResult::FAIL;
      }

      return store;
    }

    size_t get_estimated_store_cache_size()
    {
      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);
      return entry_store.estimated_size();
    }

    void tick(const std::chrono::milliseconds& elapsed_ms)
    {
      std::lock_guard<ccf::pal::Mutex> guard(requests_lock);

      {
        auto it = requests.begin();
        while (it != requests.end())
        {
          auto& request = it->second;
          if (elapsed_ms >= request.time_to_expiry)
          {
            LOG_DEBUG_FMT(
              "Dropping expired historical query with handle {}", it->first);
            lru_evict(it->first);
            it = requests.erase(it);
          }
          else
          {
            request.time_to_expiry -= elapsed_ms;
            ++it;
          }
        }
      }

      lru_shrink_to_fit(soft_store_cache_limit_raw);

      {
        auto it = entry_store.all_stores.begin();
        std::optional<std::pair<ccf::SeqNo, ccf::SeqNo>> range_to_request =
          std::nullopt;
        while (it != entry_store.all_stores.end())
        {
          auto details = it->second.lock();
          if (details == nullptr)
          {
            it = entry_store.all_stores.erase(it);
          }
          else
          {
            if (details->current_stage == StoreStage::Fetching)
            {
              details->time_until_fetch -= elapsed_ms;
              if (details->time_until_fetch.count() <= 0)
              {
                details->time_until_fetch = slow_fetch_threshold;

                const auto seqno = it->first;
                if (auto range_val = range_to_request; range_val.has_value())
                {
                  auto range = range_val.value();
                  if (range.second + 1 == seqno)
                  {
                    range.second = seqno;
                    range_to_request = range;
                  }
                  else
                  {
                    // Submit fetch for previously tracked range
                    fetch_entries_range(range.first, range.second);
                    // Track new range
                    range_to_request = std::make_pair(seqno, seqno);
                  }
                }
                else
                {
                  // Track new range
                  range_to_request = std::make_pair(seqno, seqno);
                }
              }
            }

            ++it;
          }
        }

        if (auto range_val = range_to_request; range_val.has_value())
        {
          // Submit fetch for final tracked range
          auto range = range_val.value();
          fetch_entries_range(range.first, range.second);
        }
      }
    }
  };
}
