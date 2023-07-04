// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/logger.h"
#include "ccf/pal/locking.h"

#include <stack>

#define ASSERT_MUTEX_ORDER

namespace ds
{
#ifdef ASSERT_MUTEX_ORDER
  static thread_local std::stack<std::string> locked_stack;
#endif

  class NamedMutex
  {
  private:
    ccf::pal::Mutex m;
    std::string label;

#ifdef ASSERT_MUTEX_ORDER
    void check_lock_safety()
    {
      locked_stack.push(label);
    }
#endif

  public:
    NamedMutex(const std::string& s) : label(s) {}

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
      if (locked_stack.top() != label)
      {
        LOG_FAIL_FMT("Unexpected unlock order - {} is not the last-locked", label);
      }
      else
      {
        locked_stack.pop();
      }
#endif

      m.unlock();
    }
  };
}
