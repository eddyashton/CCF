// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/ccf_exception.h"
#include "kv_serialiser.h"
#include "kv_types.h"
#include "map.h"
#include "snapshot.h"
#include "tx.h"
#include "view_containers.h"

#include <fmt/format.h>

namespace kv
{
  class Store : public AbstractStore
  {
  private:
    // All collections of Map must be ordered so that we lock their contained
    // maps in a stable order. The order here is by map name
    using Maps = std::map<std::string, std::unique_ptr<AbstractMap>>;
    Maps maps;

    using DynamicMaps = std::
      map<std::string, std::pair<kv::Version, std::shared_ptr<AbstractMap>>>;
    DynamicMaps dynamic_maps;

    std::shared_ptr<Consensus> consensus = nullptr;
    std::shared_ptr<TxHistory> history = nullptr;
    std::shared_ptr<AbstractTxEncryptor> encryptor = nullptr;
    Version version = 0;
    Version compacted = 0;
    Term term = 0;

    SpinLock maps_lock;
    SpinLock version_lock;

    std::unordered_map<Version, std::pair<PendingTx, bool>> pending_txs;
    Version last_replicated = 0;
    Version last_committable = 0;
    Version rollback_count = 0;
    kv::ReplicateType replicate_type = kv::ReplicateType::ALL;
    std::unordered_set<std::string> replicated_tables;

    // Generally we will only accept deserialised views if they are contiguous -
    // at Version N we reject everything but N+1. The exception is when a Store
    // is used for historical queries, where it may deserialise arbitrary
    // transactions. In this case the Store is a useful container for a set of
    // Tables, but its versioning invariants are ignored.
    const bool strict_versions = true;

    DeserialiseSuccess commit_deserialised(OrderedViews& views, Version& v)
    {
      auto c = apply_views(this, views, [v]() { return v; });
      if (!c.has_value())
      {
        LOG_FAIL_FMT("Failed to commit deserialised Tx at version {}", v);
        return DeserialiseSuccess::FAILED;
      }
      {
        std::lock_guard<SpinLock> vguard(version_lock);
        version = v;
        last_replicated = version;
      }
      return DeserialiseSuccess::PASS;
    }

    bool has_map_internal(const std::string& name)
    {
      auto search = maps.find(name);
      if (search != maps.end())
        return true;

      auto dynamic_search = dynamic_maps.find(name);
      if (dynamic_search != dynamic_maps.end())
        return true;

      return false;
    }

  public:
    void clone_schema(Store& from)
    {
      std::lock_guard<SpinLock> mguard(maps_lock);

      if ((maps.size() != 0) || (version != 0))
        throw std::logic_error("Cannot clone schema on a non-empty store");

      for (auto& [name, map] : from.maps)
      {
        maps[name] = std::unique_ptr<AbstractMap>(map->clone());
      }
    }

    Store(bool strict_versions_ = true) : strict_versions(strict_versions_) {}

    Store(
      const ReplicateType& replicate_type_,
      const std::unordered_set<std::string>& replicated_tables_) :
      replicate_type(replicate_type_),
      replicated_tables(replicated_tables_)
    {}

    Store(std::shared_ptr<Consensus> consensus_) : consensus(consensus_) {}

    Store(const Store& that) = delete;

    std::shared_ptr<Consensus> get_consensus() override
    {
      return consensus;
    }

    void set_consensus(std::shared_ptr<Consensus> consensus_)
    {
      consensus = consensus_;
    }

    std::shared_ptr<TxHistory> get_history() override
    {
      return history;
    }

    void set_history(std::shared_ptr<TxHistory> history_)
    {
      history = history_;
    }

    void set_encryptor(std::shared_ptr<AbstractTxEncryptor> encryptor_)
    {
      encryptor = encryptor_;
    }

    std::shared_ptr<AbstractTxEncryptor> get_encryptor() override
    {
      return encryptor;
    }

    template <class K, class V>
    Map<K, V>* get(const std::string& name)
    {
      return get<Map<K, V>>(name);
    }

