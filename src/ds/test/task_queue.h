// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#pragma once

#include <cstdint>
#include <experimental/coroutine>
#include <queue>

class TaskQueue
{
public:
  auto schedule()
  {
    struct awaiter
    {
      TaskQueue* owning_task_queue;

      constexpr bool await_ready() const noexcept
      {
        return false;
      }

      constexpr void await_resume() const noexcept {}

      void await_suspend(
        std::experimental::coroutine_handle<> coro) const noexcept
      {
        owning_task_queue->enqueue_task(coro);
      }
    };
    return awaiter{this};
  }

  bool advance_all()
  {
    decltype(pending_tasks) current_tasks;
    std::swap(current_tasks, pending_tasks);

    for (auto& task : current_tasks)
    {
      task.resume();
    }

    return !current_tasks.empty();
  }

private:
  std::vector<std::experimental::coroutine_handle<>> pending_tasks;

  void enqueue_task(std::experimental::coroutine_handle<> coro) noexcept
  {
    pending_tasks.emplace_back(coro);
  }
};