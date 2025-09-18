// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "tasks/mpmcq_lockfree.h"

#include "concurrentqueue/concurrentqueue.h"

namespace ccf::tasks
{
  struct MPMCQueue_LockFree::PImpl
  {
    moodycamel::ConcurrentQueue<Task> queue;
  };

  MPMCQueue_LockFree::MPMCQueue_LockFree() : pimpl(std::make_unique<PImpl>()) {}
  MPMCQueue_LockFree::~MPMCQueue_LockFree() = default;

  void MPMCQueue_LockFree::add_item(Task&& t)
  {
    pimpl->queue.enqueue(std::move(t));
  }

  Task MPMCQueue_LockFree::get_item()
  {
    Task t = nullptr;
    pimpl->queue.try_dequeue(t);
    return t;
  }

  bool MPMCQueue_LockFree::empty()
  {
    return pimpl->queue.size_approx() == 0;
  }
}
