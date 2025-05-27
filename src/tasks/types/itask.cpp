// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "itask.h"

// TODO: Rename, since this implements things from a few different places?

namespace ccf::tasks
{
  thread_local ITask* current_task = nullptr;

  Resumable pause_current_task()
  {
    if (current_task == nullptr)
    {
      throw std::logic_error("Cannot pause: No task currently running");
    }

    auto handle = current_task->pause();
    if (handle == nullptr)
    {
      throw std::logic_error("Cannot pause: Current task is not pausable");
    }

    return handle;
  }

  void resume_task(Resumable&& resumable)
  {
    resumable->resume();

    // Consume the argument, so it can't be reused
    auto _ = std::move(resumable);
  }

  size_t ITask::do_task()
  {
    if (cancelled.load())
    {
      return 0;
    }

    ccf::tasks::current_task = this;

    const auto n = do_task_implementation();

    ccf::tasks::current_task = nullptr;

    return n;
  }

  ccf::tasks::Resumable ITask::pause()
  {
    return nullptr;
  }

  void ITask::cancel_task()
  {
    cancelled.store(true);
  }

  bool ITask::is_cancelled() const
  {
    return cancelled.load();
  }
}
