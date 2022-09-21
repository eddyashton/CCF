// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#pragma once

#include <experimental/coroutine>
#include <iostream>

struct TaskPromise;

class [[nodiscard]] Task
{
public:
  using promise_type = TaskPromise;

  explicit Task(std::experimental::coroutine_handle<TaskPromise> handle) :
    promise_handle(handle)
  {}

private:
  std::experimental::coroutine_handle<TaskPromise> promise_handle;
};

struct TaskPromise
{
  Task get_return_object() noexcept
  {
    return Task{
      std::experimental::coroutine_handle<TaskPromise>::from_promise(*this)};
  };

  std::experimental::suspend_never initial_suspend() const noexcept
  {
    return {};
  }
  std::experimental::suspend_never final_suspend() const noexcept
  {
    return {};
  }

  void return_void() noexcept {}

  void unhandled_exception() noexcept
  {
    std::cerr << "Unhandled exception caught...\n";
    exit(1);
  }
};