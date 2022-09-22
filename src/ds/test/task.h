// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#pragma once

#include <experimental/coroutine>
#include <iostream>

struct Task;

struct TaskPromise
{
  struct FinalAwaitable
  {
    bool await_ready() const noexcept
    {
      return false;
    }

    std::experimental::coroutine_handle<> await_suspend(
      std::experimental::coroutine_handle<TaskPromise> coro) noexcept
    {
      return coro.promise().m_continuation;
    }

    void await_resume() noexcept {}
  };

  Task get_return_object() noexcept;

  std::experimental::suspend_never initial_suspend() const noexcept
  {
    return {};
  }

  auto final_suspend() const noexcept
  {
    return FinalAwaitable();
  }

  void return_void() noexcept {}
  
  void unhandled_exception() noexcept
  {
    exit(1);
  }

  void set_continuation(
    std::experimental::coroutine_handle<> continuation) noexcept
  {
    m_continuation = continuation;
  }

private:
  std::experimental::coroutine_handle<> m_continuation =
    std::experimental::noop_coroutine();
};

class Task
{
public:
  using promise_type = TaskPromise;

  explicit Task(std::experimental::coroutine_handle<TaskPromise> handle) :
    m_handle(handle)
  {}

  ~Task()
  {
    if (m_handle)
    {
      m_handle.destroy();
    }
  }

  auto operator co_await() noexcept
  {
    struct awaiter
    {
      bool await_ready() const noexcept
      {
        return !m_coro || m_coro.done();
      }
      std::experimental::coroutine_handle<> await_suspend(
        std::experimental::coroutine_handle<> awaiting_coroutine) noexcept
      {
        m_coro.promise().set_continuation(awaiting_coroutine);
        return m_coro;
      }
      void await_resume() noexcept {}

      std::experimental::coroutine_handle<TaskPromise> m_coro;
    };
    return awaiter{m_handle};
  }

private:
  std::experimental::coroutine_handle<TaskPromise> m_handle;
};

inline Task TaskPromise::get_return_object() noexcept
{
  return Task{
    std::experimental::coroutine_handle<TaskPromise>::from_promise(*this)};
}