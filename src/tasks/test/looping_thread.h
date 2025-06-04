// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/threading/thread_ids.h"

#include <atomic>
#include <doctest/doctest.h>
#include <string>
#include <thread>

enum class Stage
{
  PreInit,
  Running,
  ShuttingDown,
  Terminated,
};
template <typename TState>
struct LoopingThread
{
  using State = TState;

  // Derived instances will likely access state inside their loop_behaviour,
  // which should be destroyed _after_ the loop ends. That means (because of C++
  // destructor order) it needs to be defined as a member here, so that it is
  // destructed _after_ the destructor runs
  TState state;

  std::atomic<bool> stop_signal = false;
  std::thread thread;

  const std::string name;

  std::atomic<Stage> lifetime_stage;

  template <typename... Ts>
  LoopingThread(const std::string& _name, Ts&&... args) :
    state(std::forward<Ts>(args)...),
    name(_name),
    lifetime_stage(Stage::PreInit)
  {}

  virtual ~LoopingThread() = 0;

  virtual void shutdown()
  {
    LOG_DEBUG_FMT("Stopping {}", name);
    stop_signal.store(true);

    if (thread.joinable())
    {
      thread.join();
    }

    lifetime_stage.store(Stage::Terminated);
  }

  virtual void start()
  {
    thread = std::thread([this]() {
      ccf::threading::set_current_thread_name(name);

      lifetime_stage.store(Stage::PreInit);

      this->init_behaviour();

      lifetime_stage.store(Stage::Running);

      while (!stop_signal)
      {
        auto loop_behaviour_target_stage = this->loop_behaviour();
        REQUIRE(loop_behaviour_target_stage >= lifetime_stage);
        lifetime_stage.store(loop_behaviour_target_stage);
        if (lifetime_stage.load() == Stage::Terminated)
        {
          break;
        }

        auto idle_behaviour_target_stage = this->idle_behaviour();
        REQUIRE(idle_behaviour_target_stage >= lifetime_stage);
        lifetime_stage.store(idle_behaviour_target_stage);
        if (lifetime_stage.load() == Stage::Terminated)
        {
          break;
        }
      }

      LOG_DEBUG_FMT("Terminating thread");
    });
  }

  virtual void init_behaviour() {}

  virtual Stage loop_behaviour()
  {
    // Base loop_behaviour is to terminate immediately
    return Stage::Terminated;
  }

  virtual Stage idle_behaviour()
  {
    std::this_thread::yield();
    return lifetime_stage.load();
  }

  // TODO: This is an ugly hack, doesn't fill the given space
  void _write_status_string_number(
    size_t value, size_t ending_at, std::atomic<char>* output)
  {
    const auto s = std::to_string(value);
    for (auto it = s.rbegin(); it != s.rend(); ++it)
    {
      output[ending_at--] = *it;
    }
  }
};

template <typename T>
inline LoopingThread<T>::~LoopingThread<T>()
{}
