# io_bench TODO

## Tracing improvements (in progress)
- [x] Rename trace_writes.py → trace_io.py
- [x] Add read + fsync tracing to bpftrace script
- [x] Add `op` column to CSV output (W/R/F)
- [x] Add openat tracking for fd→path mapping, --mount-filter option
- [x] Update plot_bench.py for per-op heatmaps
- [x] Update _runner.sh, bench.sh, upload.sh references
- [x] Add open/close latency tracing (O/C ops)
- [x] Add read/write size capture

## Analysis
- [x] Profile real app I/O pattern (ops/sec, size dist, concurrency)
- [ ] Capture MIDs from DebugData during real app load
- [x] Compare CIFS Stats between stress test and real app
- [x] Correlate CCF slow-op logs with bpftrace trace
- [ ] Investigate state_lock contention (measured <3ms — not the main cause)
- [ ] Fix/validate mutex (M) timing — futex ops were wrong (needed op=137 not 0/128), now fixed. But only 2 M events for pid=28951 — suspiciously low
- [ ] Investigate missing threads for pid=28951 — only tid=28951 captured, but CCF nodes should have ≥2 threads (main + UV workers). Possible that UV worker threads get different comm names, or their TIDs weren't matched by bpftrace
- [ ] Investigate main thread starvation during big stalls (1.8s, 4.0s)
  - Main thread drops from 20K events/s to 13/s during stalls
  - Worker threads doing snapshot reads/writes during same window
  - Not blocked on mutex or file I/O — something else
- [ ] Add epoll_wait / network socket tracing to explain unaccounted stall time
- [ ] Check if network-level stalls (consensus, client comms) correlate with stall windows
- [ ] Check CPU utilisation during stalls — are worker threads saturating cores?
- [ ] Investigate whether early stalls are caused by infra/test harness writing large files to same mount (different process, same CIFS connection). Need unfiltered trace to confirm. Later steady-state stalls are a separate issue.

## Trace script improvements
- [x] Add rename/renameat2 tracking (op=N, captures old+new path)
- [x] Add futex-based mutex contention tracking (FUTEX_WAIT >100µs, op=M)
- [ ] Remove per-line epoch offset calculation (currently negligible cost, low priority)
- [x] Add pid and tid columns to CSV output
- [x] Add `--comm` process filter with fork/clone tracking
- [x] Strip unused options (--pid, --pidfile, --duration)

## Replay improvements
- [ ] Causal replay mode (infer serial chains per pid/tid/fd)
- [ ] Hybrid mode: causal within chains, rate-limited to original dispatch rate

## Speculative fixes for ftruncate stalls

- [ ] **Skip ftruncate when not needed**: If `total_len` already equals the file size (common case on primary — no stale data to trim), the ftruncate is a no-op semantically but still does a round-trip. Guard with a size check.
- [ ] **Pre-allocate ledger files**: Use `fallocate()` to pre-extend files, avoiding SET_EOF on every chunk rotation.
- [ ] **Reduce chunk rotation frequency**: Larger chunk sizes mean fewer `complete()` calls, amortizing the ftruncate cost.
- [ ] **Move ftruncate off the main thread**: Execute the truncate+complete sequence on a UV worker thread so the main loop isn't blocked.
- [ ] **Avoid ftruncate entirely**: Track valid data extent in-memory and in the positions table, skip physical truncation.

## Observations log
- Dirty page backpressure is NOT a factor (CIFS cache=strict flushes too aggressively)
- SMB WRITE avg 797ms for real app, 44% "slow" (vs 514ms / 7% for stress test)
- 21 oplock breaks in real app — possible read/write or open/open contention
- Real app has significant read traffic (1.1 MB/s read vs 2.5 MB/s write)
- bpftrace write() syscall only measures page cache copy (~35µs), not Azure Files RTT
- CIFS Stats (cmd 9 = WRITE) is the best measure of actual Azure Files latency