    /** Get Map by name
     *
     * @param name Map name
     *
     * @return Map
     */
    template <class M>
    M* get(const std::string& name)
    {
      std::lock_guard<SpinLock> mguard(maps_lock);

      auto search = maps.find(name);
      if (search != maps.end())
      {
        auto result = dynamic_cast<M*>(search->second.get());

        if (result == nullptr)
          return nullptr;

        return result;
      }

      auto dynamic_search = dynamic_maps.find(name);
      if (dynamic_search != dynamic_maps.end())
      {
        auto result = dynamic_cast<M*>(dynamic_search->second.second.get());

        if (result == nullptr)
          return nullptr;

        return result;
      }

      return nullptr;
    }

    /** Get Map by type and name
     *
     * Using type and name of other Map, retrieve the equivalent Map from this
     * Store
     *
     * @param other Other map
     *
     * @return Map
     */
    template <class M>
    M* get(const M& other)
    {
      return get<M>(other.get_name());
    }

    /** Create a Map
     *
     * Note this call will throw a logic_error if a map by that name already
     * exists.
     *
     * @param name Map name
     * @param global_hook Handler to execute on global commit
     *
     * @return Newly created Map
     */
    template <class K, class V>
    Map<K, V>& create(
      std::string name,
      SecurityDomain security_domain = kv::SecurityDomain::PRIVATE)
    {
      return create<Map<K, V>>(name, security_domain);
    }

    /** Create a Map
     *
     * Note this call will throw a logic_error if a map by that name already
     * exists.
     *
     * @param name Map name
     * @param global_hook Handler to execute on global commit
     *
     * @return Newly created Map
     */
    template <class M>
    M& create(
      std::string name,
      SecurityDomain security_domain = kv::SecurityDomain::PRIVATE)
    {
      std::lock_guard<SpinLock> mguard(maps_lock);

      if (has_map_internal(name))
        throw std::logic_error("Map already exists");

      auto replicated = true;
      if (replicate_type == kv::ReplicateType::NONE)
      {
        replicated = false;
      }
      else if (replicate_type == kv::ReplicateType::SOME)
      {
        if (replicated_tables.find(name) == replicated_tables.end())
        {
          replicated = false;
        }
      }

      auto result = new M(name, security_domain, replicated);
      maps[name] = std::unique_ptr<AbstractMap>(result);
      return *result;
    }

    // TODO: Docs
    void add_dynamic_map(Version v, const std::shared_ptr<AbstractMap>& map) override
    {
      //TODO: Expects to _be_ locked
      //std::lock_guard<SpinLock> mguard(maps_lock);

      const auto name = map->get_name();

      if (has_map_internal(name))
        throw std::logic_error("Map already exists");

      dynamic_maps[name] = std::make_pair(v, map);
    }

    bool has_map(const std::string& map_name) override
    {
      // TODO: Can't lock this, because we're already locked in commit...
      // std::lock_guard<SpinLock> mguard(maps_lock);
      return has_map_internal(map_name);
    }

    std::vector<uint8_t> serialise_snapshot(Version v) override
    {
      CCF_ASSERT_FMT(
        v >= commit_version(),
        "Cannot snapshot at version {} which is earlier than committed "
        "version {} ",
        v,
        commit_version());

      CCF_ASSERT_FMT(
        v <= current_version(),
        "Cannot snapshot at version {} which is later than current "
        "version {} ",
        v,
        current_version());

      StoreSnapshot snapshot;

      {
        std::lock_guard<SpinLock> mguard(maps_lock);

        for (auto& map : maps)
        {
          map.second->lock();
        }

        for (auto& map : maps)
        {
          snapshot.add_map_snapshot(map.second->snapshot(v));
        }

        for (auto& map : maps)
        {
          map.second->unlock();
        }
      }

      auto e = get_encryptor();
      KvStoreSerialiser serialiser(e, v, true);

      return snapshot.serialise(serialiser);
    }

