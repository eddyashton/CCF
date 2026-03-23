// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/historical_queries_interface.h"

#include <utility>

namespace ccf::historical
{
  enum class RequestNamespace : uint8_t
  {
    Application,
    System,
  };

  using CompoundHandle = std::pair<RequestNamespace, RequestHandle>;
};

FMT_BEGIN_NAMESPACE
template <>
struct formatter<ccf::historical::CompoundHandle>
{
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx)
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(
    const ccf::historical::CompoundHandle& p, FormatContext& ctx) const
  {
    return format_to(
      ctx.out(),
      "[{}|{}]",
      std::get<0>(p) == ccf::historical::RequestNamespace::Application ? "APP" :
                                                                         "SYS",
      std::get<1>(p));
  }
};
FMT_END_NAMESPACE
