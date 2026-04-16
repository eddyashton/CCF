#!/usr/bin/env bash
set -euo pipefail

# run_experiment.sh — Orchestrate multiple CCF perf test runs with tracing.
#
# Runs the CCF basicperf test N times on each mount configuration,
# capturing trace_io.py alongside each run. Collects:
#   - trace CSV (with fixed trace_io.py)
#   - CCF node output logs (3 nodes)
#   - CIFS stats before/after
#   - ccf_like reproducer results
#
# Usage:
#   ./run_experiment.sh [--runs N] [--mounts fs,fs2] [--skip-ccf] [--skip-ccflike]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TRACE_IO="$SCRIPT_DIR/trace_io.py"
CCF_LIKE="$SCRIPT_DIR/ccf_like.py"
PLOT_TRACE="$SCRIPT_DIR/plot_trace_events.py"
RESULTS_DIR="$SCRIPT_DIR/experiments/$(date +%Y%m%d_%H%M%S)"

RUNS=3
SKIP_CCF=0
SKIP_CCFLIKE=0

# Mount configurations
declare -A MOUNTS
MOUNTS[local]="/tmp"
MOUNTS[fs]="/mnt/azure-fs"      # actimeo=1
MOUNTS[fs2]="/mnt/azure-fs2"    # actimeo=30
MOUNT_LIST=(local fs fs2)        # default run order

# CCF test parameters (matching previous runs)
WORKER_THREADS=2
SNAPSHOT_TX_INTERVAL=10000
CLIENT_DEF="2,write,20000,primary"
CLIENT_TIMEOUT=60
CONSTITUTIONS=(
    "$SCRIPT_DIR/samples/constitutions/default/actions.js"
    "$SCRIPT_DIR/samples/constitutions/default/validate.js"
    "$SCRIPT_DIR/samples/constitutions/default/resolve.js"
    "$SCRIPT_DIR/samples/constitutions/default/apply.js"
)

# ccf_like parameters
CCFLIKE_CHUNKS=5
CCFLIKE_CHUNK_SIZE=3800000
CCFLIKE_WRITE_SIZE=283
CCFLIKE_THRESHOLD=10

while [[ $# -gt 0 ]]; do
    case "$1" in
        --runs) RUNS="$2"; shift 2 ;;
        --mounts) IFS=',' read -ra MOUNT_LIST <<< "$2"; shift 2 ;;
        --skip-ccf) SKIP_CCF=1; shift ;;
        --skip-ccflike) SKIP_CCFLIKE=1; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

mkdir -p "$RESULTS_DIR"
echo "Results directory: $RESULTS_DIR"
echo "Runs per mount: $RUNS"
echo "Mounts: ${MOUNT_LIST[*]}"

