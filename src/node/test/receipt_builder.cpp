// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

// These tests exercise the forward-scan logic of build_receipt_for_seqno
// in isolation, without needing real KV stores or Merkle trees.
// The fill_receipts_from_signature backward walk requires real signatures
// and is covered by the integration tests in historical_queries.cpp.

#include "node/historical_queries/receipt_builder.h"

#include "kv/test/null_encryptor.h"

#include <doctest/doctest.h>

using namespace ccf::historical;

// Create a minimal StoreDetails that looks "fetched" (store != nullptr)
static StoreDetailsPtr make_fetched_details(bool is_sig = false)
{
  auto details = std::make_shared<StoreDetails>();
  // We need a non-null store to indicate "fetched". Create a minimal one.
  details->store = std::make_shared<ccf::kv::Store>(false, true);
  details->store->set_encryptor(std::make_shared<ccf::kv::NullTxEncryptor>());
  details->current_stage = StoreStage::Trusted;
  details->is_signature = is_sig;
  return details;
}

// Create a StoreDetails in Fetching state (store == nullptr)
static StoreDetailsPtr make_fetching_details()
{
  return std::make_shared<StoreDetails>();
}

// Helper: insert a StoreDetails into both all_stores and tracked_stores
static void insert_entry(
  AllRequestedStores& all,
  TrackedStores& tracked,
  ccf::SeqNo seq,
  StoreDetailsPtr details,
  bool user_requested = true)
{
  all[seq] = details;
  tracked[seq] = {details, user_requested};
}

TEST_CASE("build_receipt_for_seqno: target not yet fetched")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto details = make_fetching_details();
  insert_entry(all, tracked, 10, details);

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.empty());
}

TEST_CASE("build_receipt_for_seqno: target not in all_stores")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.empty());
}

TEST_CASE("build_receipt_for_seqno: gap immediately after target")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);
  // Seqno 11 does not exist — gap

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.size() == 1);
  REQUIRE(result.supporting_seqnos.count(11) == 1);
}

TEST_CASE("build_receipt_for_seqno: fetching entry treated as gap")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);

  auto d11 = make_fetching_details();
  insert_entry(all, tracked, 11, d11);

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.size() == 1);
  REQUIRE(result.supporting_seqnos.count(11) == 1);
}

TEST_CASE("build_receipt_for_seqno: contiguous data then gap")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  auto d11 = make_fetched_details(false);
  auto d12 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);
  insert_entry(all, tracked, 11, d11);
  insert_entry(all, tracked, 12, d12);
  // Gap at 13

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.size() == 1);
  REQUIRE(result.supporting_seqnos.count(13) == 1);
}

TEST_CASE("build_receipt_for_seqno: target is signature — no supporting needed")
{
  AllRequestedStores all;
  TrackedStores tracked;

  // A signature entry. fill_receipts_from_signature will be called but
  // will return false (no real sig data in our mock). That's fine —
  // we're testing that the forward scan doesn't run for signatures.
  auto d10 = make_fetched_details(true);
  d10->transaction_id = {2, 10};
  insert_entry(all, tracked, 10, d10);

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.empty());
}

TEST_CASE("build_receipt_for_seqno: finds signature after data entries")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  auto d11 = make_fetched_details(false);
  auto d12 = make_fetched_details(true); // signature
  d12->transaction_id = {2, 12};
  insert_entry(all, tracked, 10, d10);
  insert_entry(all, tracked, 11, d11);
  insert_entry(all, tracked, 12, d12);

  // With mock signature (no real KV data), fill_receipts_from_signature
  // returns false. But the forward scan still terminates at the sig.
  // The throw only fires when the target is in tracked_stores AND the
  // signature doesn't cover it. Since our mock sig can't cover anything,
  // remove target from tracked_stores to avoid the throw.
  // In production, real signatures always cover the preceding entries.
  tracked.erase(10);

  auto result = build_receipt_for_seqno(10, all, tracked);
  // Signature found — no gaps needed.
  REQUIRE(result.supporting_seqnos.empty());
}

TEST_CASE("build_receipt_for_seqno: only scans all_stores, not tracked_stores")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);

  // 11 is in tracked_stores but NOT in all_stores
  auto d11 = make_fetched_details(false);
  tracked[11] = {d11, false};
  // Not in all — forward scan should miss it

  auto result = build_receipt_for_seqno(10, all, tracked);
  // Gap at 11 from all_stores perspective
  REQUIRE(result.supporting_seqnos.size() == 1);
  REQUIRE(result.supporting_seqnos.count(11) == 1);
}

TEST_CASE("build_receipt_for_seqno: expired weak pointer treated as gap")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);

  {
    // Create and immediately destroy — weak pointer expires
    auto d11 = make_fetched_details(false);
    all[11] = d11;
  }
  // d11 is destroyed, all[11] weak_ptr is expired

  auto result = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(result.supporting_seqnos.size() == 1);
  REQUIRE(result.supporting_seqnos.count(11) == 1);
}

TEST_CASE("build_receipt_for_seqno: multiple calls extend chain")
{
  AllRequestedStores all;
  TrackedStores tracked;

  auto d10 = make_fetched_details(false);
  insert_entry(all, tracked, 10, d10);

  // First call — gap at 11
  auto r1 = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(r1.supporting_seqnos == std::set<ccf::SeqNo>{11});

  // Simulate: entry 11 is fetched (data, not sig)
  auto d11 = make_fetched_details(false);
  insert_entry(all, tracked, 11, d11, false);

  // Second call — now 11 has data, gap at 12
  auto r2 = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(r2.supporting_seqnos == std::set<ccf::SeqNo>{12});

  // Simulate: entry 12 is fetched (signature)
  auto d12 = make_fetched_details(true);
  d12->transaction_id = {2, 12};
  insert_entry(all, tracked, 12, d12, false);

  // Third call — signature found, no more supporting needed.
  // Remove target from tracked_stores since our mock signature has no
  // real KV data and can't actually cover the entry.
  tracked.erase(10);
  auto r3 = build_receipt_for_seqno(10, all, tracked);
  REQUIRE(r3.supporting_seqnos.empty());
}
