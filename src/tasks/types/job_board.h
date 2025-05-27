// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/types/task.h"

#include <mutex>
#include <queue>
#include <thread>

namespace ccf::tasks
{
  struct JobBoard
  {
    std::mutex mutex;
    std::queue<Task> queue;

    void add_task(Task&& t);

    Task get_task();

    bool empty();

    Task wait_for_task(const std::chrono::milliseconds& timeout);
  };
}
