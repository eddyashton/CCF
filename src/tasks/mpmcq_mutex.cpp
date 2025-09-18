// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "tasks/mpmcq_mutex.h"

#include <mutex>
#include <queue>

namespace ccf::tasks
{
  void MPMCQueue_Mutex::add_item(Task&& t)
  {
    std::lock_guard<std::mutex> lock(mutex);
    queue.emplace(std::move(t));
  }

  Task MPMCQueue_Mutex::get_item()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty())
    {
      return nullptr;
    }

    Task t = queue.front();
    queue.pop();
    return t;
  }

  bool MPMCQueue_Mutex::empty()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return queue.empty();
  }
}
