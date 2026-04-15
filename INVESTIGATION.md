# CCF Azure Files I/O Latency Investigation

## Problem Statement

CCF (Confidential Consortium Framework) nodes running on Azure Container Instances with Azure Files (CIFS mount) experience periodic "Operation took too long" warnings during ledger operations. These stalls range from 100ms to 5+ seconds and don't reproduce on local disk or other storage backends.

## Environment

- **Container**: Azure Container Instances (ACI), 16 cores
- **Storage**: Azure Files Premium, mounted via CIFS at `/mnt/azure-fs`
- **Mount options**: `cache=strict, wsize=1048576, persistenthandles, soft, vers=3.0`
- **Application**: CCF node (`basic` binary), single-threaded UV event loop with UV worker threads
- **Kernel**: 6.1.10 (Azure Linux / Mariner), mismatched headers 6.6.130.1-3.azl3
- **bpftrace env**: `BPFTRACE_KERNEL_SOURCE=/usr/src/linux-headers-6.6.130.1-3.azl3`

## Tools Built

All in `/mnt/c/Users/edashton/io_bench/`:

- **`trace_io.py`**: bpftrace-based syscall tracer. Captures W(write), R(read), F(fsync), O(openat), C(close), D(getdents64), N(rename), M(futex/mutex), U(ftruncate), T(newfstatat/catch-all). Supports `--comm` filter (process name), `--mount` filter (path prefix). Outputs CSV with `time_s, latency_us, op, size, pid, tid, file` columns. The ftruncate probe was the key addition that resolved the "dark gap" mystery — earlier versions missed it, making those syscalls invisible.
- **`plot_bench.py`**: Renders per-op latency heatmaps from trace CSVs. Spike detection on writes with zoom panels.
- **`replay_io.py`**: Wallclock replay of trace CSVs against a target directory. For comparing storage backends.
- **`upload.sh`**: Deploys scripts to Azure file share.

## Key Findings

### 1. write() syscall measures page cache, not Azure Files (CONFIRMED)

The `write()` syscall returns in ~35µs — it's just a page cache copy. Actual Azure Files latency is hidden behind async CIFS writeback. CIFS Stats show SMB WRITE operations average 500-800ms, with tails to 3-4 seconds. The page cache absorbs everything.

### 2. Dirty page backpressure is NOT a factor (CONFIRMED)

With `cache=strict`, CIFS flushes so aggressively that dirty pages never exceed ~30MB, vs the kernel's 2.9GB hard limit. Even 64 threads writing 1MB blocks with fsync couldn't push past ~37MB dirty. The `balance_dirty_pages` throttling mechanism is irrelevant for this mount.

### 3. SMB credits are NOT a bottleneck (CONFIRMED)

Server grants 4,096 credits. Max observed in-flight requests: 54. Plenty of headroom.

### 4. Sub-second stalls are Azure Files metadata round-trips (CONFIRMED)

Every sub-second stall correlates cleanly with traced syscalls:

| Operation | Typical latency | SMB operation |
|---|---|---|
| `openat()` (create new file) | 130-300ms, max 1s | SMB CREATE |
| `rename()` (commit file) | 220-425ms, max 1.5s | SMB SET_INFO |
| `fsync()` | 100-165ms | SMB FLUSH |
| `close()` (with dirty data) | 60-70ms | SMB CLOSE |

These happen during ledger chunk rotation (complete old chunk → commit → open new chunk) and account for the 100-500ms stalls.

### 5. Multi-second stalls are ftruncate() on CIFS (ROOT CAUSE FOUND)

The worst stalls (1.4-5.2s), previously called "dark gaps", are **`ftruncate()` syscalls** issued by `LedgerFile::complete()` → `LedgerFile::truncate()`. Earlier trace_io.py versions lacked an ftruncate probe, so these syscalls were invisible — they appeared as periods of zero syscall activity because the catch-all probe was unreliably missing certain syscalls.

Once ftruncate probes were added, every former "dark gap" was accounted for. The latency scales with file size:

| File size | ftruncate latency |
|---|---|
| ~90KB | 140-180ms |
| ~185KB | 145-285ms |
| ~660KB | ~600ms |
| ~3.8MB | 1.0-2.2s |
| ~6.0MB | 2.5-4.5s |

The `complete()` method unconditionally calls `truncate(get_last_idx())` to ensure no stale data past the last entry, which issues `ftruncate(fd, total_len)`. On CIFS/Azure Files, this becomes a synchronous SMB SET_EOF operation whose latency scales with file size. On local disk it's instant.

Example trace (from `./azure-files/ccf_trace.csv`): the worst ftruncate was 4.47s on a 5.96MB ledger file, and 4.33s on a snapshot file of the same size. All three nodes show the same pattern.

### 6. Worker thread snapshot commits correlate with some stalls (OBSERVED, MECHANISM UNCLEAR)

In one instance, a UV worker thread's `on_snapshot_sync_and_rename` (in `snapshot_manager.h`) took 1,733ms for fsync+rename of a snapshot file. The main thread's dark gap overlapped exactly. However, **this code path does NOT take `state_lock`** — it's independent of the ledger. The correlation might be coincidental, or there could be kernel-level CIFS connection contention.

### 7. state_lock contention is negligible in traced runs (CONFIRMED)

After fixing futex tracing (correct ops: 0/9/128/137, filter ret==0 only), mutex contention events show max ~12ms. The lock acquisition in `write_entry()` was confirmed instant via instrumentation (no "Waiting for state_lock" log hits). The original theory that `read_entries_range` holding `state_lock` during slow I/O blocks writes was not validated in these runs — though the fix to narrow lock scope in `read_entries_range` remains a good improvement.

### 8. complete() profiling confirms ftruncate dominates (CONFIRMED)

The `LedgerFile::complete()` method now has `LOG_FAIL_FMT`-based profiling (threshold >50ms) that breaks down time into: truncate, write_positions, write_header, flush. The truncate step (which contains the ftruncate syscall) dominates in every stall. The write_positions, write_header, and flush steps are negligible by comparison.

## What We Ruled Out

- Dirty page backpressure
- SMB credit exhaustion
- bpftrace event drops during gaps
- Kernel scheduling / CPU starvation (16 cores, 3 nodes + clients)
- Missing syscall types (resolved: the raw_syscalls catch-all was unreliable; dedicated ftruncate probes achieved full coverage)
- futex/mutex contention (confirmed negligible after fixing tracing)
- io_uring bypassing syscall tracing (libuv has io_uring but CCF uses direct stdio for ledger I/O)
- Client process I/O interference (clients read from libsymcrypt, not Azure Files)
- "Userspace dark gaps" (resolved: these were untraced ftruncate syscalls, not userspace computation)

## Known Issues with trace_io.py

- **bpftrace pid/tid are swapped** on this kernel version. `convert_raw` swaps them back, but older traces have them wrong. The swap is: bpftrace `pid` = task/tid, bpftrace `tid` = tgid/pid.
- **Futex tracing had multiple bugs**: wrong op values (needed 137 not 0/128), no ret filter (captured UV event loop timeouts as contention), stale map entries from interrupted syscalls producing bogus multi-day latencies. All now fixed.
- **fd→path mapping misses pre-existing fds** opened before tracer attaches.
- **CRITICAL: fd reuse causes massive data contamination (FIXED)**: When a file fd was closed and the fd number reused by a socket (e.g. TLS connection), the stale fd→path mapping persisted. All subsequent socket reads/writes were mislabelled as file I/O on the original filename. This affected **>99% of R/W events** in both trace datasets — nearly all "file reads and writes" were actually TLS network I/O. The `1.pem` file (10,974 events) was entirely socket traffic. `ledger_23` (23,237 events) had only ~6 real file ops. Fix: `convert_raw` now removes fd→path mappings on close events. **All analysis of read/write latency distributions from traces collected before this fix is invalid.**
- **Relative paths** (like `0.ledger/ledger_1`) work for matching but can't be used with `--mount` filter.
- **Catch-all syscall probe was unreliable**: The attempt to catch unprobed syscalls with a generic probe missed ftruncate and possibly others. Dedicated per-syscall probes are needed for reliable coverage.

