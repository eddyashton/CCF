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
      tasks->actions.active,
      tasks->actions.pending.size());
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
  size_t total_requests_sent = 0;
  size_t total_responses_seen = 0;
  size_t total_tasks_processed = 0;

  {
    // Create a node
    Node node(4);
    node.start();

    {
      // Create some clients
      ClientParams client_params;
      std::vector<std::unique_ptr<Client>> clients;
      for (auto i = 0u; i < 12; ++i)
      {
        clients.push_back(std::make_unique<Client>(
          node.new_session(std::to_string(i)), client_params, i));
        clients.back()->start();
      }

      LOG_INFO_FMT("Leaving to run");

      // Run everything, checking if all clients are done
      const auto n_clients = clients.size();
      while (true)
      {
        size_t running = 0;
        size_t shutting_down = 0;
        for (auto& client : clients)
        {
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
        LOG_INFO_FMT(
          "{} clients running (submitting), {} shutting down (checking "
          "responses), ({} total) ...",
          running,
          shutting_down,
          n_clients);
        if (running + shutting_down == 0)
        {
          break;
        }
        else
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
      }

      for (auto& client : clients)
      {
        total_requests_sent += client->state.requests_sent;
        total_responses_seen += client->state.responses_seen;
      }

      LOG_INFO_FMT(
        "Shutting down clients, total sent: {}, total seen: {}",
        total_requests_sent,
        total_responses_seen);
    }

    node.dispatcher.state.consider_termination.store(true);
    node.dispatcher.shutdown();

    describe_dispatcher(node.dispatcher);

    for (auto& worker : node.workers)
    {
      worker->state.consider_termination.store(true);
      worker->shutdown();
      total_tasks_processed += worker->state.work_completed;
    }

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
