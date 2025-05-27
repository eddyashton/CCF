// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <atomic>
#include <string>

namespace ccf::tasks
{
  struct ITask
  {
    // TODO: Re-order and document members and methods
    std::atomic<bool> cancelled = false;

    virtual ~ITask() = default;

    virtual size_t do_task()
    {
      if (cancelled.load())
      {
        return 0;
      }

      return do_task_implementation();
    }

    virtual std::string get_name() const = 0;

    void cancel_task()
    {
      cancelled.store(true);
    }

    bool is_cancelled() const
    {
      return cancelled.load();
    }

    // Return some value indicating how much work was done.
    virtual size_t do_task_implementation() = 0;
  };
}
