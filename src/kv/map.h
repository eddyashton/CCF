// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/dl_list.h"
#include "ds/logger.h"
#include "ds/spin_lock.h"
#include "kv_serialiser.h"
#include "kv_types.h"
#include "tx_view.h"

#include <functional>
#include <optional>
#include <unordered_set>

namespace kv
{
  namespace Check
  {
    struct No
    {};

    template <typename T, typename Arg>
    No operator!=(const T&, const Arg&)
    {
      return No();
    }

    template <typename T, typename Arg = T>
    struct Ne
    {
      enum
      {
        value = !std::is_same<decltype(*(T*)(0) != *(Arg*)(0)), No>::value
      };
    };

    template <class T>
    bool ne(std::enable_if_t<Ne<T>::value, const T&> a, const T& b)
    {
      return a != b;
    }

    template <class T>
    bool ne(std::enable_if_t<!Ne<T>::value, const T&> a, const T& b)
    {
      return false;
    }
  }

  template <class K, class V, class H = std::hash<K>>
  class Map;

  template <class K, class V, class H>
  class TxViewCommitter : public AbstractTxView
  {
  protected:
    using MyMap = Map<K, V, H>;

    ChangeSet<K, V, H> change_set;

    MyMap& map;
    size_t rollback_counter;

    Version commit_version = NoVersion;

    bool changes = false;
    bool committed_writes = false;

  public:
    template <typename... Ts>
    TxViewCommitter(MyMap& m, size_t rollbacks, Ts&&... ts) :
      map(m),
      rollback_counter(rollbacks),
      change_set(std::forward<Ts>(ts)...)
    {}

    // Commit-related methods
    bool has_writes() override
    {
      return committed_writes || !change_set.writes.empty();
    }

    bool has_changes() override
    {
      return changes;
    }

    bool prepare() override
    {
      if (change_set.writes.empty())
        return true;

      // If the parent map has rolled back since this transaction began, this
      // transaction must fail.
      if (rollback_counter != map.rollback_counter)
        return false;

      // If we have iterated over the map, check for a global version match.
      auto current = map.roll->get_tail();

      if (
        (change_set.read_version != NoVersion) &&
        (change_set.read_version != current->version))
      {
        LOG_DEBUG_FMT("Read version {} is invalid", change_set.read_version);
        return false;
      }

      // Check each key in our read set.
      for (auto it = change_set.reads.begin(); it != change_set.reads.end();
           ++it)
      {
        // Get the value from the current state.
        auto search = current->state.get(it->first);

        if (it->second == NoVersion)
        {
          // If we depend on the key not existing, it must be absent.
          if (search.has_value())
          {
            LOG_DEBUG_FMT("Read depends on non-existing entry");
            return false;
          }
        }
        else
        {
          // If we depend on the key existing, it must be present and have the
          // version that we expect.
          if (!search.has_value() || (it->second != search.value().version))
          {
            LOG_DEBUG_FMT("Read depends on invalid version of entry");
            return false;
          }
        }
      }

      return true;
    }

    void commit(Version v) override
    {
      if (change_set.writes.empty())
      {
        commit_version = change_set.start_version;
        return;
      }

      // Record our commit time.
      commit_version = v;
      committed_writes = true;

      if (!change_set.writes.empty())
      {
        auto state = map.roll->get_tail()->state;

        for (auto it = change_set.writes.begin(); it != change_set.writes.end();
             ++it)
        {
          if (it->second.has_value())
          {
            // Write the new value with the global version.
            changes = true;
            state = state.put(it->first, VersionV{v, it->second.value()});
          }
          else
          {
            // Write an empty value with the deleted global version only if
            // the key exists.
            auto search = state.get(it->first);
            if (search.has_value())
            {
              changes = true;
              state = state.put(it->first, VersionV{-v, V()});
            }
          }
        }

        if (changes)
        {
          map.roll->insert_back(
            map.create_new_local_commit(v, state, change_set.writes));
        }
      }
    }

