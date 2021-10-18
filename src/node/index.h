#pragma once

#include <optional>
#include <tuple>
#include <vector>

namespace ccf::historical
{
  struct Range
  {
    size_t min;
    size_t max;
  };

  template <typename Result>
  class Index
  {
  public:
    Result combine_results(const Result& a, const Result& b)
    {
      Result r;
      std::merge(
        a.begin(), a.end(), b.begin(), b.end(), std::inserter(r, r.begin()));
      return r;
    }

    struct PartialIndex
    {
      Range range;
      Result result;
    };
    std::vector<PartialIndex> sub_ranges;

    static bool intersects_or_adjacent(const Range& a, const Range& b)
    {
      return !((a.max + 1) < b.min || (b.max + 1) < a.min);
    }

  public:
    std::optional<Result> lookup_index(Range range) {}

    void extend_index(Range new_range, Result new_result)
    {
      auto it = sub_ranges.begin();
      while (it != sub_ranges.end())
      {
        auto next_it = std::next(it);
        if (intersects_or_adjacent(it->range, new_range))
        {
          it->range.min = std::min(it->range.min, new_range.min);
          it->range.max = std::max(it->range.max, new_range.max);
          it->result = combine_results(it->result, new_result);

          // This may have extended to merge with the following sub index
          if (next_it != sub_ranges.end())
          {
            if (intersects_or_adjacent(it->range, next_it->range))
            {
              it->range.min = std::min(it->range.min, next_it->range.min);
              it->range.max = std::max(it->range.max, next_it->range.max);
              it->result = combine_results(it->result, next_it->result);

              it = sub_ranges.erase(next_it);
            }
          }
          return;
        }

        if (next_it != sub_ranges.end())
        {
          if (next_it->range.min > (new_range.max + 1))
          {
            break;
          }
        }

        ++it;
      }

      sub_ranges.insert(it, {new_range, new_result});
    }
  };
}
