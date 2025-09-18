// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/task.h"

#include <mutex>
#include <queue>

namespace ccf::tasks
{
  struct MPMCQueue_LockFree
  {
    struct PImpl;
    std::unique_ptr<PImpl> pimpl = nullptr;

    MPMCQueue_LockFree();
    ~MPMCQueue_LockFree();

    void add_item(Task&& t);
    Task get_item();
    bool empty();
  };
}
