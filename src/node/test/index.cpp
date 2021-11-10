// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/index.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

template <typename T>
T merger(const T& a, const T& b)
{
  T r;
  std::merge(
    a.begin(), a.end(), b.begin(), b.end(), std::inserter(r, r.begin()));
  return r;
}

template <typename T>
T map_merger(const T& a, const T& b)
{
  T r;
  auto out = std::inserter(r, r.begin());

  {
    auto a_begin = a.begin();
    const auto a_end = a.end();
    auto b_begin = b.begin();
    const auto b_end = b.end();

    for (; a_begin != a_end; ++out)
    {
      if (b_begin == b_end)
      {
        std::copy(a_begin, a_end, out);
        break;
      }
      if (b_begin->first < a_begin->first)
      {
        *out = *b_begin;
        ++b_begin;
      }
      else if (a_begin->first < b_begin->first)
      {
        *out = *a_begin;
        ++a_begin;
      }
      else
      {
        *out = std::make_pair(
          a_begin->first, merger(a_begin->second, b_begin->second));
        ++a_begin;
        ++b_begin;
      }
    }
    std::copy(b_begin, b_end, out);
  }

  return r;
}

// These functions may be helpful for debugging
template <typename T>
std::string format_result(const T& t)
{
  std::string result_s = "[Unformattable]";

  if constexpr (std::is_same_v<T, std::set<size_t>>)
  {
    result_s = fmt::format("{}", fmt::join(t, ", "));
  }
  if constexpr (std::is_same_v<T, std::map<std::string, std::set<size_t>>>)
  {
    result_s = "";
    for (const auto& [k, vs] : t)
    {
      result_s += fmt::format("\n    {} = {}", k, fmt::join(vs, ", "));
    }
  }

  return result_s;
}

template <typename T>
void print_index(const ccf::historical::Index<T>& i)
{
  fmt::print("{} sub-range(s)\n", i.sub_ranges.size());
  for (const auto& sr : i.sub_ranges)
  {
    fmt::print(
      "  [{}, {}]: {}\n", sr.range.min, sr.range.max, format_result(sr.result));
  }
}

template <typename T>
void print_lookup(ccf::historical::Index<T>& i, const ccf::historical::Range& r)
{
  fmt::print("Result for [{}, {}]:\n", r.min, r.max);

  const auto t = i.lookup(r);
  if (t.has_value())
  {
    fmt::print("{}\n", format_result(*t));
  }
  else
  {
    fmt::print("  No result!\n");
  }
}

TEST_CASE("Set-based index")
{
  using TResult = std::set<size_t>;
  ccf::historical::Index<TResult> i(merger<TResult>);

  // NB: Resulting index has a hole at index 2, where nothing has been recorded
  i.extend_index({3, 6}, {5, 6});
  i.extend_index({10, 15}, {11, 12, 13});
  i.extend_index({1, 1}, {1});
  i.extend_index({5, 8}, {5, 6, 8});
  i.extend_index({9, 9}, {});
  i.extend_index({8, 11}, {8, 10, 11});

  {
    const auto res = i.lookup({2, 2});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({2, 3});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({3, 3});
    REQUIRE(res.has_value());
    REQUIRE_FALSE(res->contains(3));
  }

  {
    const auto res = i.lookup({4, 6});
    REQUIRE(res.has_value());
    REQUIRE_FALSE(res->contains(4));
    REQUIRE(res->contains(5));
    REQUIRE(res->contains(6));
  }

  {
    const auto res = i.lookup({1, 1});
    REQUIRE(res.has_value());
    REQUIRE(res->contains(1));
  }

  {
    const auto res = i.lookup({1, 3});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({9, 9});
    REQUIRE(res.has_value());
    REQUIRE_FALSE(res->contains(7));
    REQUIRE(res->contains(8));
    REQUIRE_FALSE(res->contains(9));
  }

  {
    const auto res = i.lookup({7, 9});
    REQUIRE(res.has_value());
    REQUIRE_FALSE(res->contains(9));
  }

  {
    const auto res = i.lookup({1, 15});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({3, 15});
    REQUIRE(res.has_value());

    REQUIRE_FALSE(res->contains(3));
    REQUIRE_FALSE(res->contains(4));
    REQUIRE(res->contains(5));
    REQUIRE(res->contains(6));
    REQUIRE_FALSE(res->contains(7));
    REQUIRE(res->contains(8));
    REQUIRE_FALSE(res->contains(9));
    REQUIRE(res->contains(10));
    REQUIRE(res->contains(11));
    REQUIRE(res->contains(12));
    REQUIRE(res->contains(13));
    REQUIRE_FALSE(res->contains(14));
    REQUIRE_FALSE(res->contains(15));
  }
}

TEST_CASE("Map-based index")
{
  using TResult = std::map<std::string, std::set<size_t>>;
  ccf::historical::Index<TResult> i(map_merger<TResult>);

  i.extend_index({3, 6}, {{"a", {3, 5}}, {"b", {6}}});
  i.extend_index({10, 15}, {{"a", {11, 12, 13}}});
  i.extend_index({1, 1}, {{"b", {1}}});
  i.extend_index({5, 8}, {{"a", {5, 6, 8}}});
  i.extend_index({9, 9}, {});
  i.extend_index({8, 11}, {{"a", {8, 10}}, {"b", {11}}});

  {
    const auto res = i.lookup({1, 1});
    REQUIRE(res.has_value());
    REQUIRE(res->find("a") == res->end());
    auto b_it = res->find("b");
    REQUIRE(b_it != res->end());
    REQUIRE(b_it->second.size() == 1);
    REQUIRE(*b_it->second.begin() == 1);
  }

  {
    const auto res = i.lookup({8, 11});
    REQUIRE(res.has_value());
    auto a_it = res->find("a");
    REQUIRE(a_it != res->end());
    {
      const auto& as = a_it->second;
      REQUIRE(as.contains(8));
      REQUIRE_FALSE(as.contains(9));
      REQUIRE(as.contains(10));
      REQUIRE(as.contains(11));
    }
    auto b_it = res->find("b");
    REQUIRE(b_it != res->end());
    {
      const auto& bs = b_it->second;
      REQUIRE_FALSE(bs.contains(8));
      REQUIRE_FALSE(bs.contains(9));
      REQUIRE_FALSE(bs.contains(10));
      REQUIRE(bs.contains(11));
    }
  }

  {
    const auto res = i.lookup({4, 9});
    REQUIRE(res.has_value());
    auto a_it = res->find("a");
    REQUIRE(a_it != res->end());
    {
      const auto& as = a_it->second;
      REQUIRE_FALSE(as.contains(4));
      REQUIRE(as.contains(5));
      REQUIRE(as.contains(6));
      REQUIRE_FALSE(as.contains(7));
      REQUIRE(as.contains(8));
      REQUIRE_FALSE(as.contains(9));
    }
    auto b_it = res->find("b");
    REQUIRE(b_it != res->end());
    {
      const auto& bs = b_it->second;
      REQUIRE_FALSE(bs.contains(4));
      REQUIRE_FALSE(bs.contains(5));
      REQUIRE(bs.contains(6));
      REQUIRE_FALSE(bs.contains(7));
      REQUIRE_FALSE(bs.contains(8));
      REQUIRE_FALSE(bs.contains(9));
    }
  }

  {
    const auto res = i.lookup({1, 3});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({1, 2});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({2, 3});
    REQUIRE_FALSE(res.has_value());
  }

  {
    const auto res = i.lookup({2, 2});
    REQUIRE_FALSE(res.has_value());
  }
}