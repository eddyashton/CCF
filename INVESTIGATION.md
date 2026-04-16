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

## Tools

- **`trace_io.py`**: bpftrace-based syscall tracer with dedicated probes for each syscall type (no generic catch-all — that proved unreliable for long-running syscalls). Outputs CSV for analysis. See script for supported ops and options.
- **`ccf_like.py`**: Standalone reproducer that simulates CCF's ledger chunk write/complete/commit I/O pattern without needing a full CCF build. Reproduces metadata stalls on CIFS mounts. See script for details.
- **`run_experiment.sh`**: Experiment harness that orchestrates multiple CCF perf test runs and ccf_like runs across mount configurations, with tracing, CIFS stats capture, and auto-generated plots. Results go to `experiments/<timestamp>/`.
- **`plot_trace_events.py`**: Time-vs-latency scatter plots from `trace_io.py` CSV output. Called automatically by `run_experiment.sh`.

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

- **bpftrace pid/tid are swapped** on this kernel version. `convert_raw` swaps them back.
- **fd→path mapping misses pre-existing fds** opened before tracer attaches.
- **fd reuse contamination (FIXED)**: Stale fd→path mappings caused socket I/O to be mislabelled as file I/O. Fix: close events now clear mappings. Traces collected before this fix have invalid R/W data; metadata ops (O/C/U/F/N/T/D) are unaffected.
- **Catch-all syscall probe was unreliable**: Removed. All coverage is now via dedicated per-syscall probes.

## CCF Code References

- `src/host/ledger.h`: `Ledger::write_entry()`, `Ledger::commit()`, `LedgerFile::complete()`, `LedgerFile::truncate()`, `read_entries_range()`, `on_ledger_get_async()`
- `src/snapshots/snapshot_manager.h`: `on_snapshot_sync_and_rename()` — UV worker, does fsync+close+rename without state_lock
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

Earlier analysis claimed `actimeo=30` caused slow reads/writes due to CIFS page cache revalidation. **Retracted** — those were TLS socket I/O mislabelled as file I/O due to the fd reuse bug (see Known Issues). Metadata stall findings are unaffected.

## Reproducer

`ccf_like.py` reproduces the metadata stalls in isolation on any CIFS mount, without needing a full CCF build. Run `python3 ccf_like.py --help` for options.

## Current Status

- **Root cause identified**: ftruncate on CIFS scales with file size, blocking the main UV loop
- **Instrumentation in place**: `complete()` has LOG_FAIL_FMT profiling, `trace_io.py` has full syscall coverage
- **trace_io.py fd reuse bug fixed**: Close events now clear fd→path mappings. Traces collected before this fix have contaminated R/W data (metadata ops are unaffected).
- **Experiment harness ready**: `run_experiment.sh` automates multi-run, multi-mount experiments
- **Multiple runs needed**: Single runs can't distinguish mount-option effects from consensus-state effects. Use `run_experiment.sh --runs N` for baselines.
