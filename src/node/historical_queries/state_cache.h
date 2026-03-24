// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "node/historical_queries/historical_queries.h"

namespace ccf::historical
{
  // Public API wrapper that maps RequestHandle (user-facing, single integer)
  // to CompoundHandle (internal, namespace + integer). This is the class
  // registered as ccf::AbstractStateCache in the node subsystem.
  class AppFacingStateCache : public StateCacheCore, public AbstractStateCache
  {
  protected:
    CompoundHandle make_compound_handle(RequestHandle rh)
    {
      return {RequestNamespace::Application, rh};
    }

  public:
    template <typename... Ts>
    AppFacingStateCache(Ts&&... ts) : StateCacheCore(std::forward<Ts>(ts)...)
    {}

    ccf::kv::ReadOnlyStorePtr get_store_at(
      RequestHandle handle,
      ccf::SeqNo seqno,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_store_at(
        make_compound_handle(handle), seqno, seconds_until_expiry);
    }

    ccf::kv::ReadOnlyStorePtr get_store_at(
      RequestHandle handle, ccf::SeqNo seqno) override
    {
      return StateCacheCore::get_store_at(make_compound_handle(handle), seqno);
    }

    StatePtr get_state_at(
      RequestHandle handle,
      ccf::SeqNo seqno,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_state_at(
        make_compound_handle(handle), seqno, seconds_until_expiry);
    }

    StatePtr get_state_at(RequestHandle handle, ccf::SeqNo seqno) override
    {
      return StateCacheCore::get_state_at(make_compound_handle(handle), seqno);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_store_range(
      RequestHandle handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_store_range(
        make_compound_handle(handle),
        start_seqno,
        end_seqno,
        seconds_until_expiry);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_store_range(
      RequestHandle handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno) override
    {
      return StateCacheCore::get_store_range(
        make_compound_handle(handle), start_seqno, end_seqno);
    }

    std::vector<StatePtr> get_state_range(
      RequestHandle handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_state_range(
        make_compound_handle(handle),
        start_seqno,
        end_seqno,
        seconds_until_expiry);
    }

    std::vector<StatePtr> get_state_range(
      RequestHandle handle,
      ccf::SeqNo start_seqno,
      ccf::SeqNo end_seqno) override
    {
      return StateCacheCore::get_state_range(
        make_compound_handle(handle), start_seqno, end_seqno);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_stores_for(
      RequestHandle handle,
      const SeqNoCollection& seqnos,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_stores_for(
        make_compound_handle(handle), seqnos, seconds_until_expiry);
    }

    std::vector<ccf::kv::ReadOnlyStorePtr> get_stores_for(
      RequestHandle handle, const SeqNoCollection& seqnos) override
    {
      return StateCacheCore::get_stores_for(
        make_compound_handle(handle), seqnos);
    }

    std::vector<StatePtr> get_states_for(
      RequestHandle handle,
      const SeqNoCollection& seqnos,
      ExpiryDuration seconds_until_expiry) override
    {
      return StateCacheCore::get_states_for(
        make_compound_handle(handle), seqnos, seconds_until_expiry);
    }

    std::vector<StatePtr> get_states_for(
      RequestHandle handle, const SeqNoCollection& seqnos) override
    {
      return StateCacheCore::get_states_for(
        make_compound_handle(handle), seqnos);
    }

    void set_default_expiry_duration(ExpiryDuration duration) override
    {
      StateCacheCore::set_default_expiry_duration(duration);
    }

    void set_soft_cache_limit(CacheSize cache_limit) override
    {
      StateCacheCore::set_soft_cache_limit(cache_limit);
    }

    void track_deletes_on_missing_keys(bool track) override
    {
      StateCacheCore::track_deletes_on_missing_keys(track);
    }

    bool drop_cached_states(RequestHandle handle) override
    {
      return StateCacheCore::drop_cached_states(make_compound_handle(handle));
    }

    size_t get_estimated_store_cache_size() override
    {
      return StateCacheCore::get_estimated_store_cache_size();
    }
  };
}
