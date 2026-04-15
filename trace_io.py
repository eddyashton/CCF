#!/usr/bin/env python3
"""Trace I/O syscall latencies via bpftrace and produce CSV for plot_bench.py.

Usage:
    # Trace all I/O, Ctrl+C to stop:
    python3 trace_io.py -o trace.csv

    # Trace only a specific command (e.g. cchost and its children):
    python3 trace_io.py --comm cchost -o trace.csv

    # Filter to a mount point (post-processing filter on paths):
    python3 trace_io.py --mount /mnt/azure-fs -o trace.csv

    # Then plot:
    python3 plot_bench.py trace.csv -o trace.svg
"""

import argparse
import csv
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time


def run_bpftrace(bt_script, raw_path, stop_event):
    """Run bpftrace, writing raw output to a file."""
    cmd = ["bpftrace", "--no-warnings", bt_script]
    print(f"Running: {' '.join(cmd)}", file=sys.stderr)
    with open(raw_path, "w") as f:
        proc = subprocess.Popen(cmd, stdout=f, stderr=subprocess.PIPE)

    def read_stderr():
        for line in proc.stderr:
            line = line.decode().rstrip()
            if line:
                print(f"bpftrace: {line}", file=sys.stderr)

    stderr_thread = threading.Thread(target=read_stderr, daemon=True)
    stderr_thread.start()

    def on_stop():
        stop_event.wait()
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print("bpftrace did not exit after SIGINT, sending SIGKILL", file=sys.stderr)
            proc.kill()

    stopper = threading.Thread(target=on_stop, daemon=True)
    stopper.start()

    proc.wait()
    raw_size = os.path.getsize(raw_path)
    print(f"bpftrace exited (code {proc.returncode}), raw output: {raw_size} bytes",
          file=sys.stderr)


def convert_raw(raw_path, output_path, epoch_offset, mount_filter):
    """Convert raw bpftrace output to CSV format.

    Raw format: mono_ns,lat_us,pid,tid,fd,op,size[,path[,path2]]
    op: W (write), R (read), F (fsync), O (openat), C (close), N (rename), M (mutex)
    size: byte count for R/W, 0 for others
    path: present for O (openat) and N (rename old path)
    path2: present for N (rename new path), appended to path as "old -> new"
    """
    # First pass: build fd->path map from openat lines
    fd_paths = {}  # (pid, fd) -> path
    with open(raw_path) as rf:
        for line in rf:
            line = line.strip()
            if not line or line.startswith("Attaching"):
                continue
            parts = line.split(",", 8)
            if len(parts) < 7:
                continue
            try:
                op = parts[5]
                if op == "O" and len(parts) >= 8:
                    tgid = int(parts[3])  # bpftrace 'tid' = tgid
                    fd = int(parts[4])
                    path = parts[7]
                    fd_paths[(tgid, fd)] = path
                elif op == "C":
                    tgid = int(parts[3])
                    fd = int(parts[4])
                    fd_paths.pop((tgid, fd), None)
            except (ValueError, IndexError):
                continue

    if fd_paths:
        print(f"Built fd->path map: {len(fd_paths)} entries")

    # Second pass: convert all timed events
    written = 0
    skipped_mount = 0
    with open(raw_path) as rf, open(output_path, "w", newline="") as of:
        # Write epoch offset as a comment for consumers to reconstruct wallclock
        of.write(f"# epoch_offset={epoch_offset:.6f}\n")
        w = csv.writer(of)
        w.writerow(["time_s", "latency_us", "op", "size", "pid", "tid", "file"])
        for line in rf:
            line = line.strip()
            if not line or line.startswith("Attaching"):
                continue
            parts = line.split(",", 8)
            if len(parts) < 7:
                continue
            try:
                mono_ns = int(parts[0])
                lat_us = int(parts[1])
                # bpftrace pid=tgid (process), tid=task (thread) — but some
                # versions swap these. In our output, parts[2] is bpftrace's
                # pid (actually tid/task) and parts[3] is bpftrace's tid
                # (actually tgid/process). Swap to get conventional meaning.
                tid = int(parts[2])  # bpftrace 'pid' = task id
                pid = int(parts[3])  # bpftrace 'tid' = tgid
                fd = int(parts[4])
                op = parts[5]
                size = int(parts[6])
            except (ValueError, IndexError):
                continue

            if op not in ("W", "R", "F", "O", "C", "N", "M", "D", "S", "T", "U"):
                continue

            # Resolve path
            if op == "N" and len(parts) >= 9:
                path = f"{parts[7]} -> {parts[8]}"
            elif op == "O" and len(parts) >= 8:
                path = parts[7]
            elif op == "T" and len(parts) >= 8:
                # stat-family: size carries syscall number (262 or 332), path in parts[7]
                sc = {262: "newfstatat", 332: "statx"}.get(size, f"stat_{size}")
                path = f"{sc}:{parts[7]}"
            elif op == "M":
                path = f"futex@{size}"  # size field carries the futex address
            elif op == "S":
                # size field carries the syscall number
                SYSCALL_NAMES = {
                    8: "lseek", 9: "mmap", 10: "mprotect", 11: "munmap",
                    12: "brk", 16: "ioctl", 17: "pread64", 18: "pwrite64",
                    20: "writev", 21: "access", 22: "pipe", 23: "select",
                    25: "mremap", 28: "madvise", 32: "dup", 33: "dup2",
                    35: "nanosleep", 39: "getpid", 44: "sendto", 45: "recvfrom",
                    46: "sendmsg", 47: "recvmsg", 48: "shutdown",
                    49: "bind", 50: "listen", 51: "getsockname",
                    52: "getpeername", 53: "socketpair", 54: "setsockopt",
                    55: "getsockopt", 56: "clone", 59: "execve",
                    72: "fcntl", 73: "flock", 77: "ftruncate",
                    79: "getcwd", 80: "chdir", 83: "mkdir", 84: "rmdir",
                    85: "creat", 86: "link", 87: "unlink", 88: "symlink",
                    89: "readlink", 90: "chmod", 91: "fchmod",
                    232: "epoll_wait", 233: "epoll_ctl",
                    262: "newfstatat", 280: "utimensat",
                    281: "epoll_pwait", 288: "accept4",
                    291: "epoll_create1", 292: "dup3",
                    302: "prlimit64", 318: "getrandom",
                    334: "faccessat2", 439: "faccessat2",
                }
                sc_name = SYSCALL_NAMES.get(size, f"syscall_{size}")
                path = sc_name
            else:
                path = fd_paths.get((pid, fd), "")

            if mount_filter and op not in ("M",) and not path.startswith(mount_filter):
                skipped_mount += 1
                continue

            time_s = epoch_offset + mono_ns / 1e9
            file_label = path if path else f"pid{pid}:fd{fd}"
            w.writerow([f"{time_s:.6f}", f"{lat_us:.1f}", op, size, pid, tid, file_label])
            written += 1

    parts_msg = [f"{written} events"]
    if skipped_mount:
        parts_msg.append(f"{skipped_mount} filtered by mount")
    print(f"Converted: {', '.join(parts_msg)}")


