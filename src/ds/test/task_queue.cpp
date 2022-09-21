// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "./task_queue.h"

#include "./task.h"

#include <doctest/doctest.h>
#include <experimental/coroutine>
#include <iostream>
#include <utility>

Task foo(TaskQueue& tq)
{
  std::cout << "Beginning foo" << std::endl;
  for (auto i = 0; i < 10; ++i)
  {
    std::cout << "Pre-schedule " << i << std::endl;
    co_await tq.schedule();
    std::cout << "Post schedule " << i << std::endl;
  }
  std::cout << "Ending foo" << std::endl;
}

Task bar(TaskQueue& tq)
{
  std::cout << "Beginning bar" << std::endl;
  co_await tq.schedule();
  std::cout << "Ending bar" << std::endl;
}

TEST_CASE("Hmmmm" * doctest::test_suite("taskqueue"))
{
  TaskQueue tq;

  std::cout << "AAAA" << std::endl;
  Task t1 = foo(tq);
  std::cout << "BBB" << std::endl;
  Task t2 = bar(tq);
  std::cout << "CCC" << std::endl;

  while (tq.advance_all())
  {
    std::cout << "Looping" << std::endl;
  }

  std::cout << "Done" << std::endl;
}
