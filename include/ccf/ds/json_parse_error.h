// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <vector>

namespace ccf
{
  class JsonParseError : public std::invalid_argument
  {
  public:
    std::vector<std::string> pointer_elements = {};

    using std::invalid_argument::invalid_argument;

    std::string pointer() const
    {
      return fmt::format(
        "#/{}",
        fmt::join(pointer_elements.crbegin(), pointer_elements.crend(), "/"));
    }

    std::string describe() const
    {
      return fmt::format("At {}: {}", pointer(), what());
    }
  };
}