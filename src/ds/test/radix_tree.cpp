// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ds/radix_tree.h"

#include <doctest/doctest.h>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

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

struct DispatchTree
{
  ds::RadixTree root;

  using SubTree = std::pair<std::string, ds::RadixTree*>;

  std::set<const void*> subtrees;

  ~DispatchTree()
  {
    for (auto* v : subtrees)
    {
      auto st = (SubTree*)v;
      delete st->second;
      delete st;
    }
  }

  void insert(const std::string_view& uri, void* value)
  {
    ds::RadixTree* current = &root;

    auto template_end = 0;
    auto template_start = uri.find_first_of('{');
    while (template_start != std::string::npos)
    {
      const auto prefix =
        uri.substr(template_end, template_start - template_end);

      template_end = uri.find_first_of('}', template_start);
      if (template_end == std::string::npos)
      {
        throw std::logic_error(fmt::format(
          "Invalid templated path - missing closing curly bracket: {}", uri));
      }

      const auto element_name =
        uri.substr(template_start + 1, template_end - template_start - 1);

      fmt::print("Found another element: {}\n", element_name);
      fmt::print("Follows prefix: {}\n", prefix);

      auto prev = current->exact_lookup(prefix);
      if (prev != nullptr)
      {
        auto it = subtrees.find(prev);
        if (it != subtrees.end())
        {
          auto subtree = (SubTree*)prev;
          if (subtree->first != element_name)
          {
            throw std::logic_error(fmt::format(
              "Ambiguous templated path - found 2 templated entries at the "
              "same level ({} and {}, while parsing {})",
              element_name,
              subtree->first,
              uri));
          }
          else
          {
            // Found a matching prefix so far, use existing tree
            current = subtree->second;
          }
        }
        else
        {
          throw std::logic_error(fmt::format(
            "Ambiguous templated path - found templated entry ({}) and "
            "complete path at the same level, when parsing {}",
            element_name,
            uri));
        }
      }
      else
      {
        // Insert new element
        auto next = new SubTree(element_name, new ds::RadixTree());
        subtrees.insert(next);
        current->insert(prefix, next);
        current = next->second;
      }

      template_end += 1;
      template_start = uri.find_first_of('{', template_end + 1);
    }

    current->insert(uri.substr(template_end), value);
  }
};

TEST_CASE("foo")
{
  {
    DispatchTree dt;

    dt.insert("/{id_a}", nullptr);
    REQUIRE_THROWS_AS(dt.insert("/{id_b}", nullptr), std::logic_error);
  }

  {
    DispatchTree dt;

    dt.insert("/foo/{id_a}", nullptr);
    REQUIRE_THROWS_AS(dt.insert("/foo/{id_b}", nullptr), std::logic_error);
  }

  {
    DispatchTree dt;

    dt.insert("/foo/bar", (void*)0x1);
    // REQUIRE_THROWS_AS(dt.insert("/foo/{id}", (void*)0x2), std::logic_error);
  }

  DispatchTree dt;

  dt.insert("/foo", (void*)0x1);
  dt.insert("/foo/bar", (void*)0x2);
  dt.insert("/foo/bar/{id}/baragain", (void*)0x3);
  dt.insert("/foo/bar/{id}/baz", (void*)0x4);
  dt.insert("/bar", (void*)0x5);
  // dt.insert("/hello/world/{id}/and/{then}/{it}/gets/{complicated}");
}