// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "kv/change_set.h"
#include "kv/kv_types.h"
#include "kv/serialised_entry.h"

namespace kv::untyped
{
  using SerialisedEntry = kv::serialisers::SerialisedEntry;
  using SerialisedKeyHasher = std::hash<SerialisedEntry>;

  using VersionV = kv::VersionV<SerialisedEntry>;
  using State =
    kv::State<SerialisedEntry, SerialisedEntry, SerialisedKeyHasher>;
  using Read = kv::Read<SerialisedEntry>;
  using Write = kv::Write<SerialisedEntry, SerialisedEntry>;
  using ChangeSet =
    kv::ChangeSet<SerialisedEntry, SerialisedEntry, SerialisedKeyHasher>;
  using SnapshotChangeSet = kv::
    SnapshotChangeSet<SerialisedEntry, SerialisedEntry, SerialisedKeyHasher>;

  class TxView
  {
  public:
    // Expose these types so that other code can use them as MyTx::KeyType or
    // MyMap::TxView::KeyType, templated on the TxView or Map type
    using KeyType = SerialisedEntry;
    using ValueType = SerialisedEntry;

  protected:
    ChangeSet& tx_changes;

    /** Get pointer to current value if this key exists, else nullptr if it does
     * not exist or has been deleted. If non-null, points to something owned by
     * tx_changes - expect this is used/dereferenced immediately, and there is
     * no concurrent access which could invalidate it. Modifies read set if
     * appropriate to record read dependency on this key, at the version of the
     * returned data.
     */
    const ValueType* read_key(const KeyType& key)
    {
      // A write followed by a read doesn't introduce a read dependency.
      // If we have written, return the value without updating the read set.
      auto write = tx_changes.writes.find(key);
      if (write != tx_changes.writes.end())
      {
        if (write->second.has_value())
        {
          return &write->second.value();
        }
        else
        {
          return nullptr;
        }
      }

      // If the key doesn't exist, return empty and record that we depend on
      // the key not existing.
      const auto search = tx_changes.state.getp(key);
      if (search == nullptr)
      {
        tx_changes.reads.insert(std::make_pair(key, NoVersion));
        return nullptr;
      }

      // Record the version that we depend on.
      tx_changes.reads.insert(std::make_pair(key, search->version));

      // Return the value.
      return &search->value;
    }

  public:
    TxView(ChangeSet& cs) : tx_changes(cs) {}

    /** Get value for key
     *
     * This returns the value for the key inside the transaction. If the key
     * has been updated in the current transaction, that update will be
     * reflected in the return of this call.
     *
     * @param key Key
     *
     * @return optional containing value, empty if the key doesn't exist
     */
    std::optional<ValueType> get(const KeyType& key)
    {
      auto value_p = read_key(key);
      if (value_p == nullptr)
      {
        return std::nullopt;
      }

      return *value_p;
    }

    /** Get globally committed value for key
     *
     * This reads a globally replicated value for the specified key.
     * The value will have been the replicated value when the transaction
     * began, but the map may be compacted while the transaction is in
     * flight. If that happens, there may be a more recent committed
     * version. This is undetectable to the transaction.
     *
     * @param key Key
     *
     * @return optional containing value, empty if the key doesn't exist in
     * globally committed state
     */
    std::optional<ValueType> get_globally_committed(const KeyType& key)
    {
      // If there is no committed value, return empty.
      auto search = tx_changes.committed.get(key);
      if (!search.has_value())
      {
        return std::nullopt;
      }

      // Return the value.
      return search.value().value;
    }

    /** Test if key is present
     *
     * This returns true if the key has a value inside the transaction. If the
     * key has been updated (written to or deleted) in the current transaction,
     * that update will be reflected in the return of this call.
     *
     * @param key Key
     *
     * @return bool true iff key exists
     */
    bool has(const KeyType& key)
    {
      auto value_p = read_key(key);
      return value_p != nullptr;
    }

    /** Write value at key
     *
     * If the key already exists, the value will be replaced.
     * This will fail if the transaction is already committed.
     *
     * @param key Key
     * @param value Value
     *
     * @return true if successful, false otherwise
     */
    bool put(const KeyType& key, const ValueType& value)
    {
      // Record in the write set.
      tx_changes.writes[key] = value;
      return true;
    }

    /** Remove key
     *
     * This will fail if the key does not exist, or if the transaction
     * is already committed.
     *
     * @param key Key
     *
     * @return true if successful, false otherwise
     */
    bool remove(const KeyType& key)
    {
      auto write = tx_changes.writes.find(key);
      auto exists_in_state = tx_changes.state.getp(key) != nullptr;

      if (write != tx_changes.writes.end())
      {
        if (!exists_in_state)
        {
          // this key only exists locally, there is no reason to maintain and
          // serialise it
          tx_changes.writes.erase(key);
        }
        else
        {
          // If we have written, change the write set to indicate a remove.
          write->second = std::nullopt;
        }

        return true;
      }

      // If the key doesn't exist, return false.
      if (!exists_in_state)
      {
        return false;
      }

      // Record in the write set.
      tx_changes.writes[key] = std::nullopt;
      return true;
    }

    /** Iterate over all entries in the map
     *
     * @param F functor, taking a key and a value, return value determines
     * whether the iteration should continue (true) or stop (false)
     */
    template <class F>
    void foreach(F&& f)
    {
      // Record a global read dependency.
      tx_changes.read_version = tx_changes.start_version;
      auto& w = tx_changes.writes;
      bool should_continue = true;

      tx_changes.state.foreach(
        [&w, &f, &should_continue](const KeyType& k, const VersionV& v) {
          auto write = w.find(k);

          if (write == w.end())
          {
            should_continue = f(k, v.value);
          }

          return should_continue;
        });

      if (should_continue)
      {
        for (auto write = tx_changes.writes.begin();
             write != tx_changes.writes.end();
             ++write)
        {
          if (write->second.has_value())
          {
            should_continue = f(write->first, write->second.value());
          }

          if (!should_continue)
          {
            break;
          }
        }
      }
    }
  };
}