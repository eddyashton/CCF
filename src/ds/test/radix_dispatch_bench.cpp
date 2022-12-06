// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ds/radix_tree.h"

#define PICOBENCH_IMPLEMENT_WITH_MAIN
#include <picobench/picobench.hpp>

static const std::vector<std::string> base_paths = {
  "/foo", "/bar", "/baz", "/qux", "/quux", "/corge", "/grault", "/garply"};
static const size_t real_data = 42;

std::vector<std::string> extend_paths(const std::vector<std::string>& in)
{
  std::vector<std::string> out;
  for (const auto& p : in)
  {
    for (const auto& n : base_paths)
    {
      out.push_back(p + n);
    }
  }
  return out;
}

std::vector<std::string> generate_paths(size_t depth)
{
  std::vector<std::string> paths = base_paths;
  for (auto i = 1; i < depth; ++i)
  {
    paths = extend_paths(paths);
  }
  return paths;
}

ds::RadixTree construct_tree(const std::vector<std::string>& paths)
{
  ds::RadixTree tree;
  for (const auto& p : paths)
  {
    tree.insert(p, &real_data);
  }
  return tree;
}

static void dispatch_10k_paths(picobench::state& s)
{
  auto paths = generate_paths(s.iterations());
  auto tree = construct_tree(paths);

  s.start_timer();
  for (auto i = 0; i < 10'000; ++i)
  {
    const auto path = paths[i % paths.size()];
    auto d = tree.prefix_lookup(path);
    if (d != &real_data)
    {
      throw std::logic_error("Radix tree returned corrupt data!");
    }
  }
  s.stop_timer();
}

const std::vector<int> dispatch_sizes = {
  //
  1,
  2,
  3,
  4,
  5,
  //
};

PICOBENCH_SUITE("radix_dispatch");
// auto hash_vec = hash<std::vector<uint8_t>>;
PICOBENCH(dispatch_10k_paths).iterations(dispatch_sizes).baseline();
// auto hash_small_vec_16 = hash<llvm_vecsmall::SmallVector<uint8_t, 16>>;
// PICOBENCH(hash_small_vec_16).iterations(dispatch_sizes).baseline();
// auto hash_small_vec_128 = hash<llvm_vecsmall::SmallVector<uint8_t, 128>>;
// PICOBENCH(hash_small_vec_128).iterations(dispatch_sizes).baseline();
