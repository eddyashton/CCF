// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "./r3_registry.h"
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

std::set<std::string> all_paths_of_length(size_t target_length)
{
  static Elements elements = {
    // "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"};
    "foo",
    "bar",
    "baz",
    "qux"};
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

template <typename RegistryType>
static void dispatch(picobench::state& s)
{
  RegistryType registry("ignored_prefix");

  auto paths_set = all_paths_of_length(s.iterations());
  std::cout << fmt::format(
                 "Found {} paths of length {}",
                 paths_set.size(),
                 s.iterations())
            << std::endl;

  std::vector<std::string> paths(paths_set.begin(), paths_set.end());
  // Debug printing:
  // if (paths.size() <= 20)
  // {
  //   for (const auto& path : paths)
  //   {
  //     std::cout << fmt::format("  {}", path) << std::endl;
  //   }
  // }
  // else
  // {
  //   auto it = paths.begin();
  //   for (auto i = 0; i < 10; ++i)
  //   {
  //     std::cout << fmt::format("  {}", *it++) << std::endl;
  //   }
  //   std::cout << "  ..." << std::endl;
  //   it = paths.end();
  //   std::advance(it, -10);
  //   while (it != paths.end())
  //   {
  //     std::cout << fmt::format("  {}", *it++) << std::endl;
  //   }
  // }

  const std::string template_id = "{name}";
  for (auto& path : paths)
  {
    registry.make_endpoint(path, HTTP_POST, [](auto& ctx) {}, {}).install();
    const auto start = path.find(template_id);
    if (start != std::string::npos)
    {
      path.replace(start, template_id.size(), "bob");
    }
  }

  ccf::kv::Store store;
  ccf::kv::CommittableTx tx = store.create_tx();
  StubRpcContext rpc_ctx;

  rpc_ctx.verb = HTTP_POST;

  std::random_shuffle(paths.begin(), paths.end());

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
      if (endpoint == nullptr)
      {
        fmt::print("{} FAILED\n", rpc_ctx.request_path);
      }
      else
      {
        fmt::print(
          "{} => {}\n", rpc_ctx.request_path, endpoint->dispatch.uri_path);
      }
    }
  }
  s.stop_timer();
}

// const std::vector<int> dispatch_sizes = {1, 2, 3, 4, 5};
const std::vector<int> dispatch_sizes = {1};

PICOBENCH_SUITE("dispatch");
// auto dispatch_old = dispatch<ccf::endpoints::EndpointRegistry>;
// PICOBENCH(dispatch_old).iterations(dispatch_sizes).baseline();
auto dispatch_new = dispatch<R3Registry>;
PICOBENCH(dispatch_new).iterations(dispatch_sizes);
