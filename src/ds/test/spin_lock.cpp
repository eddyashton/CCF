// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "../spin_lock.h"

#include <atomic>
#include <doctest/doctest.h>
#include <thread>
#include <vector>

TEST_CASE("Spin lock holder")
{
  SpinLock sl;
  constexpr size_t n_competing_threads = 512;

  SUBCASE("Simple owner")
  {
    sl.lock();
    REQUIRE(sl.is_held_by_current_thread());
    sl.unlock();
  }

  std::atomic<bool> starting_pistol = false;

  SUBCASE("Competing holders are queued")
  {
    std::vector<std::thread> threads;
    for (auto i = 0; i < n_competing_threads; ++i)
    {
      threads.emplace_back([&sl, &starting_pistol]() {
        while (!starting_pistol.load())
        {
          continue;
        }

        sl.lock();
        REQUIRE(sl.is_held_by_current_thread());
        sl.unlock();
      });
    }

    starting_pistol.store(true);

    for (auto& thread : threads)
    {
      thread.join();
    }
  }

  SUBCASE("Failed locks are correctly reported")
  {
    sl.lock();

    std::vector<std::thread> threads;
    for (auto i = 0; i < n_competing_threads; ++i)
    {
      threads.emplace_back([&sl, &starting_pistol]() {
        while (!starting_pistol.load())
        {
          continue;
        }

        REQUIRE_FALSE(sl.try_lock());
        REQUIRE_FALSE(sl.is_held_by_current_thread());
      });
    }

    starting_pistol.store(true);

    for (auto& thread : threads)
    {
      thread.join();
    }

    sl.unlock();
  }
}
