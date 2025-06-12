// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

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

std::set<std::string> all_paths_of_length(
  size_t target_length, const Elements& elements)
{
  std::set<std::string> paths;

  std::vector<Elements::const_iterator> current;
  for (size_t i = 0; i < target_length; ++i)
  {
    current.push_back(elements.begin());
  }

  do
  {
    do
    {
      std::string path;
      for (const auto& it : current)
      {
        path += fmt::format("/{}", *it);
      }
      paths.insert(path);
    } while (std::next_permutation(current.begin(), current.end()));
  } while (next_choice(elements, current));

  return paths;
}

static void dispatch(picobench::state& s)
{
  ccf::endpoints::EndpointRegistry registry("ignored_prefix");

  Elements elements = {
    "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"};
  std::sort(elements.begin(), elements.end());

  const auto paths = all_paths_of_length(s.iterations(), elements);
  std::cout << fmt::format(
                 "Produced {} paths of length {}", paths.size(), s.iterations())
            << std::endl;
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

  for (const auto& path : paths)
  {
    registry.make_endpoint(path, HTTP_POST, [](auto& ctx) {}, {}).install();
  }

  ccf::kv::Store store;
  ccf::kv::CommittableTx tx = store.create_tx();
  StubRpcContext rpc_ctx;

  rpc_ctx.verb = HTTP_POST;

  s.start_timer();
  for (size_t i = 0; i < 1000; ++i)
  {
    // Choose a random path
    auto it = paths.begin();
    std::advance(it, rand() % paths.size());
    rpc_ctx.request_path = *it;
    auto _ = registry.find_endpoint(tx, rpc_ctx);
  }
  s.stop_timer();
}

const std::vector<int> dispatch_sizes = {1, 2, 3, 4, 5};

PICOBENCH_SUITE("dispatch");
PICOBENCH(dispatch).iterations(dispatch_sizes).baseline();
