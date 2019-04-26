// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "../messaging.h"

#include "../ringbuffer.h"
#include "../serialized.h"

#include <array>
#include <doctest/doctest.h>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace messaging;
using namespace ringbuffer;

template <typename Ex, typename F>
void require_throws_with(
  F&& f,
  const std::vector<std::string>& includes,
  const std::vector<std::string>& excludes = {})
{
  bool threw = false;
  try
  {
    f();
  }
  catch (const Ex& ex)
  {
    threw = true;
    const std::string what = ex.what();

    for (const auto& s : includes)
    {
      REQUIRE(what.find(s) != std::string::npos);
    }

    for (const auto& s : excludes)
    {
      REQUIRE(what.find(s) == std::string::npos);
    }
  }
  REQUIRE(true);
}

TEST_CASE("Dispatch" * doctest::test_suite("messaging"))
{
  using MType = size_t;
  constexpr MType m0 = 0;
  constexpr MType m1 = 1;
  constexpr MType m2 = 2;

  size_t x = 0;
  constexpr size_t a = 0xcafe;
  constexpr size_t b = 0xbeef;

  auto set_a = [&x](const uint8_t*, size_t) { x = a; };

  auto set_b = [&x](const uint8_t*, size_t) { x = b; };

  auto set_arg = [&x](const uint8_t* data, size_t size) {
    x = serialized::read<size_t>(data, size);
  };

  auto unregister = [](const uint8_t*, size_t) { return false; };

  Dispatcher<MType> d("Test");

  INFO("Handlers can be registered");
  {
    REQUIRE_NOTHROW(DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_a));
    REQUIRE_NOTHROW(DISPATCHER_SET_MESSAGE_HANDLER(d, m1, set_b));
  }

  INFO("Only unregistered IDs can be registered");
  {
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_a), already_handled);
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_b), already_handled);
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_arg), already_handled);
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m1, set_a), already_handled);
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m1, set_b), already_handled);
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m1, set_arg), already_handled);
  }

  INFO("Error messages are informative");
  {
    require_throws_with<already_handled>(
      [&] { DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_a); }, {"m0"}, {"m1"});
    require_throws_with<already_handled>(
      [&] { DISPATCHER_SET_MESSAGE_HANDLER(d, m1, set_a); }, {"m1"}, {"m0"});

    constexpr MType duplicate = m0;
    require_throws_with<already_handled>(
      [&] { DISPATCHER_SET_MESSAGE_HANDLER(d, duplicate, set_a); },
      {"m0", "duplicate"},
      {"m1"});
  }

  INFO("Handlers can be called");
  {
    REQUIRE_NOTHROW(d.dispatch(m0, nullptr, 0));
    REQUIRE(x == a);

    REQUIRE_NOTHROW(d.dispatch(m1, nullptr, 0));
    REQUIRE(x == b);
  }

  INFO("Dispatching an unhandled message will throw");
  {
    REQUIRE_THROWS_AS(d.dispatch(m2, nullptr, 0), no_handler);
    REQUIRE(x == b);
  }

  INFO("Handlers can be unregistered");
  {
    REQUIRE_NOTHROW(d.remove_message_handler(m0));
    REQUIRE_NOTHROW(d.remove_message_handler(m1));
  }

  INFO("Only registered IDs can be unregistered");
  {
    REQUIRE_THROWS_AS(d.remove_message_handler(m0), no_handler);
    REQUIRE_THROWS_AS(d.remove_message_handler(m1), no_handler);
  }

  INFO("IDs can be re-registered and called");
  {
    REQUIRE_NOTHROW(DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_arg));
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(d, m0, set_arg), already_handled);

    const size_t arg = 0xdead;
    REQUIRE_NOTHROW(d.dispatch(m0, (const uint8_t*)&arg, sizeof(arg)));
    REQUIRE(x == arg);

    REQUIRE_NOTHROW(d.remove_message_handler(m0));
    REQUIRE_THROWS_AS(d.remove_message_handler(m0), no_handler);
  }
}

