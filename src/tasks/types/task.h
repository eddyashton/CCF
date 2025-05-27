// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <memory>

namespace ccf::tasks
{
  struct ITask;
  using Task = std::shared_ptr<ITask>;
}
