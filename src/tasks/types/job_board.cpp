// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "tasks/types/job_board.h"

#include <mutex>
#include <queue>
#include <thread>

namespace ccf::tasks
{
  void JobBoard::add_task(Task&& t)
  {
    std::lock_guard<std::mutex> lock(mutex);
    queue.emplace(std::move(t));
  }

  Task JobBoard::get_task()
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

  bool JobBoard::empty()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return queue.empty();
  }

  Task JobBoard::wait_for_task(const std::chrono::milliseconds& timeout)
  {
    // TODO: Add a condition_variable to remove spinloop
    using TClock = std::chrono::system_clock;

    const auto start = TClock::now();
    const auto until = start + timeout;

    while (true)
    {
      auto task = get_task();
      if (task != nullptr || TClock::now() >= until)
      {
        return task;
      }

      std::this_thread::yield();
    }
  }
}
