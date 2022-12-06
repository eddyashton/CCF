// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ds/radix_tree.h"

#include <doctest/doctest.h>
#include <iostream>
#include <random>
#include <string>
#include <vector>

TEST_CASE("Radix Tree A" * doctest::test_suite("radixtree"))
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

  std::shuffle(strings.begin(), strings.end(), g);
  for (char const* s : strings)
  {
    auto p = rt.prefix_lookup(s);
    REQUIRE(p != nullptr);
    REQUIRE(s == p);
  }
}

TEST_CASE("Radix tree B" * doctest::test_suite("radixtree"))
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
