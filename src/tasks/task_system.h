// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "types/task.h"

namespace ccf::tasks
{
  // Producer side - add a piece of work to be executed by the task system
  void add_task(Task&& t);

  // Consumer side - pop the next piece of work to be executed
  Task wait_for_task(const std::chrono::milliseconds& timeout);

  // TODO: Should this be here?
  bool empty();
}
