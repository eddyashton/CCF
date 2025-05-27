// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "tasks/types/basic_task.h"
#include "tasks/types/ordered_tasks.h"
#include "worker.h"

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

// TODO: Be explicit
using namespace ccf::tasks;

TEST_CASE("Tasks")
{
  size_t x = 0;

  // Basic tasks
  const std::string name_1 = "Set x to 1";
  auto set_1 = make_basic_task([&x]() { x = 1; }, name_1);
  REQUIRE(set_1->get_name() == name_1);
  REQUIRE(x == 0);
  set_1->do_task();
  REQUIRE(x == 1);

  // Cancelling pre-execution
  const std::string name_2 = "Set x to 2";
  auto set_2 = make_basic_task([&x]() { x = 2; }, name_2);
  REQUIRE(set_2->get_name() == name_2);
  REQUIRE(x == 1);
  REQUIRE_FALSE(set_2->is_cancelled());
  set_2->cancel_task();
  REQUIRE(set_2->is_cancelled());
  set_2->do_task();
  REQUIRE(set_2->is_cancelled());
  REQUIRE(x == 1);

  // Cancelling post-execution
  const std::string name_3 = "Set x to 3";
  auto set_3 = make_basic_task([&x]() { x = 3; }, name_3);
  REQUIRE(set_3->get_name() == name_3);
  REQUIRE(x == 1);
  REQUIRE_FALSE(set_3->is_cancelled());
  set_3->do_task();
  REQUIRE(x == 3);
  REQUIRE_FALSE(set_3->is_cancelled());
  set_3->cancel_task();
  REQUIRE(set_3->is_cancelled());
  REQUIRE(x == 3);
}

TEST_CASE("OrderedTasks")
{
  // With more sessions than workers, and tasks concurrently added to these
  // sessions, each task is still executed in-order for that session
  static constexpr auto num_sessions = 5;
  static constexpr auto num_workers = 2;

  // Record last x seen for each session
  using Result = std::atomic<size_t>;
  std::vector<Result> results(num_sessions);

  {
    // Record next x to send for each session
    std::vector<std::pair<std::shared_ptr<OrderedTasks>, size_t>> all_tasks;
    for (auto i = 0; i < num_sessions; ++i)
    {
      all_tasks.emplace_back(
        std::make_shared<OrderedTasks>(std::to_string(i)), 0);
    }

    auto add_action = [&](size_t idx, size_t sleep_time_ms) {
      auto& [tasks, n] = all_tasks[idx];
      tasks->add_action(make_basic_action([=, &n, &results]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));
        const auto x = ++n;
        LOG_TRACE_FMT("{} {}", tasks->get_name(), x);
        REQUIRE(++results[idx] == x);
      }));
    };

    // Add some initial tasks on each session
    const auto spacing = 3;
    const auto period = spacing * num_sessions + 1;
    for (auto i = 0; i < num_sessions; ++i)
    {
      add_action(i, spacing * i);
      add_action(i, period);
      add_action(i, period);
    }

    {
      std::vector<std::unique_ptr<Worker>> workers;
      for (auto i = 0; i < num_workers; ++i)
      {
        workers.emplace_back(std::make_unique<Worker>(i));
      }

      // Start processing those tasks on worker threads
      for (auto& worker : workers)
      {
        worker->start();
      }

      // Continually add tasks, while the workers are running
      for (auto i = 0; i < num_workers * num_sessions * 10; ++i)
      {
        add_action(i % all_tasks.size(), period);
        // Try to produce an interesting interleaving of tasks across sessions
        if (i % ((num_workers * num_sessions) - 1) == 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(period / 2));
        }
      }

      while (!ccf::tasks::empty())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
  }
}

TEST_CASE("PauseAndResume")
{
  size_t x = 0;
  size_t y = 0;

  auto increment = [](size_t& n) { return make_basic_action([&n]() { ++n; }); };

  std::shared_ptr<OrderedTasks> x_tasks = std::make_shared<OrderedTasks>("x");
  std::shared_ptr<OrderedTasks> y_tasks = std::make_shared<OrderedTasks>("y");

  x_tasks->add_action(increment(x));
  y_tasks->add_action(increment(y));
  y_tasks->add_action(increment(y));

  {
    Worker worker(0);

    // Worker exists but hasn't started yet - no increments have occurred
    REQUIRE(x == 0);
    REQUIRE(y == 0);

    // Even if we wait
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 0);
    REQUIRE(y == 0);

    // If we start the worker (and wait), it will execute the pending tasks
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 1);
    REQUIRE(y == 2);

    // We can concurrently queue many more tasks, which will be executed
    // immediately
    for (auto i = 0; i < 100; ++i)
    {
      x_tasks->add_action(increment(x));
      y_tasks->add_action(increment(y));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 101);
    REQUIRE(y == 102);
  }

  {
    // Terminating previous worker, creating a new one (not yet running)
    Worker worker(1);

    // If we need to block, we can ask for a task to be paused. Note that the
    // current action will still complete
    bool happened = false;
    ccf::tasks::Resumable resumable;

    x_tasks->add_action(increment(x));
    x_tasks->add_action(make_basic_action([&]() {
      // NB: This doesn't need to _know_ the current task, just that it is
      // executed _as part of a task_. This means it could occur deep within a
      // call-stack.
      resumable = ccf::tasks::pause_current_task();
      // NB: The current _action_ will still complete execution!
      happened = true;
    }));
    x_tasks->add_action(increment(x));

    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 102); // One increment action happened
    REQUIRE(happened == true); // Then the pause action ran to completion
    REQUIRE(resumable != nullptr); // We got a handle to later resume this task

    // Other actions can be scheduled, including on the paused task.
    // Unpaused tasks will complete as normal.
    for (auto i = 0; i < 100; ++i)
    {
      x_tasks->add_action(increment(x));
      y_tasks->add_action(increment(y));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 102);
    REQUIRE(y == 202);

    // After resume, all queued actions will (be able to) execute, in-order
    ccf::tasks::resume_task(std::move(resumable));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 203);
    REQUIRE(y == 202);

    // A task might be paused multiple times during its life
    resumable = nullptr;
    x_tasks->add_action(increment(x));
    x_tasks->add_action(make_basic_action(
      [&]() { resumable = ccf::tasks::pause_current_task(); }));
    x_tasks->add_action(increment(x));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 204);
    REQUIRE(resumable != nullptr);

    // A paused task can be cancelled
    x_tasks->cancel_task();

    // Cancellation supercedes resumption - nothing more happens on this task
    ccf::tasks::resume_task(std::move(resumable));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(x == 204);
  }

  // Trying to pause outside of a task will throw an error
  REQUIRE_THROWS(ccf::tasks::pause_current_task());
}

int main(int argc, char** argv)
{
  // ccf::tasks::TaskSystem::init();
  ccf::logger::config::default_init();
  ccf::logger::config::level() = ccf::LoggerLevel::INFO;

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  int res = context.run();
  if (context.shouldExit())
    return res;
  return res;
}