    void post_commit() override
    {
      // This is run separately from commit so that all commits in the Tx
      // have been applied before local hooks are run. The maps in the Tx
      // are still locked when post_commit is run.
      if (change_set.writes.empty())
        return;

      if (map.local_hook)
      {
        auto roll = map.roll->get_tail();
        map.local_hook(roll->version, roll->writes);
      }
    }

    // Used by owning map during serialise and deserialise
    ChangeSet<K, V, H>& get_change_set()
    {
      return change_set;
    }

    const ChangeSet<K, V, H>& get_change_set() const
    {
      return change_set;
    }

    void set_commit_version(Version v)
    {
      commit_version = v;
    }
  };

  template <typename K, typename V, typename H>
  struct ConcreteTxView : public TxViewCommitter<K, V, H>,
                          public TxView<K, V, H>
  {
  public:
    ConcreteTxView(
      Map<K, V, H>& m,
      size_t rollbacks,
      State<K, V, H>& current_state,
      State<K, V, H>& committed_state,
      Version v) :
      TxViewCommitter<K, V, H>(m, rollbacks, current_state, committed_state, v),
      TxView<K, V, H>(TxViewCommitter<K, V, H>::change_set)
    {}
  };

  /// Signature for transaction commit handlers
  template <typename W>
  using CommitHook = std::function<void(Version, const W&)>;

  template <class K, class V, class H>
  class Map : public AbstractMap
  {
  public:
    using VersionV = VersionV<V>;
    using State = State<K, V, H>;
    using Write = Write<K, V, H>;
    using CommitHook = CommitHook<Write>;

  private:
    using This = Map<K, V, H>;

    struct LocalCommit
    {
      LocalCommit() = default;
      LocalCommit(Version v, State s, Write w) :
        version(std::move(v)),
        state(std::move(s)),
        writes(std::move(w)),
        next(nullptr),
        prev(nullptr)
      {}

      Version version;
      State state;
      Write writes;
      LocalCommit* next;
      LocalCommit* prev;
    };
    using LocalCommits = snmalloc::DLList<LocalCommit, std::nullptr_t, true>;

    AbstractStore* store;
    std::string name;
    size_t rollback_counter;
    std::unique_ptr<LocalCommits> roll;
    CommitHook local_hook = nullptr;
    CommitHook global_hook = nullptr;
    LocalCommits commit_deltas;
    SpinLock sl;
    const SecurityDomain security_domain;
    const bool replicated;

    LocalCommits empty_commits;

    template <typename... Args>
    LocalCommit* create_new_local_commit(Args&&... args)
    {
      LocalCommit* c = empty_commits.pop();
      if (c == nullptr)
      {
        c = new LocalCommit(std::forward<Args>(args)...);
      }
      else
      {
        c->~LocalCommit();
        new (c) LocalCommit(std::forward<Args>(args)...);
      }
      return c;
    }

  public:
    // Public typedef for external consumption
    using TxView = ConcreteTxView<K, V, H>;

    // Provide access to hidden rollback_counter, roll, create_new_local_commit
    friend TxViewCommitter<K, V, H>;

    Map(
      AbstractStore* store_,
      std::string name_,
      SecurityDomain security_domain_,
      bool replicated_) :
      store(store_),
      name(name_),
      roll(std::make_unique<LocalCommits>()),
      rollback_counter(0),
      security_domain(security_domain_),
      replicated(replicated_)
    {
      roll->insert_back(create_new_local_commit(0, State(), Write()));
    }

    Map(const Map& that) = delete;

    virtual AbstractMap* clone(AbstractStore* other) override
    {
      return new Map(other, name, security_domain, replicated);
    }