## CCF Code References

- `src/host/ledger.h`: `Ledger::write_entry()` (line ~1298), `Ledger::commit()` (line ~1434), `LedgerFile::complete()` (line ~528), `read_entries_range()` (line ~839), `on_ledger_get_async()` (line ~1542)
- `src/snapshots/snapshot_manager.h`: `on_snapshot_sync_and_rename()` — UV worker, does fsync+close+rename without state_lock
- `src/host/handle_ring_buffer.h`: `on_timer()` reads up to 256 messages per tick
- `src/host/time_bound_logger.h`: RAII timer that logs if scope exceeds threshold

## Root Cause Summary

All observed multi-second stalls are caused by **`ftruncate()` on CIFS-mounted Azure Files**. The call path is:

1. `Ledger::write_entry()` fills a ledger chunk to its size limit
2. `LedgerFile::complete()` is called to finalize the chunk
3. `complete()` unconditionally calls `truncate(get_last_idx())` to trim any stale data
4. `truncate()` calls `ftruncate(fileno(file), total_len)`
5. On CIFS, this becomes a synchronous SMB SET_EOF round-trip to Azure Files
6. Latency scales with file size: ~140ms for 90KB, up to 4.5s for 6MB files

The same pattern affects snapshot files via `on_snapshot_sync_and_rename()`, where ftruncate is also called.

Sub-second stalls (100-500ms) from openat/rename/fsync/close (Finding #4) are separate and expected metadata costs.

## RETRACTED: actimeo=30 slow reads/writes

Earlier analysis claimed that `actimeo=30` caused 4.3% of reads and writes to become slow (60-100ms) due to CIFS page cache revalidation. This finding is **retracted**. The "slow reads/writes" were TLS socket I/O mislabelled as file I/O due to the fd reuse bug in trace_io.py (see Known Issues).

Validation: >99% of R/W events attributed to ledger files occurred after the file's fd was closed and reused by a network socket. The W(4)+W(129)+R(16384) pattern is TLS record framing, not file I/O. The `compare_traces.py` comparison showing 574 slow writes on actimeo=30 vs 5 on the original mount was comparing network traffic under different consensus conditions, not file I/O under different mount options.

The metadata stalls (ftruncate, rename, fsync, openat) are unaffected by this bug — they use dedicated probes with path resolution at syscall entry, not fd mapping. Those findings remain valid.

## Reproducer Tool: ccf_like

`ccf_like.c` is a standalone C program that simulates CCF's ledger I/O pattern:
- Sequential writes (~283B entries) to a file until it reaches chunk size
- Chunk rotation: ftruncate → write positions table → fsync → close → rename
- Repeat for N chunks

Successfully reproduces metadata stalls (ftruncate 1-2.5s, openat ~175ms, fsync ~100ms, rename ~150ms) on both CIFS mounts. Does NOT reproduce slow reads/writes because those were a tracing artifact, not a real phenomenon.

Build: `cc -O2 -o ccf_like ccf_like.c`
Usage: `./ccf_like --dir /mnt/azure-fs/bench --chunks 5 --threshold-ms 50`

## Current Status

- **Root cause identified**: ftruncate on CIFS scales with file size, blocking the main UV loop
- **Instrumentation in place**: `complete()` has LOG_FAIL_FMT profiling, trace_io.py has full syscall coverage including ftruncate
- **trace_io.py fd reuse bug fixed**: Close events now clear fd→path mappings. Existing trace CSVs are contaminated and need re-collection.
- **Development environment**: VS Code tunnel to the ACI container for fast iteration
- **Traces need re-collection**: All existing trace CSVs (`./azure-files/ccf_trace.csv`, `./azure-files-mountopts/mount_opts_trace.csv`) have contaminated R/W data. Metadata ops (O/C/U/F/N/T/D) are unaffected. New traces with the fixed trace_io.py are needed to get accurate file I/O distributions.
- **Multiple runs needed**: Single runs can't distinguish mount-option effects from consensus-state effects (leadership changes, election storms). Future experiments need multiple runs per configuration to establish baselines.
