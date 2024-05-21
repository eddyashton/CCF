// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/tx_id.h"
#include "ccf/ds/contiguous_set.h"

namespace ccf
{
  using SeqNoCollection = ds::ContiguousSet<ccf::SeqNo>;
}
