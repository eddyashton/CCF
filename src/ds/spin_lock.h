// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <thread>

#ifdef INSIDE_ENCLAVE
#  include <pthread.h>

class SpinLockImpl
{
private:
  pthread_spinlock_t sl;

public:
  SpinLockImpl()
  {
    pthread_spin_init(&sl, PTHREAD_PROCESS_PRIVATE);
  }

  ~SpinLockImpl()
  {
    pthread_spin_destroy(&sl);
  }

  void lock()
  {
    pthread_spin_lock(&sl);
  }

  void unlock()
  {
    pthread_spin_unlock(&sl);
  }

  bool try_lock()
  {
    return pthread_spin_trylock(&sl) == 0;
  }
};
#else
#  include <mutex>
using SpinLockImpl = std::mutex;
#endif

class SpinLock
{
private:
  SpinLockImpl impl;
  std::thread::id lock_holder;

public:
  void lock()
  {
    impl.lock();
    lock_holder = std::this_thread::get_id();
  }

  void unlock()
  {
    // This seems unsafe if this thread wasn't the previous holder, but in
    // that case this is already undefined behaviour (calling unlock() on a
    // mutex you don't hold is UB)
    lock_holder = {};
    impl.unlock();
  }

  bool try_lock()
  {
    const auto locked = impl.try_lock();
    if (locked)
    {
      lock_holder = std::this_thread::get_id();
    }
    return locked;
  }

  bool is_held_by_current_thread()
  {
    const auto locked = impl.try_lock();
    if (locked)
    {
      unlock();
      return false;
    }

    return lock_holder == std::this_thread::get_id();
  }
};
