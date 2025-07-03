// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include <charconv>
#include <regex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace ccf::nonstd::numsort
{
  using Element = std::variant<size_t, std::string>;
  using Elements = std::vector<Element>;

  inline Elements to_elements(std::string_view s)
  {
    static std::regex re("[[:digit:]]+");

    auto nums_begin = std::cregex_iterator(s.begin(), s.end(), re);
    auto nums_end = std::cregex_iterator();

    Elements ret;
    size_t cursor = 0;

    for (auto it = nums_begin; it != nums_end; ++it)
    {
      std::cmatch match = *it;

      const auto position = match.position();
      if (position != cursor)
      {
        ret.emplace_back(std::string(s.substr(cursor, position - cursor)));
      }

      size_t n;
      const auto begin = s.data() + match.position();
      const auto end = begin + match.length();
      const auto [p, ec] = std::from_chars(begin, end, n);
      if (ec != std::errc())
      {
        throw std::runtime_error(fmt::format(
          "Unable to parse substring \"{}\" (from \"{}\") as a size_t",
          match.str(),
          s));
      }

      ret.emplace_back(n);

      cursor = position + match.length();
    }

    if (cursor != s.size())
    {
      ret.emplace_back(std::string(s.substr(cursor)));
    }

    return ret;
  }

  template <class RandomIt>
  void numsort(RandomIt first, RandomIt last)
  {
    std::unordered_map<std::string, Elements> cache;

    for (auto it = first; it != last; ++it)
    {
      cache[*it] = to_elements(*it);
    }

    auto comparator = [&cache](const auto& l, const auto& r) {
      return cache[l] < cache[r];
    };

    std::sort(first, last, comparator);
  }
}