def build_bpftrace_script(comm_filter=None):
    """Build the bpftrace script string.

    If comm_filter is set, only trace processes matching that command name.
    Uses simple comm == check on each probe (no fork tracking, avoids
    sched tracepoints that need kernel headers).
    """
    if comm_filter:
        fork_block = ""
        pred = f'comm == "{comm_filter}"'
        pred_fd = f'(comm == "{comm_filter}" && args->fd > 2)'
        end_extra = ""
    else:
        fork_block = ""
        pred = "1"
        pred_fd = "args->fd > 2"
        end_extra = ""

    return f"""
{fork_block}

/* Track fd->path via openat, with latency */
tracepoint:syscalls:sys_enter_openat / {pred} / {{
    @openpath[tid] = args->filename;
    @ostart[tid] = nsecs;
}}

tracepoint:syscalls:sys_exit_openat / @ostart[tid] / {{
    $lat_us = (nsecs - @ostart[tid]) / 1000;
    $fd = args->ret;
    if ($fd >= 0) {{
        printf("%llu,%llu,%d,%d,%d,O,0,%s\\n",
               nsecs, $lat_us, pid, tid, $fd, str(@openpath[tid]));
    }}
    delete(@openpath[tid]);
    delete(@ostart[tid]);
}}

/* Close tracing */
tracepoint:syscalls:sys_enter_close / {pred_fd} / {{
    @cstart[tid] = nsecs;
    @cfd[tid] = args->fd;
}}

tracepoint:syscalls:sys_exit_close / @cstart[tid] / {{
    $lat_us = (nsecs - @cstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,C,0\\n",
           nsecs, $lat_us, pid, tid, @cfd[tid]);
    delete(@cstart[tid]);
    delete(@cfd[tid]);
}}

/* Write tracing */
tracepoint:syscalls:sys_enter_write / {pred_fd} / {{
    @start[tid] = nsecs;
    @fd[tid] = args->fd;
    @wcount[tid] = args->count;
}}

tracepoint:syscalls:sys_exit_write / @start[tid] / {{
    $lat_us = (nsecs - @start[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,W,%llu\\n",
           nsecs, $lat_us, pid, tid, @fd[tid], @wcount[tid]);
    delete(@start[tid]);
    delete(@fd[tid]);
    delete(@wcount[tid]);
}}

/* Read tracing */
tracepoint:syscalls:sys_enter_read / {pred_fd} / {{
    @rstart[tid] = nsecs;
    @rfd[tid] = args->fd;
    @rcount[tid] = args->count;
}}

tracepoint:syscalls:sys_exit_read / @rstart[tid] / {{
    $lat_us = (nsecs - @rstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,R,%llu\\n",
           nsecs, $lat_us, pid, tid, @rfd[tid], @rcount[tid]);
    delete(@rstart[tid]);
    delete(@rfd[tid]);
    delete(@rcount[tid]);
}}

/* Fsync tracing */
tracepoint:syscalls:sys_enter_fsync / {pred} / {{
    @fstart[tid] = nsecs;
    @ffd[tid] = args->fd;
}}

tracepoint:syscalls:sys_exit_fsync / @fstart[tid] / {{
    $lat_us = (nsecs - @fstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,F,0\\n",
           nsecs, $lat_us, pid, tid, @ffd[tid]);
    delete(@fstart[tid]);
    delete(@ffd[tid]);
}}

/* Directory listing (getdents64) */
tracepoint:syscalls:sys_enter_getdents64 / {pred_fd} / {{
    @dstart[tid] = nsecs;
    @dfd[tid] = args->fd;
}}

tracepoint:syscalls:sys_exit_getdents64 / @dstart[tid] / {{
    $lat_us = (nsecs - @dstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,D,%d\\n",
           nsecs, $lat_us, pid, tid, @dfd[tid], args->ret);
    delete(@dstart[tid]);
    delete(@dfd[tid]);
}}

/* Rename tracing — hook all three variants */
tracepoint:syscalls:sys_enter_rename / {pred} / {{
    @rnold[tid] = args->oldname;
    @rnnew[tid] = args->newname;
    @rnstart[tid] = nsecs;
}}

tracepoint:syscalls:sys_enter_renameat / {pred} / {{
    @rnold[tid] = args->oldname;
    @rnnew[tid] = args->newname;
    @rnstart[tid] = nsecs;
}}

tracepoint:syscalls:sys_enter_renameat2 / {pred} / {{
    @rnold[tid] = args->oldname;
    @rnnew[tid] = args->newname;
    @rnstart[tid] = nsecs;
}}

tracepoint:syscalls:sys_exit_rename / @rnstart[tid] / {{
    $lat_us = (nsecs - @rnstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,0,N,0,%s,%s\\n",
           nsecs, $lat_us, pid, tid, str(@rnold[tid]), str(@rnnew[tid]));
    delete(@rnold[tid]);
    delete(@rnnew[tid]);
    delete(@rnstart[tid]);
}}

tracepoint:syscalls:sys_exit_renameat / @rnstart[tid] / {{
    $lat_us = (nsecs - @rnstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,0,N,0,%s,%s\\n",
           nsecs, $lat_us, pid, tid, str(@rnold[tid]), str(@rnnew[tid]));
    delete(@rnold[tid]);
    delete(@rnnew[tid]);
    delete(@rnstart[tid]);
}}

tracepoint:syscalls:sys_exit_renameat2 / @rnstart[tid] / {{
    $lat_us = (nsecs - @rnstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,0,N,0,%s,%s\\n",
           nsecs, $lat_us, pid, tid, str(@rnold[tid]), str(@rnnew[tid]));
    delete(@rnold[tid]);
    delete(@rnnew[tid]);
    delete(@rnstart[tid]);
}}

/* stat-family: newfstatat and statx — captures path, always emitted.
   op=T (sTat). glibc may route fs::exists via either syscall. */
tracepoint:syscalls:sys_enter_newfstatat / {pred} / {{
    @stpath[tid] = args->filename;
    @ststart[tid] = nsecs;
}}

tracepoint:syscalls:sys_exit_newfstatat / @ststart[tid] / {{
    $lat_us = (nsecs - @ststart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,0,T,262,%s\\n",
           nsecs, $lat_us, pid, tid, str(@stpath[tid]));
    delete(@stpath[tid]);
    delete(@ststart[tid]);
}}

tracepoint:syscalls:sys_enter_statx / {pred} / {{
    @sxpath[tid] = args->filename;
    @sxstart[tid] = nsecs;
}}

tracepoint:syscalls:sys_exit_statx / @sxstart[tid] / {{
    $lat_us = (nsecs - @sxstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,0,T,332,%s\\n",
           nsecs, $lat_us, pid, tid, str(@sxpath[tid]));
    delete(@sxpath[tid]);
    delete(@sxstart[tid]);
}}

/* Mutex contention via futex — FUTEX_WAIT variants, latency >100us.
   op=0 FUTEX_WAIT, op=9 FUTEX_WAIT_BITSET, +128 for PRIVATE variants */
tracepoint:syscalls:sys_enter_futex / {pred} && (args->op == 0 || args->op == 9 || args->op == 128 || args->op == 137) / {{
    @mstart[tid] = nsecs;
    @maddr[tid] = args->uaddr;
}}

tracepoint:syscalls:sys_exit_futex / @mstart[tid] / {{
    $lat_us = (nsecs - @mstart[tid]) / 1000;
    $ret = args->ret;
    if ($lat_us > 100 && $ret == 0) {{
        printf("%llu,%llu,%d,%d,0,M,%llu\\n",
               nsecs, $lat_us, pid, tid, @maddr[tid]);
    }}
    delete(@mstart[tid]);
    delete(@maddr[tid]);
}}

/* ftruncate — dedicated probe since raw_syscalls misses long calls */
tracepoint:syscalls:sys_enter_ftruncate / {pred} / {{
    @ftstart[tid] = nsecs;
    @ftfd[tid] = args->fd;
    @ftlen[tid] = args->length;
}}

tracepoint:syscalls:sys_exit_ftruncate / @ftstart[tid] / {{
    $lat_us = (nsecs - @ftstart[tid]) / 1000;
    printf("%llu,%llu,%d,%d,%d,U,%llu\\n",
           nsecs, $lat_us, pid, tid, @ftfd[tid], @ftlen[tid]);
    delete(@ftstart[tid]);
    delete(@ftfd[tid]);
    delete(@ftlen[tid]);
}}

END {{
    clear(@openpath); clear(@ostart);
    clear(@cstart); clear(@cfd);
    clear(@start); clear(@fd); clear(@wcount);
    clear(@rstart); clear(@rfd); clear(@rcount);
    clear(@fstart); clear(@ffd);
    clear(@dstart); clear(@dfd);
    clear(@rnold); clear(@rnnew); clear(@rnstart);
    clear(@stpath); clear(@ststart);
    clear(@sxpath); clear(@sxstart);
    clear(@mstart); clear(@maddr);
    clear(@ftstart); clear(@ftfd); clear(@ftlen);
{end_extra}
}}
"""


