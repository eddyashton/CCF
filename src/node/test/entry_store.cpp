// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/historical_queries/entry_store.h"

#include "node/historical_queries/receipt_builder.h"

#include <doctest/doctest.h>

using namespace ccf::historical;

static const CompoundHandle HANDLE_A = {RequestNamespace::Application, 1};
static const CompoundHandle HANDLE_B = {RequestNamespace::Application, 2};
static const CompoundHandle HANDLE_C = {RequestNamespace::System, 1};

TEST_CASE("EntryStore: basic add/remove ref")
{
  EntryStore store;

  REQUIRE(store.estimated_size() == 0);

  SUBCASE("add_ref for unknown size does not change estimated_size")
  {
    store.add_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 0);
  }

  SUBCASE("update_raw_size before add_ref counts size immediately")
  {
    // update_raw_size always adjusts estimated_size, regardless of refs.
    // This is by design: in production, add_ref is always called before
    // update_raw_size (entry is tracked before data arrives).
    store.update_raw_size(10, 100);
    REQUIRE(store.estimated_size() == 100);
  }

  SUBCASE("add_ref after update_raw_size double-counts")
  {
    // This documents current behaviour: if update_raw_size is called
    // before add_ref, estimated_size double-counts. In production this
    // order doesn't occur.
    store.update_raw_size(10, 100);
    store.add_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 200);
  }

  SUBCASE("update_raw_size after add_ref includes size")
  {
    store.add_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 0);
    store.update_raw_size(10, 100);
    REQUIRE(store.estimated_size() == 100);
  }

  SUBCASE("remove_ref brings size back to zero")
  {
    store.add_ref(10, HANDLE_A);
    store.update_raw_size(10, 100);
    REQUIRE(store.estimated_size() == 100);
    store.remove_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 0);
  }

  SUBCASE("remove_ref for unknown seqno is a no-op")
  {
    store.remove_ref(999, HANDLE_A);
    REQUIRE(store.estimated_size() == 0);
  }
}

TEST_CASE("EntryStore: multiple handles sharing a seqno")
{
  EntryStore store;

  // Production order: add_ref first, then update_raw_size
  store.add_ref(10, HANDLE_A);
  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);

  SUBCASE("second add_ref for same seqno does not double-count")
  {
    store.add_ref(10, HANDLE_B);
    REQUIRE(store.estimated_size() == 100);
  }

  SUBCASE("removing one handle keeps size when another handle remains")
  {
    store.add_ref(10, HANDLE_B);
    store.remove_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 100);
  }

  SUBCASE("removing all handles brings size to zero")
  {
    store.add_ref(10, HANDLE_B);
    store.remove_ref(10, HANDLE_A);
    store.remove_ref(10, HANDLE_B);
    REQUIRE(store.estimated_size() == 0);
  }

  SUBCASE("same handle adding ref twice is idempotent for size")
  {
    store.add_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 100);
  }
}

TEST_CASE("EntryStore: multiple seqnos")
{
  EntryStore store;

  // Production order: add refs first, then sizes arrive
  store.add_ref(10, HANDLE_A);
  store.add_ref(20, HANDLE_A);
  store.add_ref(30, HANDLE_B);

  store.update_raw_size(10, 100);
  store.update_raw_size(20, 200);
  store.update_raw_size(30, 300);

  REQUIRE(store.estimated_size() == 600);

  SUBCASE("removing refs one at a time decrements correctly")
  {
    store.remove_ref(10, HANDLE_A);
    REQUIRE(store.estimated_size() == 500);

    store.remove_ref(20, HANDLE_A);
    REQUIRE(store.estimated_size() == 300);

    store.remove_ref(30, HANDLE_B);
    REQUIRE(store.estimated_size() == 0);
  }

  SUBCASE("add_refs_for works with TrackedStores map")
  {
    TrackedStores tracked;
    tracked[10] = {nullptr, true};
    tracked[20] = {nullptr, true};

    store.add_refs_for(HANDLE_B, tracked);
    // 10 and 20 now have both HANDLE_A and HANDLE_B
    store.remove_ref(10, HANDLE_A);
    store.remove_ref(20, HANDLE_A);
    // Still referenced by HANDLE_B
    REQUIRE(store.estimated_size() == 600);
  }

  SUBCASE("remove_refs_for removes all refs for a handle")
  {
    TrackedStores tracked;
    tracked[10] = {nullptr, true};
    tracked[20] = {nullptr, true};

    store.remove_refs_for(HANDLE_A, tracked);
    // 30 still referenced by HANDLE_B
    REQUIRE(store.estimated_size() == 300);
  }
}

TEST_CASE("EntryStore: update_raw_size is idempotent for same size")
{
  EntryStore store;

  store.add_ref(10, HANDLE_A);
  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);

  // Same size again — should not change
  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);
}

TEST_CASE("EntryStore: cross-namespace handles are distinct")
{
  EntryStore store;

  // HANDLE_A is Application/1, HANDLE_C is System/1
  store.add_ref(10, HANDLE_A);
  store.add_ref(10, HANDLE_C);
  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);

  store.remove_ref(10, HANDLE_A);
  REQUIRE(store.estimated_size() == 100); // Still held by System/1

  store.remove_ref(10, HANDLE_C);
  REQUIRE(store.estimated_size() == 0);
}

TEST_CASE("EntryStore: size accounting after remove and re-add")
{
  EntryStore store;

  store.add_ref(10, HANDLE_A);
  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);

  store.remove_ref(10, HANDLE_A);
  REQUIRE(store.estimated_size() == 0);

  // Re-add after full removal — raw_store_sizes was erased, need to
  // re-register the size
  store.add_ref(10, HANDLE_A);
  // Size was erased on last remove, so estimated_size stays 0 until
  // update_raw_size is called again (mimics re-fetch)
  REQUIRE(store.estimated_size() == 0);

  store.update_raw_size(10, 100);
  REQUIRE(store.estimated_size() == 100);
}
