// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ds/radix_tree.h"

#include <doctest/doctest.h>

TEST_CASE("TODO" * doctest::test_suite("radixtree"))
{
  ds::RadixTree rt;

  auto data_a = 42;
  auto data_b = 100;

  rt.insert("POST /foo", &data_a);
  rt.insert("POST /foobar", &data_b);

  {
    auto lookup_0 = rt.lookup("POST /foo");
    REQUIRE(lookup_0 == &data_a);

    auto lookup_1 = rt.lookup("POST /foob");
    REQUIRE(lookup_1 == &data_a);

    auto lookup_2 = rt.lookup("POST /foobar");
    REQUIRE(lookup_2 == &data_b);

    auto lookup_3 = rt.lookup("NOPE");
    REQUIRE(lookup_3 == nullptr);

    auto lookup_4 = rt.lookup("POST /foobab");
    REQUIRE(lookup_4 == &data_b); // TODO: Huh?

    auto lookup_5 = rt.lookup("POST /foobarr");
    REQUIRE(lookup_5 == &data_b);
  }

  {
    auto lookup_0 = rt.lookup_exact("POST /foo");
    REQUIRE(lookup_0 == &data_a);

    auto lookup_1 = rt.lookup_exact("POST /foob");
    REQUIRE(lookup_1 == nullptr);

    auto lookup_2 = rt.lookup_exact("POST /foobar");
    REQUIRE(lookup_2 == &data_b);

    auto lookup_3 = rt.lookup_exact("NOPE");
    REQUIRE(lookup_3 == nullptr);

    auto lookup_4 = rt.lookup_exact("POST /foobab");
    REQUIRE(lookup_4 == &data_b); // TODO: HUHHH?

    auto lookup_5 = rt.lookup_exact("POST /foobarr");
    REQUIRE(lookup_5 == nullptr);
  }
}