    DeserialiseSuccess deserialise_snapshot(
      const std::vector<uint8_t>& data) override
    {
      auto e = get_encryptor();
      auto d = KvStoreDeserialiser(e);

      auto v_ = d.init(data.data(), data.size());
      if (!v_.has_value())
      {
        LOG_FAIL_FMT("Initialisation of deserialise object failed");
        return DeserialiseSuccess::FAILED;
      }
      auto v = v_.value();

      std::lock_guard<SpinLock> mguard(maps_lock);

      for (auto& map : maps)
      {
        map.second->lock();
      }

      bool success = true;
      for (auto r = d.start_map(); r.has_value(); r = d.start_map())
      {
        const auto map_name = r.value();

        auto search = maps.find(map_name);
        if (search == maps.end())
        {
          LOG_FAIL_FMT("Failed to deserialise snapshot at version {}", v);
          LOG_DEBUG_FMT("No such map in store {}", map_name);
          success = false;
          break;
        }

        auto map_version = d.deserialise_entry_version();
        auto map_snapshot = d.deserialise_raw();

        search->second->apply_snapshot(map_version, map_snapshot);
      }

      for (auto& map : maps)
      {
        map.second->unlock();
      }

      if (!success)
      {
        return DeserialiseSuccess::FAILED;
      }

      if (!d.end())
      {
        LOG_FAIL_FMT("Unexpected content in snapshot at version {}", v);
        success = false;
      }

      {
        std::lock_guard<SpinLock> vguard(version_lock);
        version = v;
        last_replicated = v;
        last_committable = v;
      }

      return DeserialiseSuccess::PASS;
    }

    void compact(Version v) override
    {
      // This is called when the store will never be rolled back to any
      // state before the specified version.
      // No transactions can be prepared or committed during compaction.
      std::lock_guard<SpinLock> mguard(maps_lock);

      if (v > current_version())
      {
        return;
      }

      for (auto& map : maps)
      {
        map.second->lock();
      }

      for (auto& map : maps)
      {
        map.second->compact(v);
      }

      for (auto& map : maps)
      {
        map.second->unlock();
      }

      {
        std::lock_guard<SpinLock> vguard(version_lock);
        compacted = v;

        auto h = get_history();
        if (h)
        {
          h->compact(v);
        }

        auto e = get_encryptor();
        if (e)
        {
          e->compact(v);
        }
      }

      for (auto& map : maps)
      {
        map.second->post_compact();
      }
    }

    void rollback(Version v, std::optional<Term> t = std::nullopt) override
    {
      // This is called to roll the store back to the state it was in
      // at the specified version.
      // No transactions can be prepared or committed during rollback.
      std::lock_guard<SpinLock> mguard(maps_lock);

      {
        std::lock_guard<SpinLock> vguard(version_lock);
        // The term should always be updated on rollback() when passed
        // regardless of whether version needs to be updated or not
        if (t.has_value())
          term = t.value();
        if (v >= version)
          return;
      }

      if (v < commit_version())
        throw std::logic_error(fmt::format(
          "Attempting rollback to {}, earlier than commit version {}",
          v,
          commit_version()));

      for (auto& map : maps)
        map.second->lock();

      for (auto& map : maps)
        map.second->rollback(v);

      // TODO: Lock dynamic maps? Or perhaps flatten both into a single
      // container?
      auto dynamic_it = dynamic_maps.begin();
      while (dynamic_it != dynamic_maps.end())
      {
        auto& [map_creation_version, map] = dynamic_it->second;
        if (map_creation_version > v)
        {
          dynamic_it = dynamic_maps.erase(dynamic_it);
        }
        else
        {
          map->rollback(v);
          ++dynamic_it;
        }
      }

      for (auto& map : maps)
        map.second->unlock();

      std::lock_guard<SpinLock> vguard(version_lock);
      version = v;
      last_replicated = v;
      last_committable = v;
      rollback_count++;
      pending_txs.clear();
      auto h = get_history();
      if (h)
        h->rollback(v);
      auto e = get_encryptor();
      if (e)
        e->rollback(v);
    }

    void set_term(Term t) override
    {
      std::lock_guard<SpinLock> vguard(version_lock);
      term = t;
    }

