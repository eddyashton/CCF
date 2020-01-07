// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#define PICOBENCH_IMPLEMENT_WITH_MAIN
#include "../logger.h"

#include <picobench/picobench.hpp>

enum LoggerKind
{
  None = 0x0,

  Console = 0x1,
  JSON = 0x2,

  All = 0xffff,
};

template <LoggerKind LK, bool Absorb = true>
static void prepare_loggers()
{
  logger::config::loggers().clear();

  if constexpr ((LK & LoggerKind::Console) != 0)
  {
    logger::config::loggers().emplace_back(
      std::make_unique<logger::ConsoleLogger>());
  }

  if constexpr ((LK & LoggerKind::JSON) != 0)
  {
    logger::config::loggers().emplace_back(
      std::make_unique<logger::JsonLogger>("./custom_json_logger"));
  }

  if constexpr (Absorb)
  {
    // Swallow all output for duration of benchmarks
    for (auto& logger : logger::config::loggers())
    {
      logger->get_stream().setstate(std::ios_base::badbit);
    }
  }
}

static void reset_loggers()
{
  logger::config::loggers().clear();

  logger::config::loggers().emplace_back(
    std::make_unique<logger::ConsoleLogger>());

  std::cout.clear();
}

template <LoggerKind LK, bool Absorb = true>
static void log_accepted(picobench::state& s)
{
  prepare_loggers<LK, Absorb>();

  logger::config::level() = logger::DBG;
  {
    picobench::scope scope(s);

    for (size_t i = 0; i < s.iterations(); ++i)
    {
      LOG_DEBUG << "test" << std::endl;
    }
  }

  reset_loggers();
}

template <LoggerKind LK, bool Absorb = true>
static void log_accepted_fmt(picobench::state& s)
{
  prepare_loggers<LK, Absorb>();

  logger::config::level() = logger::DBG;
  {
    picobench::scope scope(s);

    for (size_t i = 0; i < s.iterations(); ++i)
    {
      LOG_DEBUG_FMT("test");
    }
  }

  reset_loggers();
}

template <LoggerKind LK, bool Absorb = true>
static void log_rejected(picobench::state& s)
{
  prepare_loggers<LK, Absorb>();

  logger::config::level() = logger::FAIL;
  {
    picobench::scope scope(s);

    for (size_t i = 0; i < s.iterations(); ++i)
    {
      LOG_DEBUG << "test" << std::endl;
    }
  }

  reset_loggers();
}

template <LoggerKind LK, bool Absorb = true>
static void log_rejected_fmt(picobench::state& s)
{
  prepare_loggers<LK, Absorb>();

  logger::config::level() = logger::FAIL;
  {
    picobench::scope scope(s);

    for (size_t i = 0; i < s.iterations(); ++i)
    {
      LOG_DEBUG_FMT("test");
    }
  }

  reset_loggers();
}

const std::vector<int> sizes = {1000};

PICOBENCH_SUITE("logger");
auto console_accept = log_accepted<LoggerKind::Console>;
PICOBENCH(console_accept).iterations(sizes).samples(10);
auto console_accept_fmt = log_accepted_fmt<LoggerKind::Console>;
PICOBENCH(console_accept_fmt).iterations(sizes).samples(10);
auto console_reject = log_rejected<LoggerKind::Console>;
PICOBENCH(console_reject).iterations(sizes).samples(10);
auto console_reject_fmt = log_rejected_fmt<LoggerKind::Console>;
PICOBENCH(console_reject_fmt).iterations(sizes).samples(10);

auto json_accept = log_accepted<LoggerKind::JSON>;
PICOBENCH(json_accept).iterations(sizes).samples(10);
auto json_accept_fmt = log_accepted_fmt<LoggerKind::JSON>;
PICOBENCH(json_accept_fmt).iterations(sizes).samples(10);
auto json_reject = log_rejected<LoggerKind::JSON>;
PICOBENCH(json_reject).iterations(sizes).samples(10);
auto json_reject_fmt = log_rejected_fmt<LoggerKind::JSON>;
PICOBENCH(json_reject_fmt).iterations(sizes).samples(10);

auto log_all = log_accepted<LoggerKind::All, false>;
PICOBENCH(log_all).iterations(sizes).samples(10);
