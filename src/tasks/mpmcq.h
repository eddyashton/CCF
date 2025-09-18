// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#define CCF_TASK_SYSTEM_LOCKFREE true

#if CCF_TASK_SYSTEM_LOCKFREE
#  include "tasks/mpmcq_lockfree.h"
#else
#  include "tasks/mpmcq_mutex.h"
#endif

namespace ccf::tasks
{
#if CCF_TASK_SYSTEM_LOCKFREE
  using MPMCQueue = MPMCQueue_LockFree;
#else
  using MPMCQueue = MPMCQueue_Mutex;
#endif
}
