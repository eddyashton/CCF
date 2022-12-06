// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ds/radix_tree.h"

#include <doctest/doctest.h>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

TEST_CASE("Hmmm" * doctest::test_suite("radixtree"))
{
  std::vector<char const*> strings = {
    "a",
    "b",
    "c",
    "d",
    "aa",
    "ab",
    "ac",
    "ad",
    "bb",
    "bbb",
    "bbbb",
    "bbbbb",
    "bbbbbb",
    "bbbbbbb",
    "dda",
    "ddc"};

  ds::RadixTree rt;

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(strings.begin(), strings.end(), g);
  for (char const* s : strings)
  {
    rt.insert(s, s);
  }

  // std::random_device rd;
  // std::mt19937 g(rd());
  std::shuffle(strings.begin(), strings.end(), g);
  for (char const* s : strings)
  {
    auto p = rt.prefix_lookup(s);
    REQUIRE(p != nullptr);
    REQUIRE(s == p);
  }
}

TEST_CASE("TODO" * doctest::test_suite("radixtree"))
{
  ds::RadixTree rt;

  auto data_a = 42;
  auto data_b = 100;

  rt.insert("POST /foo", &data_a);
  rt.insert("POST /foobar", &data_b);

  {
    auto lookup_0 = rt.prefix_lookup("POST /foo");
    REQUIRE(lookup_0 == &data_a);

    auto lookup_1 = rt.prefix_lookup("POST /foob");
    REQUIRE(lookup_1 == &data_a);

    auto lookup_2 = rt.prefix_lookup("POST /foobar");
    REQUIRE(lookup_2 == &data_b);

    auto lookup_3 = rt.prefix_lookup("NOPE");
    REQUIRE(lookup_3 == nullptr);

    auto lookup_4 = rt.prefix_lookup("POST /foobab");
    REQUIRE(lookup_4 == &data_a);

    auto lookup_5 = rt.prefix_lookup("POST /foobarr");
    REQUIRE(lookup_5 == &data_b);

    auto lookup_6 = rt.prefix_lookup("POST /fo");
    REQUIRE(lookup_6 == nullptr);
  }

  {
    auto lookup_0 = rt.exact_lookup("POST /foo");
    REQUIRE(lookup_0 == &data_a);

    auto lookup_1 = rt.exact_lookup("POST /foob");
    REQUIRE(lookup_1 == nullptr);

    auto lookup_2 = rt.exact_lookup("POST /foobar");
    REQUIRE(lookup_2 == &data_b);

    auto lookup_3 = rt.exact_lookup("NOPE");
    REQUIRE(lookup_3 == nullptr);

    auto lookup_4 = rt.exact_lookup("POST /foobab");
    REQUIRE(lookup_4 == nullptr);

    auto lookup_5 = rt.exact_lookup("POST /foobarr");
    REQUIRE(lookup_5 == nullptr);

    auto lookup_6 = rt.exact_lookup("POST /fo");
    REQUIRE(lookup_6 == nullptr);
  }
}

TEST_CASE("benchmark" * doctest::test_suite("radixtree"))
{
  std::vector<std::string> elements = {
    "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"};
  std::vector<std::string> paths;
  for (auto i = 0; i < elements.size(); ++i)
  {
    const auto first = elements[i];
    for (auto j = 0; j < elements.size(); ++j)
    {
      if (i != j)
      {
        const auto second = elements[j];
        for (auto k = 0; k < elements.size(); ++k)
        {
          if (i != k && j != k)
          {
            const auto third = elements[k];
            paths.push_back(fmt::format("/{}/{}/{}", first, second, third));
          }
        }
      }
    }
  }

  ds::RadixTree rt;
  for (const auto& s : paths)
  {
    rt.insert(s, s.data());
  }

  using Clock = std::chrono::system_clock;

  static constexpr auto num_attempts = 5;
  static constexpr auto iterations_per_attempt = 10'000;
  const auto start_time = Clock::now();
  for (auto attempt = 0; attempt < num_attempts; ++attempt)
  {
    for (auto i = 0; i < iterations_per_attempt; ++i)
    {
      for (const auto& s : paths)
      {
        auto d = rt.prefix_lookup(s);
      }
    }
  }
  const auto end_time = Clock::now();
  const auto total_time_ms =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  const float total_time_s = total_time_ms.count() / 1000.f;

  fmt::print(
    "{} runs, {} lookups of {} paths each run, finished in {}s\n",
    num_attempts,
    iterations_per_attempt,
    paths.size(),
    total_time_s);

  const auto total_dispatches =
    num_attempts * iterations_per_attempt * paths.size();
  const auto dispatch_rate = total_dispatches / total_time_s;
  // Actual obsereved rates are > 15'000'000. Add a big buffer for variance and
  // slower hardware, just try to confirm we've not completely lost performance
  // here
  REQUIRE(dispatch_rate > 10'000'000);
}
