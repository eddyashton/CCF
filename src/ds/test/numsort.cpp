// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "../numsort.h"

#include <doctest/doctest.h>
#include <fmt/ranges.h>

using namespace ccf::nonstd::numsort;

std::string format_elements(const Elements& es)
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
        else if constexpr (std::is_same_v<T, std::string>)
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
  return repr;
}

void require_elements(std::string_view s, Elements expected)
{
  const auto actual = to_elements(s);

  if (actual != expected)
  {
    fmt::print(
      "From {}, expected {}, actual {}\n",
      s,
      format_elements(expected),
      format_elements(actual));
  }

  REQUIRE(actual == expected);
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

using Strings = std::vector<std::string>;
TEST_CASE("sort" * doctest::test_suite("numsort"))
{
  {
    Strings v;
    REQUIRE_NOTHROW(numsort(v.begin(), v.end()));
  }

  {
    Strings v{"alice_10", "alice_1"};
    numsort(v.begin(), v.end());
    REQUIRE(v[0] == "alice_1");
    REQUIRE(v[1] == "alice_10");
  }

  {
    Strings v{
      "alice_1_1",
      "bob_1_1",
      "alice_5_1",
      "alice_10_1",
      "bob_1_10",
      "bob_5_1",
      "bob_5_10",
      "alice_11_1",
      "alice_10_5",
      "bob_1_5"};
    numsort(v.begin(), v.end());
    REQUIRE(v[0] == "alice_1_1");
    REQUIRE(v[1] == "alice_5_1");
    REQUIRE(v[2] == "alice_10_1");
    REQUIRE(v[3] == "alice_10_5");
    REQUIRE(v[4] == "alice_11_1");
    REQUIRE(v[5] == "bob_1_1");
    REQUIRE(v[6] == "bob_1_5");
    REQUIRE(v[7] == "bob_1_10");
    REQUIRE(v[8] == "bob_5_1");
    REQUIRE(v[9] == "bob_5_10");
  }

  {
    INFO("Repeated values, variable number of elements");
    Strings v{
      "1.0a",
      "1.0b",
      "1.1",
      "1.a",
      "1.a.42",
      "1.2.0",
      "1.1.2",
      "1.2.0",
      "0.1",
      "1.0",
      "1.2.0",
      "0.0.1",
      "1.2.0",
      "1",
      "10",
      "1.2.0",
    };
    numsort(v.begin(), v.end());
    REQUIRE(v[0] == "0.0.1");
    REQUIRE(v[1] == "0.1");
    REQUIRE(v[2] == "1");
    REQUIRE(v[3] == "1.0");
    REQUIRE(v[4] == "1.0a");
    REQUIRE(v[5] == "1.0b");
    REQUIRE(v[6] == "1.1");
    REQUIRE(v[7] == "1.1.2");
    REQUIRE(v[8] == "1.2.0");
    REQUIRE(v[9] == "1.2.0");
    REQUIRE(v[10] == "1.2.0");
    REQUIRE(v[11] == "1.2.0");
    REQUIRE(v[12] == "1.2.0");
    REQUIRE(v[13] == "1.a");
    REQUIRE(v[14] == "1.a.42");
    REQUIRE(v[15] == "10");
  }
}
