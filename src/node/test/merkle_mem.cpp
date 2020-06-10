// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "../../ds/logger.h"
#include "../history.h"

#include <algorithm>
#include <random>
#include <sys/resource.h>
#include <sys/time.h>

extern "C"
{
#include <evercrypt/EverCrypt_AutoConfig2.h>
}

using namespace std;

static constexpr size_t appends = 1000000;
static constexpr size_t max_tree_size = 1000;
static constexpr size_t flushes_without_retract = 10;

static constexpr size_t max_expected_rss = 4096;

static size_t get_maxrss()
{
  rusage r;
  auto rc = getrusage(RUSAGE_SELF, &r);
  if (rc != 0)
    throw std::logic_error("getrusage failed");
  return r.ru_maxrss;
}

static crypto::Sha256Hash random_hash(std::random_device& rdev)
{
  crypto::Sha256Hash h;
  for (size_t j = 0; j < crypto::Sha256Hash::SIZE; j++)
    h.h[j] = rdev();
  return h;
}

static int append_flush_and_retract()
{
  ccf::MerkleTreeHistory t;
  std::random_device r;

  for (size_t index = 0; index < appends; ++index)
  {
    auto h = random_hash(r);
    t.append(h);

    if (index > 0 && index % max_tree_size == 0)
    {
      if (index % (flushes_without_retract * max_tree_size) == 0)
      {
        t.retract(index - max_tree_size);
        LOG_DEBUG_FMT("retract() {}", index - max_tree_size);
      }
      else
      {
        t.flush(index - max_tree_size);
        LOG_DEBUG_FMT("flush() {}", index - max_tree_size);
      }
    }
    if (index % (appends / 10) == 0)
      LOG_INFO_FMT("MAX RSS: {}Kb", get_maxrss());
  }
  LOG_INFO_FMT("MAX RSS: {}Kb", get_maxrss());

  return get_maxrss() < max_expected_rss ? 0 : 1;
}

static void rebuilding_from_serialised()
{
  std::random_device r;

  std::vector<uint8_t> serialised_before;
  std::vector<uint8_t> serialised_final;
  std::vector<crypto::Sha256Hash> hashes;

  constexpr size_t flush_index = 4;
  constexpr size_t end_index = 30;

  {
    ccf::MerkleTreeHistory t1;
    for (size_t i = 0; i <= flush_index; ++i)
    {
      hashes.push_back(random_hash(r));
      auto h = hashes.back();
      t1.append(h);
    }

    serialised_before = t1.serialise();
    t1.flush(flush_index);

    for (size_t i = flush_index + 1; i <= end_index; ++i)
    {
      hashes.push_back(random_hash(r));
      auto h = hashes.back();
      t1.append(h);
    }

    serialised_final = t1.serialise();
  }

  std::vector<std::shared_ptr<ccf::MerkleTreeHistory>> trees;

  ccf::MerkleTreeHistory final_tree(serialised_final);
  constexpr auto sign_index = end_index - 5;
  auto receipt = final_tree.get_receipt(sign_index);

  for (size_t i = 0; i < 1; ++i)
  {
    trees.emplace_back(
      std::make_shared<ccf::MerkleTreeHistory>(serialised_before));
    auto& tree = trees.back();

    for (size_t e = flush_index + 1; e < hashes.size(); ++e)
    {
      auto h = hashes[e];
      tree->append(h);
    }

    auto b = tree->verify(receipt);
    assert(b);
  }
}

// We need an explicit main to initialize kremlib and EverCrypt
int main(int argc, char* argv[])
{
  ::EverCrypt_AutoConfig2_init();
  rebuilding_from_serialised();
  return append_flush_and_retract();
}