TEST_CASE("Basic message loop" * doctest::test_suite("messaging"))
{
  enum : Message
  {
    set_x = Const::msg_min,
    echo,
    echo_out,
    finish
  };

  BufferProcessor bp;

  Reader loop_src(1 << 10);
  Writer test_filler(loop_src);

  Reader out_reader(1 << 10);
  Writer out_writer(out_reader);

  size_t x = 0;

  auto set_x_handler = [&](const uint8_t* data, size_t size) {
    x = serialized::read<uint8_t>(data, size);
  };

  auto echo_handler = [&](const uint8_t* data, size_t size) {
    out_writer.write(echo_out, serializer::ByteRange{data, size});
  };

  auto finish_handler = [&](const uint8_t* data, size_t size) {
    bp.set_finished();
  };

  DISPATCHER_SET_MESSAGE_HANDLER(bp, set_x, set_x_handler);
  DISPATCHER_SET_MESSAGE_HANDLER(bp, echo, echo_handler);
  DISPATCHER_SET_MESSAGE_HANDLER(bp, finish, finish_handler);

  SUBCASE("Duplicate calls to set handler will throw")
  {
    REQUIRE_THROWS_AS(
      DISPATCHER_SET_MESSAGE_HANDLER(bp, finish, finish_handler),
      messaging::already_handled);
  }

  SUBCASE("Unhandled messages will throw")
  {
    test_filler.write(echo_out);
    REQUIRE_THROWS_AS(bp.run(loop_src), messaging::no_handler);
  }

  SUBCASE("Message handlers can finish the loop")
  {
    test_filler.write(finish);
    REQUIRE(bp.run(loop_src) == 1);
  }

  SUBCASE("Message handlers can affect external state")
  {
    const uint8_t new_x = 42;
    test_filler.write(set_x, new_x);
    test_filler.write(finish);
    REQUIRE(bp.run(loop_src) == 2);
    REQUIRE(x == new_x);
  }

  SUBCASE("Message handlers can communicate through the writer")
  {
    const std::vector<uint8_t> actual = {0, 13, 42, 100, 100, 10, 1};
    test_filler.write(echo, actual);
    test_filler.write(finish);
    REQUIRE(bp.run(loop_src) == 2);

    REQUIRE(
      out_reader.read(
        1, [&actual](Message m, const uint8_t* data, size_t size) {
          REQUIRE(m == echo_out);
          REQUIRE(size == actual.size());
          for (size_t i = 0; i < size; ++i)
          {
            REQUIRE(data[i] == actual[i]);
          }
        }) == 1);
  }

  SUBCASE("Dispatcher can be accessed directly")
  {
    auto& dispatcher = bp.get_dispatcher();
    dispatcher.remove_message_handler(set_x);

    const auto old_x = x;
    const auto new_x = old_x + 1;
    DISPATCHER_SET_MESSAGE_HANDLER(
      dispatcher, set_x, [&](const uint8_t* data, size_t size) { x = new_x; });

    REQUIRE(x == old_x);
    dispatcher.dispatch(set_x, nullptr, 0);
    REQUIRE(x == new_x);

    REQUIRE_NOTHROW(dispatcher.remove_message_handler(set_x));
    REQUIRE_THROWS_AS(
      dispatcher.remove_message_handler(set_x), messaging::no_handler);
    REQUIRE_THROWS_AS(
      dispatcher.dispatch(set_x, nullptr, 0), messaging::no_handler);
  }
}

