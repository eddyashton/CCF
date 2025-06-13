// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "./path_dispatch_registry.h"
#include "./stub_rpc_context.h"
#include "node/rpc/frontend.h"

#define PICOBENCH_IMPLEMENT_WITH_MAIN
#include <picobench/picobench.hpp>

using Elements = std::vector<std::string>;

bool next_choice(
  const Elements& e, std::vector<Elements::const_iterator>& current_choice)
{
  auto it = current_choice.end();
  while (it != current_choice.begin())
  {
    std::advance(it, -1);

    const auto candidate = std::next(*it);
    if (candidate == e.end())
    {
      continue;
    }
    else
    {
      while (it != current_choice.end())
      {
        *it++ = candidate;
      }
      return true;
    }
  }

  return false;
}

static Elements elements = {
  "foo", "fooo", "foooo", "fooooo", "foooooo", "fooooooo", "bar", "baz"};

std::set<std::string> all_paths_of_length(size_t target_length)
{
  std::sort(elements.begin(), elements.end());

  using Result = std::set<std::string>;
  static std::map<size_t, Result> memo_results;

  auto it = memo_results.find(target_length);
  if (it == memo_results.end())
  {
    Result paths;
    if (target_length == 0)
    {
      paths.insert("");
    }
    else
    {
      Result shorter_paths = all_paths_of_length(target_length - 1);
      for (const auto& prefix : shorter_paths)
      {
        for (const auto& e : elements)
        {
          paths.insert(fmt::format("{}/{}", prefix, e));
        }
      }
    }
    it = memo_results.emplace_hint(it, target_length, paths);
  }

  return it->second;
}

std::set<std::string> all_paths_up_to_length(size_t target_length)
{
  std::set<std::string> paths;
  for (size_t i = 0; i < target_length + 1; ++i)
  {
    const auto more_paths = all_paths_of_length(i);
    paths.insert(more_paths.begin(), more_paths.end());
  }
  return paths;
}

std::set<std::string> first_n_paths(size_t n)
{
  std::set<std::string> paths;

  size_t path_length = 1;
  while (paths.size() < n)
  {
    const auto next_paths = all_paths_of_length(path_length);
    auto it = next_paths.begin();
    while (it != next_paths.end() && paths.size() < n)
    {
      paths.insert(*it++);
    }
    ++path_length;
  }

  return paths;
}

template <typename It>
void debug_print_paths(It begin, It end)
{
  auto it = begin;
  if (std::distance(begin, end) <= 20)
  {
    while (it != end)
    {
      std::cout << fmt::format("  {}", *it++) << std::endl;
    }
  }
  else
  {
    for (auto i = 0; i < 10; ++i)
    {
      std::cout << fmt::format("  {}", *it++) << std::endl;
    }
    std::cout << "  ..." << std::endl;
    it = end;
    std::advance(it, -10);
    while (it != end)
    {
      std::cout << fmt::format("  {}", *it++) << std::endl;
    }
  }
}

template <typename RegistryType>
static void dispatch(picobench::state& s)
{
  RegistryType registry("ignored_prefix");

  auto paths_set = first_n_paths(s.iterations());

  // std::cout << fmt::format("First {} paths", s.iterations()) << std::endl;
  // debug_print_paths(paths_set.begin(), paths_set.end());

  std::vector<std::string> paths(paths_set.begin(), paths_set.end());

  for (auto& path : paths)
  {
    registry.make_endpoint(path, HTTP_POST, [](auto& ctx) {}, {}).install();
  }

  ccf::kv::Store store;
  ccf::kv::CommittableTx tx = store.create_tx();
  StubRpcContext rpc_ctx;

  rpc_ctx.verb = HTTP_POST;

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(paths.begin(), paths.end(), g);

  s.start_timer();
  for (size_t i = 0; i < 1; ++i)
  {
    // // Choose a random path
    // auto it = paths.begin();
    // std::advance(it, rand() % paths.size());
    // rpc_ctx.request_path = *it;
    for (const auto& path : paths)
    {
      rpc_ctx.request_path = path;

      auto endpoint = registry.find_endpoint(tx, rpc_ctx);
    }
  }
  s.stop_timer();
}

std::string replaced_with_template(
  std::string path, const std::string_view& target)
{
  size_t found = 0;
  const size_t len = target.size();

  auto match_start = path.find(target);
  while (match_start != std::string::npos)
  {
    const auto slug = std::string("{") + fmt::format("name_{}", found++) + "}";
    path.replace(match_start, len, slug);
    match_start = path.find(target, match_start);
  }

  return path;
}

struct AllRegexCollection
{
  std::unordered_map<std::string, std::regex> paths;

