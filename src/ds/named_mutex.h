// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/logger.h"
#include "ccf/pal/locking.h"

#include <deque>
#include <set>

#define ASSERT_MUTEX_ORDER

namespace ds
{
#ifdef ASSERT_MUTEX_ORDER
  static thread_local std::deque<std::string> locked_stack;
  using PrecedingLock = std::pair<std::string, std::string>;
  static std::set<PrecedingLock> lock_precedences;
#endif

  class NamedMutex
  {
  private:
    ccf::pal::Mutex m;

#ifdef ASSERT_MUTEX_ORDER
    std::string label;

    void check_lock_safety()
    {
      for (const auto& locked : locked_stack)
      {
        const auto inverted = std::make_pair(label, locked);
        const auto it = lock_precedences.find(inverted);
        if (it != lock_precedences.end())
        {
          LOG_FAIL_FMT(
            "Mutexes locked in inconsistent order - this may cause deadlock!");
          LOG_FAIL_FMT(
            "Currently attempting to lock {} while holding {} - previously "
            "held {} while trying to lock {}",
            label,
            locked,
            locked,
            label);
        }

        const auto ordered = std::make_pair(locked, label);
        lock_precedences.insert(ordered);
      }
      locked_stack.push_back(label);
    }
#endif

  public:
    NamedMutex(const std::string& s)
#ifdef ASSERT_MUTEX_ORDER
      :
      label(fmt::format("{} ({:0x})", s, (size_t)this))
#endif
    {}

    void lock()
    {
#ifdef ASSERT_MUTEX_ORDER
      check_lock_safety();
#endif

      m.lock();
    }

    bool try_lock()
    {
#ifdef ASSERT_MUTEX_ORDER
      check_lock_safety();
#endif

      return m.try_lock();
    }

    void unlock()
    {
#ifdef ASSERT_MUTEX_ORDER
      if (locked_stack.back() != label)
      {
        LOG_FAIL_FMT(
          "Unexpected unlock order - {} is not the last-locked (stack is {})",
          label,
          fmt::join(locked_stack, "->"));
      }
      else
      {
        locked_stack.pop_back();
      }
#endif

      m.unlock();
    }
  };
}

#define CCF_CREATE_GUARD(var_name, mutex) \
  std::lock_guard<decltype(mutex)> var_name(mutex)
