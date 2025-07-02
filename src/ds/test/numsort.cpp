// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "../numsort.h"

#include <doctest/doctest.h>

using namespace ccf::nonstd::numsort;

void require_elements(std::string_view s, Elements expected)
{
  const auto es = to_elements(s);

  if (es != expected)
  {
    std::string repr;
    for (const auto& e : es)
    {
      if (!repr.empty())
      {
        repr += ", ";
      }

      repr += std::visit(
        [](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, size_t>)
          {
            return std::to_string(arg);
          }
          else if constexpr (std::is_same_v<T, std::string_view>)
          {
            return fmt::format("\"{}\"", arg);
          }
          else
          {
            static_assert(false, "Non-exhaustive visitor");
          }
        },
        e);
    }

    fmt::print("{} => [{}] {}\n", s, es.size(), repr);
  }

  REQUIRE(es == expected);
}

TEST_CASE("to_elements" * doctest::test_suite("numsort"))
{
  require_elements("", {});

  require_elements("a", {"a"});
  require_elements("1", {1ull});

  require_elements("a_1", {"a_", 1ull});
  require_elements("1_a", {1ull, "_a"});

  require_elements("12helloworld", {12ull, "helloworld"});
  require_elements("hello12world", {"hello", 12ull, "world"});
  require_elements("helloworld12", {"helloworld", 12ull});

  require_elements("1hello2world", {1ull, "hello", 2ull, "world"});
  require_elements("1helloworld2", {1ull, "helloworld", 2ull});
  require_elements("hello1world2", {"hello", 1ull, "world", 2ull});
}