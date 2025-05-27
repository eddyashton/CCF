// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "types/task.h"

namespace ccf::tasks
{
  void add_task(Task&& t);

  Task get_task();

  // TODO: Should this be here?
  bool empty();

  Task wait_for_task(const std::chrono::milliseconds& timeout);
}
