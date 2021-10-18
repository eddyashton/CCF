// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/index.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

using TIndex = ccf::historical::Index<std::set<size_t>>;

void print_index(const TIndex& i)
{
  fmt::print("{} sub-range(s)\n", i.sub_ranges.size());
  for (const auto& sr : i.sub_ranges)
  {
    fmt::print(
      "  [{}, {}]: {}\n",
      sr.range.min,
      sr.range.max,
      fmt::join(sr.result, ", "));
  }
}

TEST_CASE("TODO")
{
  TIndex i;

  i.extend_index({3, 6}, {5, 6});
  print_index(i);

  i.extend_index({10, 15}, {11, 12, 13});
  print_index(i);

  i.extend_index({1, 1}, {1});
  print_index(i);

  i.extend_index({5, 8}, {5, 6, 8});
  print_index(i);

  i.extend_index({9, 9}, {});
  print_index(i);

  i.extend_index({8, 11}, {8, 10, 11});
  print_index(i);
}