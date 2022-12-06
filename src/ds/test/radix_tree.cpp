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

// TODO: Move the below to a separate issue
// To be able to handle paths with multiple templates, like:
//   /foo/{student}/address/{street}/{number}/count
// We need some kind of recursive matchers. So we have insert an entry for
// "/foo/" into our radix tree, and when that matches it does a regex check of
// the next element, and if _that_ matches it directs to another prefix tree,
// where we insert "/address", and so on.
// Note that non-templated URIs will always be preferred, so if we have
//   /foo/bar   AND   /foo/{id}
// then /foo/bar will match the first, while /foo/ba and /foo/baz will match the
// second.

TEST_CASE("Regex lookup" * doctest::test_suite("radixtree"))
{
  ds::RadixTree rt;

  auto data_a = 42;
  auto data_b = 100;

  rt.insert("POST /foo", &data_a);
  rt.insert("POST /foo/bar", &data_b);

  REQUIRE(rt.prefix_lookup("POST /foo/bar") == &data_b);
  REQUIRE(rt.prefix_lookup("POST /foo/any/other/string") == &data_a);
}