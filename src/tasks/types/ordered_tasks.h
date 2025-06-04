// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/logger.h"
#include "tasks/task_system.h"
#include "tasks/types/pause_resume.h"

#include <deque>
#include <mutex>

namespace ccf::tasks
{
  struct ITaskAction
  {
    virtual void do_action() = 0;

    virtual std::string get_name() const = 0;
  };

  using TaskAction = std::shared_ptr<ITaskAction>;

  struct BasicTaskAction : public ITaskAction
  {
    using Fn = std::function<void()>;

    Fn fn;
    const std::string name;

    BasicTaskAction(const Fn& _fn, const std::string& s = "[Anon]") :
      name(s),
      fn(_fn)
    {}

    void do_action() override
    {
      fn();
    }

    std::string get_name() const override
    {
      return name;
    }
  };

  template <typename... Ts>
  TaskAction make_basic_action(Ts&&... ts)
  {
    return std::make_shared<BasicTaskAction>(std::forward<Ts>(ts)...);
  }

  // Self-scheduling collection of in-order tasks. Tasks will be executed in the
  // order they are added. To self-schedule, this instance will ensure that it
  // is scheduled for further execution whenever more sub-tasks are available
  // for execution.
  class OrderedTasks : public PausableTask
  {
  protected:
    struct Impl;
    std::unique_ptr<Impl> impl = nullptr;

    void enqueue_self();

    void on_pause() override;
    void on_resume() override;

  public:
    OrderedTasks(const std::string& s = "[Ordered]");
    ~OrderedTasks();

    size_t do_task_implementation() override;

    std::string get_name() const override;

    void add_action(TaskAction&& action);

    bool currently_active();
    size_t pending_tasks();
  };
}
