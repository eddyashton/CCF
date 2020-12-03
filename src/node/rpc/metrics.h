// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/histogram.h"
#include "ds/json.h"
#include "ds/logger.h"

#include <nlohmann/json.hpp>

#define HIST_MAX (1 << 17)
#define HIST_MIN 1
#define HIST_BUCKET_GRANULARITY 5
#define TX_RATE_BUCKETS_LEN 4000

namespace metrics
{
  struct HistogramResults
  {
    int low = {};
    int high = {};
    size_t overflow = {};
    size_t underflow = {};
    nlohmann::json buckets = {};
  };
  DECLARE_JSON_TYPE(HistogramResults)
  DECLARE_JSON_REQUIRED_FIELDS(
    HistogramResults, low, high, overflow, underflow, buckets)

  struct Sample
  {
    size_t start_time_ms;
    size_t end_time_ms;
    size_t tx_count;
  };
  DECLARE_JSON_TYPE(Sample)
  DECLARE_JSON_REQUIRED_FIELDS(Sample, start_time_ms, end_time_ms, tx_count)

  class Metrics
  {
  private:
    static constexpr auto MAX_SAMPLES_COUNT = 4000;

    SpinLock samples_lock;

    Sample samples[MAX_SAMPLES_COUNT] = {};
    size_t next_sample_index = 0;
    bool all_samples_filled = false;
    std::chrono::milliseconds time_elapsed = std::chrono::milliseconds(0);

    using Hist =
      histogram::Histogram<int, HIST_MIN, HIST_MAX, HIST_BUCKET_GRANULARITY>;
    histogram::Global<Hist> global =
      histogram::Global<Hist>("histogram", __FILE__, __LINE__);
    Hist histogram = Hist(global);

    struct TxStatistics
    {
      uint32_t tx_count = 0;
    };
    std::array<TxStatistics, 100> times;

    HistogramResults get_histogram_results()
    {
      HistogramResults result;
      result.low = histogram.get_low();
      result.high = histogram.get_high();
      result.overflow = histogram.get_overflow();
      result.underflow = histogram.get_underflow();
      auto range_counts = histogram.get_range_count();
      nlohmann::json buckets;
      for (auto const& e : range_counts)
      {
        const auto count = e.second;
        if (count > 0)
        {
          buckets.push_back(e);
        }
      }
      result.buckets = buckets;
      return result;
    }

    std::vector<Sample> get_all_samples()
    {
      std::vector<Sample> ret_samples;
      const size_t start_index = all_samples_filled ? next_sample_index : 0;
      const size_t end_index = all_samples_filled ?
        start_index + MAX_SAMPLES_COUNT :
        next_sample_index;
      for (size_t i = start_index; i < end_index; ++i)
      {
        const size_t index = i % MAX_SAMPLES_COUNT;
        ret_samples.push_back(samples[index]);
      }

      LOG_INFO << "Printing time series, this:" << (uint64_t)this << std::endl;
      for (uint32_t i = 0; i < times.size(); ++i)
      {
        LOG_INFO_FMT("{} - {}", i, times[i].tx_count);
      }

      return ret_samples;
    }

  public:
    auto get_metrics()
    {
      std::lock_guard<SpinLock> guard(samples_lock);

      return std::make_pair(get_histogram_results(), get_all_samples());
    }

    void track_tx_rates(
      const std::chrono::milliseconds& elapsed, kv::Consensus::Statistics stats)
    {
      std::lock_guard<SpinLock> guard(samples_lock);

      // calculate how many tx/sec we have processed in this tick
      auto duration = elapsed.count() / 1000.0;
      auto tx_rate = stats.tx_count / duration;
      histogram.record(tx_rate);

      size_t index = next_sample_index++;
      if (next_sample_index >= MAX_SAMPLES_COUNT)
      {
        next_sample_index = 0;
        all_samples_filled = true;
      }

      Sample& sample = samples[index];
      sample.start_time_ms = time_elapsed.count();
      time_elapsed += elapsed;
      sample.end_time_ms = time_elapsed.count();
      sample.tx_count = stats.tx_count;

      uint32_t bucket = time_elapsed.count() / 1000.0;
      if (bucket < times.size())
      {
        times[bucket].tx_count += stats.tx_count;
      }
    }
  };
}