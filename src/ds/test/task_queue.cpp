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
  std::cout << "  Beginning foo" << std::endl;
  for (auto i = 0; i < 3; ++i)
  {
    std::cout << "  Pre-schedule " << i << std::endl;
    co_await tq.schedule();
    std::cout << "  Post schedule " << i << std::endl;
  }
  std::cout << "  Ending foo" << std::endl;
}

Task bar(TaskQueue& tq)
{
  std::cout << "  Beginning bar" << std::endl;
  co_await tq.schedule();
  std::cout << "  Ending bar" << std::endl;
}

Task baz(TaskQueue& tq)
{
  std::cout << "  Beginning baz" << std::endl;
  co_await tq.schedule();
  std::cout << "  Mid-baz A" << std::endl;
  co_await bar(tq);
  std::cout << "  Mid-baz B" << std::endl;
  co_await tq.schedule();
  std::cout << "  Ending baz" << std::endl;
}

TEST_CASE("TestA" * doctest::test_suite("taskqueue"))
{
  std::cout << "==TestA==" << std::endl;
  TaskQueue tq;

  std::cout << "AAA" << std::endl;
  Task t1 = bar(tq);
  std::cout << "BBB" << std::endl;

  tq.advance_all();
  tq.advance_all();

  // while (tq.advance_all())
  // {
  //   std::cout << "Looping" << std::endl;
  // }

  std::cout << "Done" << std::endl;
}

TEST_CASE("TestB" * doctest::test_suite("taskqueue"))
{
  std::cout << "==TestB==" << std::endl;
  TaskQueue tq;

  std::cout << "AAA" << std::endl;
  Task t1 = baz(tq);
  std::cout << "BBB" << std::endl;

  tq.advance_all();
  tq.advance_all();

  // while (tq.advance_all())
  // {
  //   std::cout << "Looping" << std::endl;
  // }

  std::cout << "Done" << std::endl;
}
