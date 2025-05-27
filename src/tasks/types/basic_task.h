// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/types/task.h"

#include <functional>

namespace ccf::tasks
{
  struct BasicTask : public ITask
  {
    using Fn = std::function<void()>;

    Fn fn;
    const std::string name;

    BasicTask(const Fn& _fn, const std::string& s = "[BasicTask]") :
      name(s),
      fn(_fn)
    {}

    size_t do_task_implementation() override
    {
      fn();
      return 1;
    }

    std::string get_name() const override
    {
      return name;
    }
  };

  template <typename T, typename... Ts>
  Task make_task(Ts&&... ts)
  {
    return std::make_shared<T>(std::forward<Ts>(ts)...);
  }

  template <typename... Ts>
  Task make_basic_task(Ts&&... ts)
  {
    return make_task<BasicTask>(std::forward<Ts>(ts)...);
  }
}
