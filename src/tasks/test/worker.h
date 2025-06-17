// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "./looping_thread.h"
#include "tasks/task_system.h"
#include "tasks/types/itask.h"

struct WorkerState
{
  std::atomic<size_t> work_completed = 0;

  std::atomic<bool> consider_termination = false;

  size_t spinner_idx = 0;
};

struct Worker : public LoopingThread<WorkerState>
{
  Worker(size_t idx) : LoopingThread<WorkerState>(fmt::format("w{}", idx)) {}

  ~Worker() override
  {
    shutdown();
  }

  std::string get_status_string()
  {
    char prefix;
    if (state.consider_termination.load())
    {
      prefix = 'T';
    }
    else
    {
      static constexpr auto spinner = "|/-\\";
      static constexpr auto spinner_len = 4;
      state.spinner_idx = (state.spinner_idx + 1) % spinner_len;
      prefix = spinner[state.spinner_idx];
    }
    return fmt::format("{}[{:4}] ", prefix, state.work_completed);
  }

  Stage loop_behaviour() override
  {
    // Wait (with timeout) for a task
    auto task = ccf::tasks::wait_for_task(std::chrono::milliseconds(10));
    if (task != nullptr)
    {
      state.work_completed += task->do_task();
    }
    else if (state.consider_termination.load())
    {
      return Stage::Terminated;
    }

    return Stage::Running;
  }
};