def main():
    parser = argparse.ArgumentParser(
        description="Trace I/O latencies via bpftrace, output CSV for plot_bench.py",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("-o", "--output", default="trace.csv",
                        help="Output CSV path")
    parser.add_argument("--mount", default="",
                        help="Only include I/O to files under this path (e.g. /mnt/azure-fs)")
    parser.add_argument("--comm", default="",
                        help="Only trace processes with this command name (e.g. basic)")
    args = parser.parse_args()

    # Capture wall-clock offset: bpftrace nsecs is CLOCK_MONOTONIC
    wall = time.time()
    mono = time.monotonic_ns()
    epoch_offset = wall - mono / 1e9

    tmpdir = tempfile.mkdtemp(prefix="trace_io_")
    bt_path = os.path.join(tmpdir, "trace.bt")
    raw_path = os.path.join(tmpdir, "raw.csv")

    bt_script = build_bpftrace_script(comm_filter=args.comm or None)
    with open(bt_path, "w") as f:
        f.write(bt_script)

    stop_event = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop_event.set())
    signal.signal(signal.SIGTERM, lambda *_: stop_event.set())

    print("Tracing I/O syscalls...")
    if args.comm:
        print(f"Command filter: {args.comm}")
    else:
        print("No command filter (capturing all processes)")
    if args.mount:
        print(f"Mount filter: {args.mount}")
    print("Press Ctrl+C to stop.")
    print()

    run_bpftrace(bt_path, raw_path, stop_event)
    stop_event.set()

    convert_raw(raw_path, args.output, epoch_offset, args.mount)

    try:
        os.unlink(bt_path)
        os.unlink(raw_path)
        os.rmdir(tmpdir)
    except OSError:
        pass


if __name__ == "__main__":
    main()
