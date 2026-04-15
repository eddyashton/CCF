#!/usr/bin/env python3
"""
ccf_like.py — Simulate CCF ledger I/O pattern on a target directory.

Mimics the write -> complete -> commit -> rotate cycle from CCF's Ledger
class (src/host/ledger.h), producing the same syscall pattern on CIFS.

Logs any operation exceeding --threshold-ms.

Usage: python3 ccf_like.py --dir /mnt/azure-fs/bench --chunks 20
"""

import argparse
import os
import struct
import time


# ── Timing helper ─────────────────────────────────────────────────────────

def timed(label, fname, file_pos, chunk_idx, fn, *, threshold_ms):
    """Run fn(), log if it exceeds threshold_ms."""
    t0 = time.monotonic()
    result = fn()
    dt_ms = (time.monotonic() - t0) * 1000
    if dt_ms >= threshold_ms:
        print(
            f"[{time.time():.3f}] SLOW {dt_ms:7.1f}ms  {label:<12s}"
            f"  chunk={chunk_idx}  size={file_pos}  file={fname}",
            flush=True,
        )
    return result


# ── Ledger file operations ────────────────────────────────────────────────

def ledger_file_create(fname, chunk_idx, *, threshold_ms):
    """
    Create a new ledger chunk file.
    Mirrors: LedgerFile::LedgerFile(dir, start_idx) — the "new file" ctor
    which calls fopen(path, "w+b") and writes the 8-byte header placeholder.
    See ledger.h LedgerFile constructor (new file path).
    """
    f = timed("openat", fname, 0, chunk_idx,
              lambda: open(fname, "w+b", buffering=8192),
              threshold_ms=threshold_ms)

    # Header reserved for the offset to the positions table
    header = struct.pack("<Q", 0)
    timed("write", fname, 0, chunk_idx,
          lambda: f.write(header),
          threshold_ms=threshold_ms)

    return f, len(header)


def ledger_write_entry(f, fname, file_pos, data, flush, chunk_idx,
                       *, threshold_ms):
    """
    Write a single ledger entry and optionally flush.
    Mirrors: LedgerFile::write_entry() with committable=true path.
    fseek + fwrite + conditional fflush.
    See ledger.h LedgerFile::write_entry().
    """
    def _do():
        f.seek(file_pos)
        f.write(data)
        if flush:
            f.flush()

    timed("write", fname, file_pos, chunk_idx, _do,
          threshold_ms=threshold_ms)


def ledger_file_complete(f, fname, file_pos, positions, chunk_idx,
                         *, threshold_ms):
    """
    Complete a ledger chunk: truncate + write positions table + flush.
    Mirrors: LedgerFile::complete() which calls truncate(get_last_idx()),
    then writes the positions table at the end of the file, writes the
    table offset into the header at byte 0, and flushes.
    See ledger.h LedgerFile::complete().
    """
    # truncate(get_last_idx()) -> fflush + ftruncate
    def _truncate():
        f.flush()
        os.ftruncate(f.fileno(), file_pos)

    timed("ftruncate", fname, file_pos, chunk_idx, _truncate,
          threshold_ms=threshold_ms)

    # Write positions table at end, then table offset at header
    def _write_pos():
        f.seek(file_pos)
        table_offset = f.tell()
        pos_data = struct.pack(f"<{len(positions)}Q", *positions)
        f.write(pos_data)
        f.seek(0)
        f.write(struct.pack("<Q", table_offset))
        f.flush()

    timed("write_pos", fname, file_pos, chunk_idx, _write_pos,
          threshold_ms=threshold_ms)