TEST_CASE("Multiple threads" * doctest::test_suite("messaging"))
{
  enum : Message
  {
    empty = Const::msg_min,
    ping,
    pong,
    finish
  };

  struct ThreadArgs
  {
    Reader& r;
    Writer w;
    size_t& read_count;
  };

  auto run_threads =
    [](Circuit& circuit, size_t& reads_inside, size_t& reads_outside) {
      std::atomic<bool> first_finished(false);

      auto thread_fn = [&](ThreadArgs* ta) {
        BufferProcessor bp;

        auto finish_handler = [&bp, &ta](const uint8_t* data, size_t size) {
          // If they tell us to stop, we tell them to stop as well
          ta->w.write(finish);
          bp.set_finished();
        };
        DISPATCHER_SET_MESSAGE_HANDLER(bp, finish, finish_handler);

        auto empty_handler = [](const uint8_t* data, size_t size) {};
        DISPATCHER_SET_MESSAGE_HANDLER(bp, empty, empty_handler);

        // Read [count, tail...], write pong(count, tail...)
        auto ping_handler = [&ta](const uint8_t* data, size_t size) {
          const auto count = serialized::read<uint8_t>(data, size);
          ta->w.write(pong, count, serializer::ByteRange{data, size});
        };
        DISPATCHER_SET_MESSAGE_HANDLER(bp, ping, ping_handler);

        // Read [count, tail...], if positive write ping(count - 1)
        // else if tail, write ping(tail...)
        // else if we're the second finisher, write finish
        auto pong_handler = [&ta, &first_finished](
                              const uint8_t* data, size_t size) {
          const auto count = serialized::read<uint8_t>(data, size);
          if (count > 0)
          {
            ta->w.write(
              ping, (uint8_t)(count - 1), serializer::ByteRange{data, size});
          }
          else if (size > 0)
          {
            const auto next = serialized::read<uint8_t>(data, size);
            ta->w.write(ping, (uint8_t)next, serializer::ByteRange{data, size});
          }
          else
          {
            bool was = false;
            const bool done = !first_finished.compare_exchange_weak(was, true);
            if (done)
            {
              ta->w.write(finish);
            }
          }
        };
        DISPATCHER_SET_MESSAGE_HANDLER(bp, pong, pong_handler);

        ta->read_count += bp.run(ta->r);
      };

      ThreadArgs insideArgs = {
        circuit.read_from_outside(), circuit.write_to_outside(), reads_inside};
      std::thread inside_thread(thread_fn, &insideArgs);

      ThreadArgs outsideArgs = {
        circuit.read_from_inside(), circuit.write_to_inside(), reads_outside};
      std::thread outside_thread(thread_fn, &outsideArgs);

      inside_thread.join();
      outside_thread.join();
    };

  SUBCASE("Message loops run until stopped")
  {
    size_t reads_inside = 0;
    size_t reads_outside = 0;
    Circuit circuit(1 << 10);

    // Prepare a single message from outside to inside, telling it to finish
    circuit.write_to_inside().write(finish);
    run_threads(circuit, reads_inside, reads_outside);

    REQUIRE(reads_inside == 1);
    REQUIRE(reads_outside == 1);
  }

  SUBCASE("Both loops can send data")
  {
    size_t reads_inside = 0;
    size_t reads_outside = 0;
    Circuit circuit(1 << 10);

    // Both sides send some empty messages
    constexpr auto count_inbound = 2u;
    constexpr auto count_outbound = 5u;

    for (auto i = 0u; i < count_inbound; ++i)
    {
      circuit.write_to_inside().write(empty);
    }

    for (auto i = 0u; i < count_outbound; ++i)
    {
      circuit.write_to_outside().write(empty);
    }

    circuit.write_to_inside().write(finish);

    run_threads(circuit, reads_inside, reads_outside);

    REQUIRE(reads_inside == count_inbound + 1);
    REQUIRE(reads_outside == count_outbound + 1);
  }

  SUBCASE("Messages can cause repeated nested responses")
  {
    struct PingCounts
    {
      const std::vector<uint8_t> in;
      const std::vector<uint8_t> out;
    };

    for (const auto& pc : {PingCounts{{5}, {5}},
                           {{3, 4, 5}, {2}},
                           {{2}, {3, 4, 5}},
                           {{3, 4, 2, 8}, {100, 10}},
                           {{100, 10}, {3, 4, 2, 8}}})
    {
      const auto& pings_in = pc.in;
      const auto& pings_out = pc.out;

      size_t reads_inside = 0;
      size_t reads_outside = 0;
      Circuit circuit(1 << 10);

      circuit.write_to_inside().write(ping, pings_in);
      circuit.write_to_outside().write(ping, pings_out);

      run_threads(circuit, reads_inside, reads_outside);

      // There's a pong for every ping, so both sides read the same number of
      // messages
      const size_t total_piongs = pings_in.size() +
        std::accumulate(pings_in.begin(), pings_in.end(), 0u) +
        pings_out.size() +
        std::accumulate(pings_out.begin(), pings_out.end(), 0u);
      CHECK(reads_inside == total_piongs + 1);
      CHECK(reads_outside == total_piongs + 1);
    }
  }
}
