// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "pause_resume.h"

#include <stdexcept>

namespace ccf::tasks
{
  /// Implementation of functions from pause_resume.h, using a thread-local
  /// pointer to the currently active PausableTask to access an an overridden
  /// pause, and later consuming a (previously-returned) PauseResumeHandle.
  ///@{
  thread_local PausableTask* current_task = nullptr;

  PauseResumeHandle pause_current_task()
  {
    if (current_task == nullptr)
    {
      throw std::logic_error("Cannot pause: No task currently running");
    }

    auto handle = current_task->pause_task();
    if (handle == nullptr)
    {
      throw std::logic_error("Cannot pause: Current task is not pausable");
    }

    return handle;
  }

  void resume_task(PauseResumeHandle&& handle)
  {
    handle->resume();

    // Consume the argument, so it can't be reused
    auto _ = std::move(handle);
  }

  size_t PausableTask::do_task()
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

  ccf::tasks::PauseResumeHandle PausableTask::pause_task()
  {
    on_pause();
    return std::make_unique<PauseResume>(shared_from_this());
  }
  ///@}
}
