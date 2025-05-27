// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/types/itask.h"

#include <memory>

namespace ccf::tasks
{
  struct PauseResume;
  using PauseResumeHandle = std::unique_ptr<PauseResume>;

  PauseResumeHandle pause_current_task();
  void resume_task(PauseResumeHandle&& handle);

  struct PausableTask : public ITask,
                        public std::enable_shared_from_this<PausableTask>
  {
    size_t do_task() override;

    ccf::tasks::PauseResumeHandle pause_task();

    virtual void on_pause() = 0;
    virtual void on_resume() = 0;
  };

  struct PauseResume
  {
  private:
    std::shared_ptr<PausableTask> task;

    void resume()
    {
      task->on_resume();
    }

  public:
    PauseResume(std::shared_ptr<PausableTask> t) : task(std::move(t)) {}

    friend void ccf::tasks::resume_task(PauseResumeHandle&& handle);
  };
}
