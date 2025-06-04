// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "./looping_thread.h"
#include "tasks/task_system.h"
#include "tasks/types/itask.h"

struct WorkerState
{
  size_t work_completed;

  std::atomic<bool> consider_termination = false;

  static constexpr auto STATUS_STRING_LENGTH = 8;
  std::atomic<char> status_string[STATUS_STRING_LENGTH];

  size_t spinner_idx = 0;
};

struct Worker : public LoopingThread<WorkerState>
{
  Worker(size_t idx) : LoopingThread<WorkerState>(fmt::format("w{}", idx))
  {
    // eg: "T[1234] "
    for (auto i = 0; i < WorkerState::STATUS_STRING_LENGTH; ++i)
    {
      const char c = i == 1 ? '[' : (i == 6 ? ']' : ' ');
      state.status_string[i] = c;
    }
  }

  ~Worker() override
  {
    shutdown();

    // LOG_INFO_FMT(
    //   "Shutting down {}, processed {} tasks", name, state.work_completed);
  }

  Stage loop_behaviour() override
  {
    // Wait (with timeout) for a task
    auto task = ccf::tasks::wait_for_task(std::chrono::milliseconds(10));
    if (task != nullptr)
    {
      state.work_completed += task->do_task();
      _write_status_string_number(state.work_completed, 5, state.status_string);
    }
    else if (state.consider_termination.load())
    {
      state.status_string[0] = 'T';
      return Stage::Terminated;
    }

    static constexpr auto spinner = "|/-\\";
    static constexpr auto spinner_len = 4;
    state.spinner_idx = (state.spinner_idx + 1) % spinner_len;
    state.status_string[0] = spinner[state.spinner_idx];
    return Stage::Running;
  }
};
