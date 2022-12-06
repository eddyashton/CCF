// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include <doctest/doctest.h>

extern "C"
{
#include <lighttpd/radix.h>
}

TEST_CASE("verbatim" * doctest::test_suite("radix"))
{
  auto magic1 = 4235;
  auto magic2 = 59234;
  void* value;

  in_addr_t ip1 = htonl(INADDR_LOOPBACK);
  in_addr_t ip2 = htonl((in_addr_t)0xC0A8007D);
  liRadixTree* rd = li_radixtree_new();

  li_radixtree_insert(rd, &ip1, 32, &magic1);
  li_radixtree_insert(rd, &ip2, 32, &magic2);

  value = li_radixtree_lookup_exact(rd, &ip1, 32);

  REQUIRE(&magic1 == value);

  value = li_radixtree_lookup_exact(rd, &ip2, 32);

  REQUIRE(&magic2 == value);

  li_radixtree_free(rd, NULL, NULL);
}

TEST_CASE("strings" * doctest::test_suite("radix"))
{
  auto tree = li_radixtree_new();

  auto data_a = 42;
  auto data_b = 100;

  const auto k_foo = "POST /foo";
  li_radixtree_insert(tree, k_foo, strlen(k_foo) * 8, &data_a);

  const auto k_bar = "POST /foobar";
  li_radixtree_insert(tree, k_bar, strlen(k_foo) * 8, &data_b);

  auto lookup_0 = li_radixtree_lookup(tree, k_foo, strlen(k_foo) * 8);
  REQUIRE(lookup_0 == &data_a);

  li_radixtree_free(tree, nullptr, nullptr);
}