    DeserialiseSuccess deserialise_views(
      const std::vector<uint8_t>& data,
      bool public_only = false,
      Term* term_ = nullptr,
      AbstractViewContainer* tx = nullptr)
    {
      // If we pass in a transaction we don't want to commit, just deserialise
      // and put the views into that transaction.
      // Tread carefully here: at the moment passing in a transaction assumes we
      // are using pbft as the consensus and that we are deserialising for
      // playback purposes
      auto commit = (tx == nullptr);

      // This will return FAILED if the serialised transaction is being
      // applied out of order.
      // Processing transactions locally and also deserialising to the
      // same store will result in a store version mismatch and
      // deserialisation will then fail.
      auto e = get_encryptor();

      // create the first deserialiser
      auto d = KvStoreDeserialiser(
        e,
        public_only ? kv::SecurityDomain::PUBLIC :
                      std::optional<kv::SecurityDomain>());

      auto v_ = d.init(data.data(), data.size());
      if (!v_.has_value())
      {
        LOG_FAIL_FMT("Initialisation of deserialise object failed");
        return DeserialiseSuccess::FAILED;
      }
      auto v = v_.value();

      // Throw away any local commits that have not propagated via the
      // consensus.
      rollback(v - 1);

      if (strict_versions)
      {
        // Make sure this is the next transaction.
        auto cv = current_version();
        if (cv != (v - 1))
        {
          LOG_FAIL_FMT(
            "Tried to deserialise {} but current_version is {}", v, cv);
          return DeserialiseSuccess::FAILED;
        }
      }

      // Deserialised transactions express read dependencies as versions,
      // rather than with the actual value read. As a result, they don't
      // need snapshot isolation on the map state, and so do not need to
      // lock all the maps before creating the transaction.
      std::lock_guard<SpinLock> mguard(maps_lock);
      OrderedViews views;

      for (auto r = d.start_map(); r.has_value(); r = d.start_map())
      {
        const auto map_name = r.value();

        auto search = maps.find(map_name);
        if (search == maps.end())
        {
          LOG_FAIL_FMT("Failed to deserialise transaction at version {}", v);
          LOG_DEBUG_FMT("No such map in store {}", map_name);
          return DeserialiseSuccess::FAILED;
        }

        auto view_search = views.find(map_name);
        if (view_search != views.end())
        {
          LOG_FAIL_FMT("Failed to deserialise transaction at version {}", v);
          LOG_DEBUG_FMT("Multiple writes on map {}", map_name);
          return DeserialiseSuccess::FAILED;
        }

        // if we are not committing now then use NoVersion to deserialise
        // otherwise the view will be considered as having a committed
        // version
        auto deserialise_version = (commit ? v : NoVersion);
        auto deserialised_write_set =
          search->second->deserialise(d, deserialise_version);
        if (deserialised_write_set == nullptr)
        {
          LOG_FAIL_FMT(
            "Failed to deserialise transaction at version {}",
            deserialise_version);
          LOG_DEBUG_FMT(
            "Could not deserialise transaction for map {}", map_name);
          return DeserialiseSuccess::FAILED;
        }

        // Take ownership of the produced write set, store it to be committed
        // later
        views[map_name] = {
          search->second.get(),
          std::unique_ptr<AbstractTxView>(deserialised_write_set)};
      }

      if (!d.end())
      {
        LOG_FAIL_FMT("Unexpected content in transaction at version {}", v);
        return DeserialiseSuccess::FAILED;
      }

      auto success = DeserialiseSuccess::PASS;

      if (commit)
      {
        success = commit_deserialised(views, v);
        if (success == DeserialiseSuccess::FAILED)
        {
          return success;
        }

        auto h = get_history();

        auto search = views.find("ccf.signatures");
        if (search != views.end())
        {
          // Transactions containing a signature must only contain
          // a signature and must be verified
          if (views.size() > 1)
          {
            LOG_FAIL_FMT("Failed to deserialize");
            LOG_DEBUG_FMT("Unexpected contents in signature transaction {}", v);
            return DeserialiseSuccess::FAILED;
          }

          if (h)
          {
            if (!h->verify(term_))
            {
              LOG_FAIL_FMT("Failed to deserialize");
              LOG_DEBUG_FMT("Signature in transaction {} failed to verify", v);
              return DeserialiseSuccess::FAILED;
            }
          }
          success = DeserialiseSuccess::PASS_SIGNATURE;
        }

        if (h)
        {
          h->append(data.data(), data.size());
        }
      }
      else
      {
        // Transactions containing a pre prepare or a pbft request should not
        // contain anything else
        if (views.size() > 1)
        {
          LOG_FAIL_FMT("Failed to deserialize");
          LOG_DEBUG_FMT("Unexpected contents in pbft transaction {}", v);
          return DeserialiseSuccess::FAILED;
        }

        if (views.find("ccf.pbft.preprepares") != views.end())
        {
          success = DeserialiseSuccess::PASS_PRE_PREPARE;
        }
        else if (views.find("ccf.pbft.newviews") != views.end())
        {
          success = DeserialiseSuccess::PASS_NEW_VIEW;
        }
        else if (views.find("ccf.pbft.requests") == views.end())
        {
          // we have deserialised an entry that didn't belong to the pbft
          // requests, nor the pbft new views, nor the pbft pre prepares table
          return DeserialiseSuccess::FAILED;
        }
      }

      if (tx)
      {
        tx->set_view_list(views, term);
      }

      return success;
    }

