// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ordered_tasks.h"

#include <deque>
#include <mutex>

namespace ccf::tasks
{
  // Helper type for OrderedTasks, containing a list of sub-tasks to be
  // performed in-order. Modifiers return bools indicating whether the caller is
  // responsible for scheduling a future flush of this queue.
  template <typename T>
  class SubTaskQueue
  {
  protected:
    std::mutex mutex;
    std::deque<T> pending;
    std::atomic<bool> active;
    std::atomic<bool> paused;

  public:
    bool push(T&& t)
    {
      std::lock_guard<std::mutex> lock(mutex);
      const bool ret = pending.empty() && !active.load();
      pending.emplace_back(std::forward<T>(t));
      return ret;
    }

    using Visitor = std::function<void(T&&)>;
    bool pop_and_visit(Visitor&& visitor)
    {
      decltype(pending) local;
      {
        std::lock_guard<std::mutex> lock(mutex);
        active.store(true);

        std::swap(local, pending);
      }

      auto it = local.begin();
      while (!paused.load() && it != local.end())
      {
        visitor(std::forward<T>(*it));
        ++it;
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        if (it != local.end())
        {
          // Paused mid-execution - some actions remain that need to be spliced
          // back onto the front of the pending pending
          pending.insert(pending.begin(), it, local.end());
        }

        active.store(false);
        return !pending.empty() && !paused.load();
      }
    }

    void pause()
    {
      std::lock_guard<std::mutex> lock(mutex);
      paused.store(true);
    }

    bool unpause()
    {
      std::lock_guard<std::mutex> lock(mutex);
      paused.store(false);
      return !pending.empty() && !active.load();
    }
  };

  struct OrderedTasks::Impl
  {
    std::string name;
    SubTaskQueue<TaskAction> actions;
  };

  struct OrderedTasks::ResumeOrderedTasks : public ccf::tasks::IResumable
  {
    std::shared_ptr<OrderedTasks> tasks;

    ResumeOrderedTasks(std::shared_ptr<OrderedTasks> t) : tasks(t) {}

    void resume() override
    {
      if (tasks->impl->actions.unpause())
      {
        tasks->enqueue_self();
      }
    }
  };

  OrderedTasks::OrderedTasks(const std::string& s)
  {
    impl = std::make_unique<Impl>();
    impl->name = s;
  }

  OrderedTasks::~OrderedTasks() = default;

  void OrderedTasks::enqueue_self()
  {
    ccf::tasks::add_task(shared_from_this());
  }

  size_t OrderedTasks::do_task_implementation()
  {
    size_t n = 0;
    if (impl->actions.pop_and_visit(
          [this, &n](TaskAction&& action) { n += action->do_action(); }))
    {
      enqueue_self();
    }
    return n;
  }

  ccf::tasks::Resumable OrderedTasks::pause()
  {
    impl->actions.pause();

    return std::make_unique<ResumeOrderedTasks>(shared_from_this());
  }

  std::string OrderedTasks::get_name() const
  {
    return impl->name;
  }

  void OrderedTasks::add_action(TaskAction&& action)
  {
    if (impl->actions.push(std::move(action)))
    {
      enqueue_self();
    }
  }
}
