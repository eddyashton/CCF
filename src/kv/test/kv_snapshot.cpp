// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "kv/kv_serialiser.h"
#include "kv/store.h"
#include "kv/test/null_encryptor.h"
#include "kv/tx.h"

#include <doctest/doctest.h>

struct MapTypes
{
  using StringString = kv::Map<std::string, std::string>;
  using NumNum = kv::Map<size_t, size_t>;
};

TEST_CASE("Simple snapshot" * doctest::test_suite("snapshot"))
{
  kv::Store store;
  auto& string_map = store.create<MapTypes::StringString>(
    "string_map", kv::SecurityDomain::PUBLIC);
  auto& num_map =
    store.create<MapTypes::NumNum>("num_map", kv::SecurityDomain::PUBLIC);

  kv::Version first_snapshot_version = kv::NoVersion;
  kv::Version second_snapshot_version = kv::NoVersion;

  INFO("Apply transactions to original store");
  {
    auto tx1 = store.create_tx();
    auto view_1 = tx1.get_view(string_map);
    view_1->put("foo", "bar");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);
    first_snapshot_version = tx1.commit_version();

    auto tx2 = store.create_tx();
    auto view_2 = tx2.get_view(num_map);
    view_2->put(42, 123);
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);
    second_snapshot_version = tx2.commit_version();

    auto tx3 = store.create_tx();
    auto view_3 = tx1.get_view(string_map);
    view_3->put("key", "not committed");
    // Do not commit tx3
  }

  auto first_snapshot = store.serialise_snapshot(first_snapshot_version);

  INFO("Apply snapshot at 1 to new store");
  {
    kv::Store new_store;
    new_store.clone_schema(store);

    REQUIRE_EQ(
      new_store.deserialise_snapshot(first_snapshot),
      kv::DeserialiseSuccess::PASS);
    REQUIRE_EQ(new_store.current_version(), 1);

    auto new_string_map = new_store.get<MapTypes::StringString>("string_map");
    auto new_num_map = new_store.get<MapTypes::NumNum>("num_map");

    auto tx1 = store.create_tx();
    auto view = tx1.get_view(*new_string_map);
    auto v = view->get("foo");
    REQUIRE(v.has_value());
    REQUIRE_EQ(v.value(), "bar");

    auto view_ = tx1.get_view(*new_num_map);
    auto v_ = view_->get(42);
    REQUIRE(!v_.has_value());

    view = tx1.get_view(*new_string_map);
    v = view->get("key");
    REQUIRE(!v.has_value());
  }

  auto second_snapshot = store.serialise_snapshot(second_snapshot_version);
  INFO("Apply snapshot at 2 to new store");
  {
    kv::Store new_store;
    new_store.clone_schema(store);
    new_store.deserialise_snapshot(second_snapshot);
    REQUIRE_EQ(new_store.current_version(), 2);

    auto new_string_map = new_store.get<MapTypes::StringString>("string_map");
    auto new_num_map = new_store.get<MapTypes::NumNum>("num_map");

    auto tx1 = new_store.create_tx();
    auto view = tx1.get_view(*new_string_map);

    auto v = view->get("foo");
    REQUIRE(v.has_value());
    REQUIRE_EQ(v.value(), "bar");

    auto view_ = tx1.get_view(*new_num_map);
    auto v_ = view_->get(42);
    REQUIRE(v_.has_value());
    REQUIRE_EQ(v_.value(), 123);

    view = tx1.get_view(*new_string_map);
    v = view->get("key");
    REQUIRE(!v.has_value());
  }
}

TEST_CASE(
  "Commit transaction while applying snapshot" *
  doctest::test_suite("snapshot"))
{
  kv::Store store;
  auto& string_map = store.create<MapTypes::StringString>(
    "string_map", kv::SecurityDomain::PUBLIC);

  kv::Version snapshot_version = kv::NoVersion;
  INFO("Apply transactions to original store");
  {
    auto tx1 = store.create_tx();
    auto view_1 = tx1.get_view(string_map);
    view_1->put("foo", "foo");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK); // Committed at 1

    auto tx2 = store.create_tx();
    auto view_2 = tx2.get_view(string_map);
    view_2->put("bar", "bar");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK); // Committed at 2
    snapshot_version = tx2.commit_version();
  }

  auto snapshot = store.serialise_snapshot(snapshot_version);

  INFO("Apply snapshot while committing a transaction");
  {
    kv::Store new_store;
    new_store.clone_schema(store);

    auto new_string_map = new_store.get<MapTypes::StringString>("string_map");
    auto tx = store.create_tx();
    auto view = tx.get_view(*new_string_map);
    view->put("in", "flight");
    // tx is not committed until the snapshot is deserialised

    new_store.deserialise_snapshot(snapshot);

    // Transaction conflicts as snapshot was applied while transaction was in
    // flight
    REQUIRE(tx.commit() == kv::CommitSuccess::CONFLICT);

    view = tx.get_view(*new_string_map);
    view->put("baz", "baz");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }
}