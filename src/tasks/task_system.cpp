// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "tasks/types/job_board.h"

namespace ccf::tasks
{
  static JobBoard job_board;

  void add_task(Task&& t)
  {
    job_board.add_task(std::move(t));
  }

  Task get_task()
  {
    return job_board.get_task();
  }

  bool empty()
  {
    return job_board.empty();
  }

  Task wait_for_task(const std::chrono::milliseconds& timeout)
  {
    return job_board.wait_for_task(timeout);
  }
}