    DeserialiseSuccess deserialise(
      const std::vector<uint8_t>& data,
      bool public_only = false,
      Term* term = nullptr) override
    {
      return deserialise_views(data, public_only, term);
    }

    bool operator==(const Store& that) const
    {
      // Only used for debugging, not thread safe.
      if (version != that.version)
        return false;

      if (maps.size() != that.maps.size())
        return false;

      for (auto it = maps.begin(); it != maps.end(); ++it)
      {
        auto search = that.maps.find(it->first);

        if (search == that.maps.end())
          return false;

        if (*it->second != *search->second)
          return false;
      }

      return true;
    }

    bool operator!=(const Store& that) const
    {
      // Only used for debugging, not thread safe.
      return !(*this == that);
    }

    Version current_version() override
    {
      // Must lock in case the version or term is being incremented.
      std::lock_guard<SpinLock> vguard(version_lock);
      return version;
    }

    TxID current_txid() override
    {
      // Must lock in case the version is being incremented.
      std::lock_guard<SpinLock> vguard(version_lock);
      return {term, version};
    }

    Version commit_version() override
    {
      // Must lock in case the store is being compacted.
      std::lock_guard<SpinLock> vguard(version_lock);
      return compacted;
    }

    CommitSuccess commit(
      const TxID& txid,
      PendingTx&& pending_tx,
      bool globally_committable) override
    {
      auto r = get_consensus();
      if (!r)
        return CommitSuccess::OK;

      LOG_DEBUG_FMT(
        "Store::commit {}{}",
        txid.version,
        (globally_committable ? " globally_committable" : ""));

      BatchVector batch;
      Version previous_last_replicated = 0;
      Version next_last_replicated = 0;
      Version previous_rollback_count = 0;
      kv::Consensus::View replication_view = 0;

      {
        std::lock_guard<SpinLock> vguard(version_lock);
        // This can happen when a transaction started before a view change,
        // but tries to commit after the view change is complete.
        LOG_DEBUG_FMT(
          "Want to commit for term {}, term is {}", txid.term, term);
        if (txid.term != term)
          return CommitSuccess::NO_REPLICATE;

        if (globally_committable && txid.version > last_committable)
          last_committable = txid.version;

        pending_txs.insert(
          {txid.version,
           std::make_pair(std::move(pending_tx), globally_committable)});

        auto h = get_history();
        auto c = get_consensus();

        for (Version offset = 1; true; ++offset)
        {
          auto search = pending_txs.find(last_replicated + offset);
          if (search == pending_txs.end())
            break;

          auto& [pending_tx_, committable_] = search->second;
          auto [success_, reqid, data_] = pending_tx_();
          auto data_shared =
            std::make_shared<std::vector<uint8_t>>(std::move(data_));

          // NB: this cannot happen currently. Regular Tx only make it here if
          // they did succeed, and signatures cannot conflict because they
          // execute in order with a read_version that's version - 1, so even
          // two contiguous signatures are fine
          if (success_ != CommitSuccess::OK)
            LOG_DEBUG_FMT("Failed Tx commit {}", last_replicated + offset);

          if (h)
          {
            h->add_pending(reqid, txid.version, data_shared);
          }

          LOG_DEBUG_FMT(
            "Batching {} ({})", last_replicated + offset, data_shared->size());

          batch.emplace_back(
            last_replicated + offset, data_shared, committable_);
          pending_txs.erase(search);
        }

        if (batch.size() == 0)
          return CommitSuccess::OK;

        previous_rollback_count = rollback_count;
        previous_last_replicated = last_replicated;
        next_last_replicated = last_replicated + batch.size();

        replication_view = term;
      }

      if (r->replicate(batch, replication_view))
      {
        std::lock_guard<SpinLock> vguard(version_lock);
        if (
          last_replicated == previous_last_replicated &&
          previous_rollback_count == rollback_count)
        {
          last_replicated = next_last_replicated;
        }
        return CommitSuccess::OK;
      }
      else
      {
        LOG_DEBUG_FMT("Failed to replicate");
        return CommitSuccess::NO_REPLICATE;
      }
    }