    void serialise(
      const AbstractTxView* view,
      KvStoreSerialiser& s,
      bool include_reads) override
    {
      const auto committer =
        dynamic_cast<const TxViewCommitter<K, V, H>*>(view);
      if (committer == nullptr)
      {
        LOG_FAIL_FMT("Unable to serialise map due to type mismatch");
        return;
      }

      const auto& change_set = committer->get_change_set();

      s.start_map(name, security_domain);

      if (include_reads)
      {
        s.serialise_read_version(change_set.read_version);

        s.serialise_count_header(change_set.reads.size());
        for (auto it = change_set.reads.begin(); it != change_set.reads.end();
             ++it)
        {
          s.serialise_read(it->first, it->second);
        }
      }
      else
      {
        s.serialise_read_version(NoVersion);
        s.serialise_count_header(0);
      }

      uint64_t write_ctr = 0;
      uint64_t remove_ctr = 0;
      for (auto it = change_set.writes.begin(); it != change_set.writes.end();
           ++it)
      {
        if (it->second.has_value())
        {
          ++write_ctr;
        }
        else
        {
          auto search = roll->get_tail()->state.get(it->first);
          if (search.has_value())
          {
            ++remove_ctr;
          }
        }
      }

      s.serialise_count_header(write_ctr);
      for (auto it = change_set.writes.begin(); it != change_set.writes.end();
           ++it)
      {
        if (it->second.has_value())
        {
          s.serialise_write(it->first, it->second.value());
        }
      }

      s.serialise_count_header(remove_ctr);
      for (auto it = change_set.writes.begin(); it != change_set.writes.end();
           ++it)
      {
        if (!it->second.has_value())
        {
          s.serialise_remove(it->first);
        }
      }
    }

    AbstractTxView* deserialise(
      KvStoreDeserialiser& d, Version version) override
    {
      // Create a new change set, and deserialise d's contents into it.
      auto view = create_view<TxView>(version);
      view->set_commit_version(version);

      auto& change_set = view->get_change_set();

      uint64_t ctr;

      auto rv = d.template deserialise_read_version<Version>();
      if (rv != NoVersion)
      {
        change_set.read_version = rv;
      }

      ctr = d.deserialise_read_header();
      for (size_t i = 0; i < ctr; ++i)
      {
        auto r = d.template deserialise_read<K>();
        change_set.reads[std::get<0>(r)] = std::get<1>(r);
      }

      ctr = d.deserialise_write_header();
      for (size_t i = 0; i < ctr; ++i)
      {
        auto w = d.template deserialise_write<K, V>();
        change_set.writes[std::get<0>(w)] = std::get<1>(w);
      }

      ctr = d.deserialise_remove_header();
      for (size_t i = 0; i < ctr; ++i)
      {
        auto r = d.template deserialise_remove<K>();
        change_set.writes[r] = std::nullopt;
      }

      return view;
    }

    /** Get the name of the map
     *
     * @return const std::string&
     */
    const std::string& get_name() const override
    {
      return name;
    }

    /** Get store that the map belongs to
     *
     * @return Pointer to `kv::AbstractStore`
     */
    AbstractStore* get_store() override
    {
      return store;
    }

    /** Set handler to be called on local transaction commit
     *
     * @param hook function to be called on local transaction commit
     */
    void set_local_hook(const CommitHook& hook)
    {
      std::lock_guard<SpinLock> guard(sl);
      local_hook = hook;
    }

    /** Reset local transaction commit handler
     */
    void unset_local_hook()
    {
      std::lock_guard<SpinLock> guard(sl);
      local_hook = nullptr;
    }

    /** Set handler to be called on global transaction commit
     *
     * @param hook function to be called on global transaction commit
     */
    void set_global_hook(const CommitHook& hook)
    {
      std::lock_guard<SpinLock> guard(sl);
      global_hook = hook;
    }

    /** Reset global transaction commit handler
     */
    void unset_global_hook()
    {
      std::lock_guard<SpinLock> guard(sl);
      global_hook = nullptr;
    }

    /** Get security domain of a Map
     *
     * @return Security domain of the map (affects serialisation)
     */
    virtual SecurityDomain get_security_domain() override
    {
      return security_domain;
    }

    /** Get Map replicability
     *
     * @return true if the map is to be replicated, false if it is to be derived
     */
    virtual bool is_replicated() override
    {
      return replicated;
    }