  void _insert(
    const std::string& s,
    std::optional<ccf::endpoints::PathTemplateSpec> spec_opt)
  {
    auto regex =
      spec_opt.has_value() ? spec_opt->template_regex : std::regex(s);

    paths[s] = regex;
  }

  void insert(const std::string& s)
  {
    _insert(s, ccf::endpoints::PathTemplateSpec::parse(s));
  }

  std::optional<std::string> lookup(const std::string& s)
  {
    std::smatch match;
    for (const auto& [key, regex] : paths)
    {
      if (std::regex_match(s, match, regex))
      {
        return key;
      }
    }

    return std::nullopt;
  }
};

struct PrefixedRegexCollection
{
  struct Entry
  {
    std::string source_key;
    std::regex regex;
  };

  using PrefixMap = std::unordered_map<std::string, std::vector<Entry>>;

  std::map<size_t, PrefixMap> prefix_sizes;

  void _insert(
    const std::string& s, std::optional<ccf::endpoints::PathTemplateSpec> _)
  {
    const auto prefix_len = s.find_first_of("{");
    const auto prefix = s.substr(0, prefix_len);
    const auto remainder = s.substr(prefix_len);

    auto remainder_spec = ccf::endpoints::PathTemplateSpec::parse(remainder);

    auto regex = remainder_spec.has_value() ? remainder_spec->template_regex :
                                              std::regex();

    prefix_sizes[prefix_len][prefix].push_back(Entry{s, regex});
  }

  std::optional<std::string> lookup(const std::string& s)
  {
    auto size_it =
      std::make_reverse_iterator(prefix_sizes.lower_bound(s.size()));
    std::smatch match;
    while (size_it != prefix_sizes.rend())
    {
      const auto& [size, entries] = *size_it;
      const auto prefix = s.substr(0, size);
      const auto remainder = s.substr(size);

      const auto entry_it = entries.find(prefix);
      if (entry_it != entries.end())
      {
        for (const auto& entry : entry_it->second)
        {
          if (std::regex_match(remainder, match, entry.regex))
          {
            return entry.source_key;
          }
        }
      }
      ++size_it;
    }

    return std::nullopt;
  }
};

template <typename BaseCollection>
struct PureShortcut : public BaseCollection
{
  std::set<std::string> pure_paths;

  void insert(const std::string& s)
  {
    auto spec_opt = ccf::endpoints::PathTemplateSpec::parse(s);
    if (!spec_opt.has_value())
    {
      pure_paths.insert(s);
    }
    else
    {
      BaseCollection::_insert(s, spec_opt);
    }
  }

  std::optional<std::string> lookup(const std::string& s)
  {
    const auto it = pure_paths.find(s);
    if (it != pure_paths.end())
    {
      return s;
    }

    return BaseCollection::lookup(s);
  }
};

template <typename CollectionType>
static void regex_lookup(picobench::state& s)
{
  auto paths_set = first_n_paths(s.iterations());
  std::vector<std::string> paths(paths_set.begin(), paths_set.end());

  CollectionType collection;
  for (auto& path : paths)
  {
    collection.insert(replaced_with_template(path, "bar"));
  }

  // std::cout << fmt::format("Produced {} templated paths", s.iterations())
  //           << std::endl;
  // debug_print_paths(paths.begin(), paths.end());

  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(paths.begin(), paths.end(), g);

  s.start_timer();
  for (size_t i = 0; i < 1; ++i)
  {
    for (const auto& path : paths)
    {
      const auto res = collection.lookup(path);
      if (!res.has_value())
      {
        throw std::logic_error(fmt::format("Failed lookup for: {}", path));
      }
    }
  }
  s.stop_timer();
}

const std::vector<int> dispatch_sizes = {10, 100, 1'000, 10'000};

PICOBENCH_SUITE("dispatch");
auto dispatch_old = dispatch<ccf::endpoints::EndpointRegistry>;
PICOBENCH(dispatch_old).iterations(dispatch_sizes).baseline();
// auto dispatch_new = dispatch<R3Registry>;
// PICOBENCH(dispatch_new).iterations(dispatch_sizes);
auto dispatch_new = dispatch<NewRegistry>;
PICOBENCH(dispatch_new).iterations(dispatch_sizes);

const std::vector<int> regex_sizes = {10, 100, 1'000, 10'000};

PICOBENCH_SUITE("regex_lookup");
auto current = regex_lookup<PureShortcut<AllRegexCollection>>;
PICOBENCH(current).iterations(regex_sizes).baseline();
auto all_regex = regex_lookup<AllRegexCollection>;
PICOBENCH(all_regex).iterations(regex_sizes);
auto prefixed_regex = regex_lookup<PureShortcut<PrefixedRegexCollection>>;
PICOBENCH(prefixed_regex).iterations(regex_sizes);