# Cleanup on exit: kill any orphan bpftrace processes we spawned
TRACE_PIDS=()
cleanup() {
    for pid in "${TRACE_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT

capture_cifs_stats() {
    local outfile="$1"
    cat /proc/fs/cifs/Stats >> "$outfile" 2>/dev/null || echo "(not available)" >> "$outfile"
}

reset_cifs_stats() {
    echo 0 > /proc/fs/cifs/Stats 2>/dev/null || true
}

plot_trace_outputs() {
    local trace_csv="$1"

    if [[ ! -f "$PLOT_TRACE" ]]; then
        echo "  Plot script not found, skipping plots: $PLOT_TRACE"
        return
    fi

    # Skip plotting when trace has no event rows beyond header/comments.
    local event_rows
    event_rows=$(awk -F, '!/^#/ && $1!="time_s" && NF>=7 {n++} END {print n+0}' "$trace_csv")
    if [[ "$event_rows" -eq 0 ]]; then
        echo "  Trace has no events, skipping plots: $trace_csv"
        return
    fi

    local base="${trace_csv%.csv}"
    local all_plot="${base}_all_events.png"
    local meta_plot="${base}_metadata_ops.png"

    echo "  Plotting all events..."
    python3 "$PLOT_TRACE" "$trace_csv" -o "$all_plot"
    echo "  Plotting metadata ops (O,C,F,N,U,T,D)..."
    python3 "$PLOT_TRACE" "$trace_csv" --ops O,C,F,N,U,T,D -o "$meta_plot"
}

run_ccf_test() {
    local mount_key="$1"
    local run_num="$2"
    local mount_path="${MOUNTS[$mount_key]}"
    local run_dir="$RESULTS_DIR/${mount_key}_run${run_num}"
    local workspace="$mount_path/CCF.exp.${run_num}"
    local label="pi_exp_${mount_key}_${run_num}"

    mkdir -p "$run_dir"
    echo "[CCF] Run $run_num on $mount_key ($mount_path) -> $run_dir"

    # Reset CIFS stats before test
    reset_cifs_stats

    # Start trace_io
    BPFTRACE_KERNEL_SOURCE=/usr/src/linux-headers-6.6.130.1-3.azl3 \
        python3 "$TRACE_IO" --comm basic -o "$run_dir/trace.csv" &
    local trace_pid=$!
    TRACE_PIDS+=("$trace_pid")
    sleep 5  # wait for bpftrace to attach

    # Build constitution args
    local const_args=()
    for c in "${CONSTITUTIONS[@]}"; do
        const_args+=(--constitution "$c")
    done

    # Run the test (must run from build directory)
    pushd "$BUILD_DIR" > /dev/null
    source env/bin/activate
    PYTHONPATH=/root/CCF/tests python3 "$SCRIPT_DIR/tests/infra/basicperf.py" \
        -b "." \
        -c "./submit" \
        --log-level info \
        --worker-threads "$WORKER_THREADS" \
        "${const_args[@]}" \
        --label "$label" \
        --perf-label "Experiment" \
        --snapshot-tx-interval "$SNAPSHOT_TX_INTERVAL" \
        --package "samples/apps/basic/basic" \
        --client-def "$CLIENT_DEF" \
        --workspace "$workspace" \
        --client-timeout-s "$CLIENT_TIMEOUT" \
        2>&1 | tee "$run_dir/test_output.log" || true
    popd > /dev/null

    sleep 3  # let trace_io drain

    # Stop trace
    kill "$trace_pid" 2>/dev/null; wait "$trace_pid" 2>/dev/null || true

    # Capture CIFS stats after test
    capture_cifs_stats "$run_dir/cifs_stats.txt"

    # Generate trace visualisations.
    plot_trace_outputs "$run_dir/trace.csv"

    # Copy node output logs (include pid in filename for trace correlation)
    for i in 0 1 2; do
        local node_dir="$workspace/${label}_${i}"
        local node_out="$node_dir/out"
        local pid_file="$node_dir/node.pid"
        if [[ -f "$node_out" ]]; then
            local node_pid=""
            if [[ -f "$pid_file" ]]; then
                node_pid=$(cat "$pid_file")
            fi
            cp "$node_out" "$run_dir/node_${i}.${node_pid:-unknown}.out"
        fi
    done

    # Extract summary stats
    echo "  Extracting summary..."
    python3 -c "
import csv; from collections import defaultdict
ops = defaultdict(lambda: {'n':0,'slow':0,'max':0})
with open('$run_dir/trace.csv') as f:
    for row in csv.reader(f):
        if not row or row[0].startswith('#') or row[0]=='time_s': continue
        op, lat = row[2], float(row[1])
        ops[op]['n'] += 1
        if lat > 10000: ops[op]['slow'] += 1
        if lat > ops[op]['max']: ops[op]['max'] = lat
for op in sorted(ops):
    d = ops[op]
    print(f'  {op:2s}: n={d[\"n\"]:>7d}  slow(>10ms)={d[\"slow\"]:>5d}  max={d[\"max\"]/1000:>8.1f}ms')
" | tee "$run_dir/summary.txt"

    # Count slow-op logs per node
    for node_file in "$run_dir"/node_*.out; do
        if [[ -f "$node_file" ]]; then
            local count=$(grep -c "Operation took" "$node_file" 2>/dev/null || echo 0)
            echo "  $(basename "$node_file"): $count 'Operation took too long' entries"
        fi
    done | tee -a "$run_dir/summary.txt"
}

run_ccflike_test() {
    local mount_key="$1"
    local run_num="$2"
    local mount_path="${MOUNTS[$mount_key]}"
    local run_dir="$RESULTS_DIR/${mount_key}_ccflike_run${run_num}"
    local bench_dir="$mount_path/ccflike_exp_${run_num}"

    mkdir -p "$run_dir"
    echo "[ccf_like] Run $run_num on $mount_key ($mount_path) -> $run_dir"

    # Start trace
    BPFTRACE_KERNEL_SOURCE=/usr/src/linux-headers-6.6.130.1-3.azl3 \
        python3 "$TRACE_IO" --comm python3 --mount "$mount_path" -o "$run_dir/trace.csv" &
    local trace_pid=$!
    TRACE_PIDS+=("$trace_pid")
    sleep 5

    # Run ccf_like
    python3 "$CCF_LIKE" \
        "$bench_dir" \
        --chunks "$CCFLIKE_CHUNKS" \
        --chunk-size "$CCFLIKE_CHUNK_SIZE" \
        --write-size "$CCFLIKE_WRITE_SIZE" \
        --threshold-ms "$CCFLIKE_THRESHOLD" \
        2>"$run_dir/ccflike_output.log" || true

    sleep 3  # let trace_io drain

    # Stop trace
    kill "$trace_pid" 2>/dev/null; wait "$trace_pid" 2>/dev/null || true

    # Generate trace visualisations.
    plot_trace_outputs "$run_dir/trace.csv"

    echo "  ccf_like output:"
    cat "$run_dir/ccflike_output.log"

}

# Main execution
echo ""
echo "=========================================="
echo " Starting experiment at $(date -Iseconds)"
echo "=========================================="

for mount_key in "${MOUNT_LIST[@]}"; do
    echo ""
    echo "===== Mount: $mount_key (${MOUNTS[$mount_key]}) ====="

    if [[ "$SKIP_CCF" -eq 0 ]]; then
        for run in $(seq 1 "$RUNS"); do
            run_ccf_test "$mount_key" "$run"
            echo ""
        done
    fi

    if [[ "$SKIP_CCFLIKE" -eq 0 ]]; then
        for run in $(seq 1 "$RUNS"); do
            run_ccflike_test "$mount_key" "$run"
            echo ""
        done
    fi
done

echo ""
echo "=========================================="
echo " Experiment complete: $RESULTS_DIR"
echo "=========================================="
echo ""
echo "Results structure:"
find "$RESULTS_DIR" -type f | sort | sed 's|^|  |'
