// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "../../ds/logger.h"
#include "../history.h"
#include "ds/stacktrace_utils.h"

#include <algorithm>
#include <random>
#include <sys/resource.h>
#include <sys/time.h>

extern "C"
{
#include <evercrypt/EverCrypt_AutoConfig2.h>
}

using namespace std;

static constexpr size_t appends = 1'000'000;
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
    {
      LOG_INFO_FMT("At {}", index);
      LOG_INFO_FMT("  MAX RSS: {}Kb", get_maxrss());
      const auto serialised = t.serialise();
      LOG_INFO_FMT("  SERIALISED: {}Kb", serialised.size() / 1024);
      const auto receipt = t.get_receipt(t.end_index());
      LOG_INFO_FMT("  SERIALISED RECEIPT: {}bytes", receipt.to_v().size());
    }
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

  constexpr size_t end_index = 24;

  {
    ccf::MerkleTreeHistory t1;
    t1.print();
    t1.get_root();
    serialised_before = t1.serialise();

    for (size_t i = 0; i <= end_index; ++i)
    {
      hashes.push_back(random_hash(r));
      auto h = hashes.back();
      LOG_INFO_FMT("Append original {}", i);
      t1.append(h);
      t1.print();
    }

    serialised_final = t1.serialise();
  }

  ccf::MerkleTreeHistory final_tree(serialised_final);

  ccf::MerkleTreeHistory reconstruction(serialised_before);
  reconstruction.print();
  for (size_t i = 0; i < hashes.size(); ++i)
  {
    auto h = hashes[i];
    reconstruction.append(h);
    LOG_INFO_FMT("Append reconstruction {}", i);
    reconstruction.print();
  }
}

// We need an explicit main to initialize kremlib and EverCrypt
int main(int argc, char* argv[])
{
  ::EverCrypt_AutoConfig2_init();
  stacktrace::init_sig_handlers();
  rebuilding_from_serialised();
  return append_flush_and_retract();
}