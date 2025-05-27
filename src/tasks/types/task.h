// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "tasks/types/itask.h"

#include <memory>

namespace ccf::tasks
{
  using Task = std::shared_ptr<ITask>;
}
