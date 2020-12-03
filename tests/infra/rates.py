# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import json
from statistics import mean, harmonic_mean, median, pstdev

from loguru import logger as LOG


def samples_to_tx_per_s(samples):
    rates = []
    count_in_bucket = 0
    ms_used_in_bucket = 0
    for sample in samples:
        sample_duration = sample["end_time_ms"] - sample["start_time_ms"]
        count_in_bucket += sample["tx_count"]

        ms_used_in_bucket += sample_duration
        # NB: Assuming durations cut-off near each second, doing no splitting of samples which cross second boundaries
        if ms_used_in_bucket >= 1000:
            rates.append(count_in_bucket)
            count_in_bucket = 0
            ms_used_in_bucket = 0

    if ms_used_in_bucket > 0:
        final_duration = ms_used_in_bucket / 1000
        final_rate = count_in_bucket / final_duration
        rates.append(final_rate)

    return rates


class TxRates:
    def __init__(self, primary):
        self.get_histogram = False
        self.primary = primary
        self.same_commit_count = 0
        self.histogram_data = {}
        self.tx_rates_data = []
        self.all_metrics = {}
        self.commit = 0

    def __str__(self):
        out_list = []

        def format_title(s):
            out_list.append(f"{s:-^42}")

        def format_line(s, n):
            out_list.append(f"--- {s:>20}: {n:>12.2f} ---")

        format_title("Summary")
        format_line("mean", mean(self.tx_rates_data))
        format_line("harmonic mean", harmonic_mean(self.tx_rates_data))
        format_line("standard deviation", pstdev(self.tx_rates_data))
        format_line("median", median(self.tx_rates_data))
        format_line("max", max(self.tx_rates_data))
        format_line("min", min(self.tx_rates_data))

        format_title("Histogram")
        buckets = self.histogram_data["buckets"]
        bucket_counts = [bucket["count"] for bucket in buckets]
        out_list.append(
            f"({sum(bucket_counts)} samples in {len(bucket_counts)} buckets)"
        )
        max_count = max(bucket_counts)
        for bucket in buckets:
            count = bucket["count"]
            out_list.append(
                "{:>12}: {}".format(
                    f"{bucket['lower_bound']}-{bucket['upper_bound']}",
                    "*" * min(count, (60 * count // max_count)),
                )
            )

        return "\n".join(out_list)

    def save_results(self, output_file):
        LOG.info(f"Saving metrics to {output_file}")
        with open(output_file, "w") as mfile:
            json.dump(self.all_metrics, mfile, indent=2)

    def process_next(self):
        with self.primary.client() as client:
            rv = client.get("/node/commit")
            next_commit = rv.body.json()["seqno"]
            more_to_process = self.commit != next_commit
            self.commit = next_commit

            return more_to_process

    def get_metrics(self):
        with self.primary.client("user0") as client:
            rv = client.get("/app/metrics")
            self.all_metrics = rv.body.json()

            samples = self.all_metrics.get("samples")
            if samples is None:
                LOG.error("No tx count samples found")
            else:
                rates = samples_to_tx_per_s(samples)
                # For parity with old numbers, ignore any buckets of 0
                self.tx_rates_data = [rate for rate in rates if rate != 0]

            histogram = self.all_metrics.get("rates_histogram")
            if histogram is None:
                LOG.error("No rates histogram found")
            else:
                self.histogram_data = histogram

    def insert_metrics(self, **kwargs):
        self.all_metrics.update(**kwargs)
