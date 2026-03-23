// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "node/historical_queries/compound_handle.h"
#include "node/historical_queries/store_details.h"

#include <cassert>
#include <set>
#include <unordered_map>

namespace ccf::historical
{
  // Owns the global store of fetched/in-flight ledger entries and the
  // ref-counting / size-tracking bookkeeping that was previously spread
  // across StateCacheImpl.  All cache-size mutations go through this class
  // so that the invariant
  //   estimated_size == sum(raw_size for entries with ref_count > 0)
  // is maintained in exactly one place.
  class EntryStore
  {
  public:
    // Weak-pointer map of *all* entries across all requests.  Distinct
    // requests for the same seqno share the same underlying StoreDetails.
    AllRequestedStores all_stores;

    void add_ref(SeqNo seq, CompoundHandle handle)
    {
      auto it = store_to_requests_.find(seq);

      if (it == store_to_requests_.end())
      {
        store_to_requests_.insert({seq, {handle}});
        auto size = raw_store_sizes_.find(seq);
        if (size != raw_store_sizes_.end())
        {
          estimated_size_ += size->second;
        }
      }
      else
      {
        it->second.insert(handle);
      }
    }

    void remove_ref(SeqNo seq, CompoundHandle handle)
    {
      auto it = store_to_requests_.find(seq);
      assert(it != store_to_requests_.end());

      it->second.erase(handle);
      if (it->second.empty())
      {
        store_to_requests_.erase(it);
        auto size = raw_store_sizes_.find(seq);
        if (size != raw_store_sizes_.end())
        {
          estimated_size_ -= size->second;
          raw_store_sizes_.erase(size);
        }
      }
    }

    void add_refs_for(CompoundHandle handle, const auto& stores_map)
    {
      for (const auto& [seq, _] : stores_map)
      {
        add_ref(seq, handle);
      }
    }

    void remove_refs_for(CompoundHandle handle, const auto& stores_map)
    {
      for (const auto& [seq, _] : stores_map)
      {
        remove_ref(seq, handle);
      }
    }

    void update_raw_size(SeqNo seq, size_t new_size)
    {
      auto& stored_size = raw_store_sizes_[seq];
      assert(!stored_size || stored_size == new_size);

      estimated_size_ -= stored_size;
      estimated_size_ += new_size;
      stored_size = new_size;
    }

    size_t estimated_size() const
    {
      return estimated_size_;
    }

  private:
    // Maps seqno -> set of handles that reference it
    std::unordered_map<SeqNo, std::set<CompoundHandle>> store_to_requests_;
    // Maps seqno -> raw serialised byte size
    std::unordered_map<ccf::SeqNo, size_t> raw_store_sizes_;
    // Running total of raw sizes for entries with at least one reference
    CacheSize estimated_size_ = 0;
  };
}
