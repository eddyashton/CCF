// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/types/resumable.h"

#include <atomic>
#include <string>

namespace ccf::tasks
{
  struct ITask
  {
    // TODO: Re-order and document members
    std::atomic<bool> cancelled = false;

    virtual ~ITask() = default;

    size_t do_task();

    virtual ccf::tasks::Resumable pause();

    // Return some value indicating how much work was done.
    virtual size_t do_task_implementation() = 0;

    virtual std::string get_name() const = 0;

    void cancel_task();
    bool is_cancelled();
  };
}