    void lock() override
    {
      maps_lock.lock();
    }

    void unlock() override
    {
      maps_lock.unlock();
    }

    Version next_version() override
    {
      std::lock_guard<SpinLock> vguard(version_lock);

      // Get the next global version. If we would go negative, wrap to 0.
      ++version;

      if (version < 0)
        version = 0;

      return version;
    }

    TxID next_txid() override
    {
      std::lock_guard<SpinLock> vguard(version_lock);

      // Get the next global version. If we would go negative, wrap to 0.
      ++version;
      if (version < 0)
        version = 0;

      return {term, version};
    }

    size_t commit_gap() override
    {
      std::lock_guard<SpinLock> vguard(version_lock);
      return version - last_committable;
    }

    void clear()
    {
      std::lock_guard<SpinLock> mguard(maps_lock);

      // This deletes the entire content of all maps in the store.
      for (auto& map : maps)
        map.second->lock();

      for (auto& map : maps)
        map.second->clear();

      for (auto& map : maps)
        map.second->unlock();

      {
        std::lock_guard<SpinLock> vguard(version_lock);
        version = 0;
        compacted = 0;
        last_replicated = 0;
        last_committable = 0;
        rollback_count = 0;
        pending_txs.clear();
      }
    }

    /** This is only safe in very restricted circumstances, and is only
     * meant to be used during catastrophic recovery, between a KV
     * with public-state only and a KV with full state, to swap in the
     * private state from the latter into the former.
     *
     * It's also important to note that swapping in private state
     * may result in previously uncompacted writes becoming effectively
     * compacted, from a user's perspective (they would not be internally
     * compacted until the next compact() however). So it is important to
     * make sure that the private state being swapped in is fully compacted
     * before the swap.
     **/
    void swap_private_maps(Store& store)
    {
      std::lock_guard<SpinLock> this_maps_guard(maps_lock);
      std::lock_guard<SpinLock> other_maps_guard(store.maps_lock);

      using MapEntry = std::tuple<std::string, AbstractMap*, AbstractMap*>;
      std::vector<MapEntry> entries;

      for (auto& [name, map] : maps)
      {
        if (map->get_security_domain() == SecurityDomain::PRIVATE)
        {
          map->lock();
          entries.emplace_back(name, map.get(), nullptr);
        }
      }

      auto entry = entries.begin();
      for (auto& [name, map] : store.maps)
      {
        if (map->get_security_domain() == SecurityDomain::PRIVATE)
        {
          if (entry == entries.end())
            throw std::logic_error(
              "Private map list mismatch during swap, " + name + " not found");

          if (std::get<0>(*entry) != name)
            throw std::logic_error(
              "Private map list mismatch during swap, " + std::get<0>(*entry) +
              " != " + name);

          map->lock();
          std::get<2>(*entry) = map.get();

          ++entry;
        }
      }

      if (entry != entries.end())
      {
        throw std::logic_error(
          "Private map list mismatch during swap, missing at least " +
          std::get<0>(*entry));
      }

      for (auto& [name, lhs, rhs] : entries)
      {
        lhs->swap(rhs);
      }

      for (auto& [name, lhs, rhs] : entries)
      {
        lhs->unlock();
        rhs->unlock();
      }
    }

    ReadOnlyTx create_read_only_tx()
    {
      return ReadOnlyTx(this);
    }

    Tx create_tx()
    {
      return Tx(this);
    }

    ReservedTx create_reserved_tx(Version v)
    {
      return ReservedTx(this, v);
    }
  };
}