    bool operator==(const AbstractMap& that) const override
    {
      auto p = dynamic_cast<const This*>(&that);
      if (p == nullptr)
        return false;

      if (name != p->name)
        return false;

      auto state1 = roll->get_tail();
      auto state2 = p->roll->get_tail();

      if (state1->version != state2->version)
        return false;

      size_t count = 0;
      state2->state.foreach([&count](const K& k, const VersionV& v) {
        count++;
        return true;
      });

      size_t i = 0;
      bool ok =
        state1->state.foreach([&state2, &i](const K& k, const VersionV& v) {
          auto search = state2->state.get(k);

          if (search.has_value())
          {
            auto& found = search.value();
            if (found.version != v.version)
            {
              return false;
            }
            else if (Check::ne(found.value, v.value))
            {
              return false;
            }
          }
          else
          {
            return false;
          }

          i++;
          return true;
        });

      if (i != count)
        ok = false;

      return ok;
    }

    bool operator!=(const AbstractMap& that) const override
    {
      return !(*this == that);
    }

    void compact(Version v) override
    {
      // This discards available rollback state before version v, and populates
      // the commit_deltas to be passed to the global commit hook, if there is
      // one, up to version v. The Map expects to be locked during compaction.
      while (roll->get_head() != roll->get_tail())
      {
        auto r = roll->get_head();

        // Globally committed but not discardable.
        if (r->version == v)
        {
          // We know that write set is not empty.
          if (global_hook)
          {
            commit_deltas.insert_back(
              create_new_local_commit(r->version, r->state, move(r->writes)));
          }
          return;
        }

        // Discardable, so move to commit_deltas.
        if (global_hook && !r->writes.empty())
        {
          commit_deltas.insert_back(
            create_new_local_commit(r->version, r->state, move(r->writes)));
        }

        // Stop if the next state may be rolled back or is the only state.
        // This ensures there is always a state present.
        if (r->next->version > v)
          return;

        auto c = roll->pop();
        empty_commits.insert(c);
      }

      // There is only one roll. We may need to call the commit hook.
      auto r = roll->get_head();

      if (global_hook && !r->writes.empty())
      {
        commit_deltas.insert_back(
          create_new_local_commit(r->version, r->state, move(r->writes)));
      }
    }

    void post_compact() override
    {
      if (global_hook)
      {
        for (auto r = commit_deltas.get_head(); r != nullptr; r = r->next)
        {
          global_hook(r->version, r->writes);
        }
      }

      commit_deltas.clear();
    }

    void rollback(Version v) override
    {
      // This rolls the current state back to version v.
      // The Map expects to be locked during rollback.
      bool advance = false;

      while (roll->get_head() != roll->get_tail())
      {
        auto r = roll->get_tail();

        // The initial empty state has v = 0, so will not be discarded if it
        // is present.
        if (r->version <= v)
          break;

        advance = true;
        auto c = roll->pop_tail();
        empty_commits.insert(c);
      }

      if (advance)
        rollback_counter++;
    }

    void clear() override
    {
      // This discards all entries in the roll and resets the compacted value
      // and rollback counter. The Map expects to be locked before clearing it.
      roll->clear();
      roll->insert_back(create_new_local_commit(0, State(), Write()));
      rollback_counter = 0;
    }

    void lock() override
    {
      sl.lock();
    }

    void unlock() override
    {
      sl.unlock();
    }

    void swap(AbstractMap* map_) override
    {
      This* map = dynamic_cast<This*>(map_);
      if (map == nullptr)
        throw std::logic_error(
          "Attempted to swap maps with incompatible types");

      std::swap(rollback_counter, map->rollback_counter);
      std::swap(roll, map->roll);
    }

    template <typename TView>
    TView* create_view(Version version)
    {
      lock();

      // Find the last entry committed at or before this version.
      TView* view = nullptr;

      for (auto current = roll->get_tail(); current != nullptr;
           current = current->prev)
      {
        if (current->version <= version)
        {
          view = new TView(
            *this,
            rollback_counter,
            current->state,
            roll->get_head()->state,
            current->version);
          break;
        }
      }

      if (view == nullptr)
      {
        view = new TView(
          *this,
          rollback_counter,
          roll->get_head()->state,
          roll->get_head()->state,
          roll->get_head()->version);
      }

      unlock();
      return view;
    }
  };
}