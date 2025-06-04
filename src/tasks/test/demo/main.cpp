// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "./actions.h"
#include "./clients.h"
#include "./node.h"

#include <doctest/doctest.h>

void describe_session_manager(SessionManager& sm)
{
  std::lock_guard<std::mutex> lock(sm.sessions_mutex);
  fmt::print("SessionManager contains {} sessions\n", sm.all_sessions.size());
  for (auto& session : sm.all_sessions)
  {
    fmt::print(
      "  {}: {} to_node, {} from_node\n",
      session->name,
      session->to_node.size(),
      session->from_node.size());
  }
}

// TODO: Add stat tracking/logging in the task system?
// void describe_job_board(JobBoard& jb)
// {
//   std::lock_guard<std::mutex> lock(jb.mutex);
//   fmt::print("JobBoard contains {} tasks\n", jb.queue.size());
//   // for (auto& task : jb.queue)
//   // {
//   //   fmt::print("  {}\n", task->get_name());
//   // }
// }

void describe_dispatcher(Dispatcher& d)
{
  describe_session_manager(d.state.session_manager);
  // describe_job_board((JobBoard&)d.state.job_board);

  fmt::print(
    "Dispatcher is tracking {} sessions\n",
    d.state.ordered_tasks_per_client.size());

  for (auto& [session, tasks] : d.state.ordered_tasks_per_client)
  {
    fmt::print(
      "  {}: {} (active: {}, queue.size: {})\n",
      session->name,
      tasks->get_name(),
      tasks->currently_active(),
      tasks->pending_tasks());
  }
}

// Written a bunch of code here, so run a few simple sanity checks that the
// basic operations do what we expect
TEST_CASE("SignAction" * doctest::test_suite("demo"))
{
  for (size_t i = 0; i < 100; ++i)
  {
    auto orig = std::make_unique<SignAction>();
    auto ser = orig->serialise();

    auto received = deserialise_action(ser);
    auto result = received->do_action();

    orig->verify_serialised_response(result);
  }
}

TEST_CASE("Demo" * doctest::test_suite("demo"))
{
  constexpr auto n_clients = 6;
  constexpr auto n_workers = 2;

  size_t total_requests_sent = 0;
  size_t total_responses_seen = 0;
  size_t total_tasks_processed = 0;

  auto print_header = []() {
    printf("\e[3A"); // Move back to start of header line
    printf("--- Progress ---\n");
    printf("\n"); // Place-holder for workers line
    printf("\n"); // Place-holder for clients line
  };

  // Print placeholders for the 3 lines we're about to overwrite
  printf("\n");
  printf("\n");
  printf("\n");
  print_header();

  {
    // Create a node
    Node node(n_workers);
    node.start();

    static constexpr auto workers_status_string_length =
      n_workers * WorkerState::STATUS_STRING_LENGTH;
    char workers_status_string[workers_status_string_length + 1];
    workers_status_string[workers_status_string_length] = '\0';

    auto print_workers = [&node, &workers_status_string]() {
      for (size_t i = 0; i < n_workers; ++i)
      {
        for (auto j = 0; j < WorkerState::STATUS_STRING_LENGTH; ++j)
        {
          workers_status_string[i * WorkerState::STATUS_STRING_LENGTH + j] =
            node.workers[i]->state.status_string[j];
        }
      }
      printf("\e[2A"); // Move back to start of workers line
      fmt::print("\r  Workers: {}\n", workers_status_string);
      printf("\n"); // Place-holder for clients line
    };

    {
      // Create some clients
      ClientParams client_params;
      std::vector<std::unique_ptr<Client>> clients;
      for (auto i = 0u; i < n_clients; ++i)
      {
        clients.push_back(std::make_unique<Client>(
          node.new_session(std::to_string(i)), client_params, i));
        clients.back()->start();
      }

      static constexpr auto clients_status_string_length =
        n_clients * ClientState::STATUS_STRING_LENGTH;
      char clients_status_string[clients_status_string_length + 1];
      clients_status_string[clients_status_string_length] = '\0';

      auto print_clients = [&clients, &clients_status_string]() {
        for (size_t i = 0; i < n_clients; ++i)
        {
          for (auto j = 0; j < ClientState::STATUS_STRING_LENGTH; ++j)
          {
            clients_status_string[i * ClientState::STATUS_STRING_LENGTH + j] =
              clients[i]->state.status_string[j];
          }
        }
        printf("\e[1A"); // Move back to start of client line
        fmt::print("\r  Clients: {}\n", clients_status_string);
      };

      // Run everything, checking if all clients are done
      while (true)
      {
        print_header();
        print_workers();
        print_clients();
        fflush(stdout);

        size_t running = 0;
        size_t shutting_down = 0;

        for (size_t i = 0; i < n_clients; ++i)
        {
          auto& client = clients[i];

          const auto stage = client->lifetime_stage.load();

          if (stage <= Stage::Running)
          {
            ++running;
          }
          else if (stage == Stage::ShuttingDown)
          {
            ++shutting_down;
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (running + shutting_down == 0)
        {
          break;
        }
      }

      for (auto& client : clients)
      {
        total_requests_sent += client->state.requests_sent;
        total_responses_seen += client->state.responses_seen;
      }

      print_header();
      print_workers();
      print_clients();
      fflush(stdout);

      // LOG_INFO_FMT(
      //   "Shutting down clients, total sent: {}, total seen: {}",
      //   total_requests_sent,
      //   total_responses_seen);
    }

    print_header();
    print_workers();
    fflush(stdout);

    node.dispatcher.state.consider_termination.store(true);
    node.dispatcher.shutdown();

    print_header();
    print_workers();
    fflush(stdout);

    // describe_dispatcher(node.dispatcher);

    for (auto& worker : node.workers)
    {
      worker->state.consider_termination.store(true);
      worker->shutdown();
      total_tasks_processed += worker->state.work_completed;
    }

    print_header();
    print_workers();
    fflush(stdout);

    // TODO
    // Validate results?
    // Validate clean shutdown?
    // Print some metrics?
  }

  LOG_INFO_FMT(
    "{} vs {} vs {}",
    total_requests_sent,
    total_tasks_processed,
    total_responses_seen);

  REQUIRE(total_requests_sent >= total_tasks_processed);
  REQUIRE(total_tasks_processed >= total_responses_seen);
}