def ledger_file_commit(f, fname, file_pos, committed_name, chunk_idx,
                       *, threshold_ms):
    """
    Commit a completed ledger chunk: fsync + close + rename to .committed.
    Mirrors: LedgerFile::commit() (fsync + rename) and ~LedgerFile (fclose).
    See ledger.h LedgerFile::commit().
    """
    timed("fsync", fname, file_pos, chunk_idx,
          lambda: os.fsync(f.fileno()),
          threshold_ms=threshold_ms)

    timed("close", fname, file_pos, chunk_idx,
          lambda: f.close(),
          threshold_ms=threshold_ms)

    timed("rename", fname, file_pos, chunk_idx,
          lambda: os.rename(fname, committed_name),
          threshold_ms=threshold_ms)


# ── Main ──────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Simulate CCF ledger I/O pattern",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("dir", help="Target directory")
    parser.add_argument("--chunks", type=int, default=20,
                        help="Number of chunk rotations")
    parser.add_argument("--chunk-size", type=int, default=3_800_000,
                        help="Bytes per chunk")
    parser.add_argument("--write-size", type=int, default=283,
                        help="Bytes per entry write")
    parser.add_argument("--threshold-ms", type=float, default=10.0,
                        help="Log ops slower than this")
    args = parser.parse_args()

    os.makedirs(args.dir, exist_ok=True)

    entry_data = b"\xAB" * args.write_size
    ae_batch = 20  # AppendEntries batch size — flush interval

    t_total_start = time.monotonic()
    total_writes = 0

    for chunk_idx in range(args.chunks):
        fname = os.path.join(args.dir, f"ledger_{chunk_idx}")
        committed = os.path.join(args.dir, f"ledger_{chunk_idx}.committed")

        positions = []
        entry_count = 0
        t_chunk_start = time.monotonic()

        #
        # Phase 1: Create new chunk file.
        # Mirrors Ledger::write_entry() -> new LedgerFile(dir, start_idx).
        #
        f, file_pos = ledger_file_create(
            fname, chunk_idx, threshold_ms=args.threshold_ms)

        #
        # Phase 2: Write entries until chunk is full.
        # Mirrors the Ledger::write_entry() -> LedgerFile::write_entry()
        # loop driven by ledger_append ring buffer messages.
        # Committable entries (every ae_batch'th) are flushed.
        #
        while file_pos + args.write_size <= args.chunk_size:
            flush = (entry_count % ae_batch == ae_batch - 1)
            ledger_write_entry(
                f, fname, file_pos, entry_data, flush, chunk_idx,
                threshold_ms=args.threshold_ms)

            positions.append(file_pos)
            file_pos += args.write_size
            entry_count += 1
            total_writes += 1

        #
        # Phase 3: Complete the chunk.
        # Mirrors LedgerFile::complete() — called when
        # FORCE_LEDGER_CHUNK_AFTER flag is set, or when the chunk
        # reaches its size limit.
        # Does: fflush + ftruncate + write positions table + write header.
        #
        ledger_file_complete(
            f, fname, file_pos, positions, chunk_idx,
            threshold_ms=args.threshold_ms)

        #
        # Phase 4: Commit the chunk.
        # Mirrors LedgerFile::commit() — called when the commit index
        # reaches the last entry in the completed chunk.
        # Does: fsync + fclose + rename to .committed suffix.
        #
        ledger_file_commit(
            f, fname, file_pos, committed, chunk_idx,
            threshold_ms=args.threshold_ms)

        chunk_ms = (time.monotonic() - t_chunk_start) * 1000
        rate = entry_count / (chunk_ms / 1000) if chunk_ms > 0 else 0
        print(
            f"[{time.time():.3f}] chunk {chunk_idx} done: "
            f"{entry_count} entries, {file_pos} bytes, "
            f"{chunk_ms:.1f}ms ({rate:.0f} writes/s)",
            flush=True,
        )

    total_ms = (time.monotonic() - t_total_start) * 1000
    rate = total_writes / (total_ms / 1000) if total_ms > 0 else 0
    print(
        f"\nSummary: {args.chunks} chunks, {total_writes} writes, "
        f"{total_ms / 1000:.1f}s total ({rate:.0f} writes/s avg)",
        flush=True,
    )


if __name__ == "__main__":
    main()
