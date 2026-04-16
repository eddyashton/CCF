#!/usr/bin/env python3
"""Plot filesystem trace events from a trace_io.py CSV file.

Expected CSV format (comments allowed):
    # epoch_offset=...
    time_s,latency_us,op,size,pid,tid,file

Example:
    python3 plot_trace_events.py experiments/20260416_083206/local_run1/trace.csv
    python3 plot_trace_events.py trace.csv -o trace_scatter.svg --ops U,N,F,O
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import matplotlib.pyplot as plt


OP_LABELS = {
    "O": "openat",
    "C": "close",
    "R": "read",
    "W": "write",
    "F": "fsync",
    "N": "rename",
    "D": "getdents64",
    "T": "stat/newfstatat",
    "U": "ftruncate",
    "M": "futex_wait",
    "S": "syscall_other",
}

OP_COLORS = {
    "U": "#b80000",
    "N": "#e65100",
    "F": "#0050d0",
    "O": "#7b1fa2",
    "C": "#00838f",
    "W": "#1b5e20",
    "R": "#33691e",
    "T": "#4a148c",
    "D": "#e6a800",
    "M": "#212121",
    "S": "#757575",
}

OP_MARKERS = {
    "U": "D",
    "N": "^",
    "F": "s",
    "O": "p",
    "C": "h",
    "W": "o",
    "R": "d",
    "T": "v",
    "D": "*",
    "M": "x",
    "S": "+",
}


@dataclass(frozen=True)
class Event:
    time_s: float
    latency_us: float
    op: str
    size: int
    pid: int
    tid: int
    file: str


def read_trace_csv(path: Path) -> list[Event]:
    events: list[Event] = []

    with path.open("r", newline="") as f:
        reader = csv.reader(f)
        header_seen = False

        for row in reader:
            if not row:
                continue
            if row[0].startswith("#"):
                continue

            # First non-comment row is the expected header.
            if not header_seen:
                header_seen = True
                continue

            if len(row) < 7:
                continue

            try:
                events.append(
                    Event(
                        time_s=float(row[0]),
                        latency_us=float(row[1]),
                        op=row[2],
                        size=int(row[3]),
                        pid=int(row[4]),
                        tid=int(row[5]),
                        file=row[6],
                    )
                )
            except ValueError:
                # Skip malformed rows.
                continue

    return events


def filter_events(
    events: Iterable[Event],
    allowed_ops: set[str] | None,
    min_latency_us: float,
    max_latency_us: float | None,
) -> list[Event]:
    out: list[Event] = []

    for e in events:
        if allowed_ops is not None and e.op not in allowed_ops:
            continue
        if e.latency_us < min_latency_us:
            continue
        if max_latency_us is not None and e.latency_us > max_latency_us:
            continue
        out.append(e)

    return out


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot time vs latency scatter from trace_io CSV",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("trace_csv", type=Path, help="Input trace CSV")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output image path (.png/.svg/.pdf). Defaults to <input>_scatter.png",
    )
    parser.add_argument(
        "--ops",
        default="",
        help="Comma-separated op codes to include (example: U,N,F,O). Default: all",
    )
    parser.add_argument(
        "--min-latency-us",
        type=float,
        default=0.0,
        help="Minimum latency (microseconds) to include",
    )
    parser.add_argument(
        "--max-latency-us",
        type=float,
        default=None,
        help="Maximum latency (microseconds) to include",
    )
    parser.add_argument(
        "--absolute-time",
        action="store_true",
        help="Use absolute epoch time on X axis instead of seconds since trace start",
    )
    parser.add_argument(
        "--linear-y",
        action="store_true",
        help="Use linear Y axis (default is log scale)",
    )
    parser.add_argument(
        "--point-size",
        type=float,
        default=14.0,
        help="Marker size",
    )
    parser.add_argument(
        "--alpha",
        type=float,
        default=0.85,
        help="Marker transparency",
    )
    parser.add_argument(
        "--title",
        default="",
        help="Custom plot title",
    )

    args = parser.parse_args()

    if not args.trace_csv.exists():
        raise SystemExit(f"Input file does not exist: {args.trace_csv}")

    output = args.output
    if output is None:
        output = args.trace_csv.with_name(f"{args.trace_csv.stem}_scatter.png")

    allowed_ops: set[str] | None = None
    if args.ops.strip():
        allowed_ops = {item.strip() for item in args.ops.split(",") if item.strip()}

    events = read_trace_csv(args.trace_csv)
    if not events:
        raise SystemExit("No events found in CSV")

    events = filter_events(
        events,
        allowed_ops=allowed_ops,
        min_latency_us=args.min_latency_us,
        max_latency_us=args.max_latency_us,
    )
    if not events:
        raise SystemExit("No events left after filtering")

    t0 = min(e.time_s for e in events)
    x_vals = []
    y_vals_ms = []
    op_vals = []

    for e in events:
        x_vals.append(e.time_s if args.absolute_time else (e.time_s - t0))
        y_vals_ms.append(e.latency_us / 1000.0)
        op_vals.append(e.op)

    fig, ax = plt.subplots(figsize=(13, 7))

    # Plot per op so legend remains useful.
    unique_ops = sorted(set(op_vals))
    for op in unique_ops:
        xs = [x_vals[i] for i in range(len(events)) if op_vals[i] == op]
        ys = [y_vals_ms[i] for i in range(len(events)) if op_vals[i] == op]
        label = f"{op} ({OP_LABELS.get(op, 'unknown')}), n={len(xs)}"
        marker = OP_MARKERS.get(op, "o")
        # Unfilled markers (x, +) use edgecolors, not facecolors.
        filled = marker not in ("x", "+", "|", "_")
        scatter_kw = dict(
            s=args.point_size,
            alpha=args.alpha,
            label=label,
            c=OP_COLORS.get(op, "#444444"),
            marker=marker,
        )
        if filled:
            scatter_kw["edgecolors"] = "none"
        ax.scatter(xs, ys, **scatter_kw)

    ax.set_xlabel("Epoch time (s)" if args.absolute_time else "Time since trace start (s)")
    ax.set_ylabel("Latency (ms)")
    if not args.linear_y:
        y_min, y_max = min(y_vals_ms), max(y_vals_ms)
        # Only use log scale when data spans more than ~1.5 orders of magnitude;
        # otherwise the tick labels degrade into ugly "2.4 × 10²" form.
        use_log = False
        if y_min > 0 and y_max / y_min > 30:
            use_log = True
        elif y_min <= 0 and y_max > 0:
            # Data includes zero/negative — wide range, log is appropriate
            use_log = True
        if use_log:
            ax.set_yscale("log")

    title = args.title.strip()
    if not title:
        title = f"Trace Events: {args.trace_csv.name}"
    ax.set_title(title)

    ax.grid(True, which="both", linestyle="--", linewidth=0.5, alpha=0.45)
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), frameon=True, fontsize=9)
    fig.tight_layout()

    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=170)

    print(f"Read {len(events)} filtered events")
    print(f"Wrote plot: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
