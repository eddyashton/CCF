// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/logger.h"
#include "ccf/pal/locking.h"

namespace ds
{
  class NamedMutex
  {
  private:
    ccf::pal::Mutex m;
    std::string label;

  public:
    NamedMutex(const std::string& s) : label(s) {}

    void lock()
    {
      LOG_INFO_FMT("Locking {}", label);
      m.lock();
    }

    bool try_lock()
    {
      return m.try_lock();
    }

    void unlock()
    {
      LOG_INFO_FMT("Unlocking {}", label);
      m.unlock();
    }
  };
}
