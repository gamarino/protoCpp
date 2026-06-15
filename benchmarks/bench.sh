#!/usr/bin/env bash
# protoCpp benchmark runner.
#
# Median of 5 runs per binary, paired (cpp / proto) on the same row.
# The output table is what RESULTS.md publishes.
set -u
BUILD="${1:-$(dirname "$0")/../build_release}"
RUNS=5

if [[ ! -d "$BUILD" ]]; then
    echo "build dir not found: $BUILD" >&2; exit 2
fi

# List of bench stems. Each must have a `bench_cpp_<stem>` and a
# `bench_proto_<stem>` binary in $BUILD.
BENCHES=(
    int_sum_loop
    call_recursion
    attr_lookup
    list_append_loop
    str_concat_loop
    multithread_cpu
)

# Run a binary N times, print median ms to stdout.
median_ms() {
    local bin="$1"
    local samples=()
    for _ in $(seq 1 "$RUNS"); do
        local t0 t1 ms
        t0=$(date +%s.%N)
        "$bin" > /dev/null 2>&1
        t1=$(date +%s.%N)
        ms=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.2f", (b - a) * 1000 }')
        samples+=("$ms")
    done
    printf '%s\n' "${samples[@]}" | sort -n | awk -v n="$RUNS" '
        { a[NR] = $1 }
        END { print a[int((n + 1) / 2)] }'
}

printf "%-20s %-9s %-9s %-9s %-9s %-9s\n" \
    "bench" "cpp(ms)" "proto(ms)" "fast(ms)" "proto/cpp" "fast/cpp"
echo   "----------------------------------------------------------------------"

for b in "${BENCHES[@]}"; do
    cpp_bin="$BUILD/bench_cpp_$b"
    pr_bin="$BUILD/bench_proto_$b"
    fast_bin="$BUILD/bench_proto_fast_$b"
    if [[ ! -x "$cpp_bin" || ! -x "$pr_bin" ]]; then
        printf "%-20s ** missing binary **\n" "$b"
        continue
    fi
    cpp_ms=$(median_ms "$cpp_bin")
    pr_ms=$(median_ms "$pr_bin")
    pr_ratio=$(awk -v c="$cpp_ms" -v p="$pr_ms" 'BEGIN {
        if (c == 0) print "inf"; else printf "%.2fx", p / c }')
    if [[ -x "$fast_bin" ]]; then
        fast_ms=$(median_ms "$fast_bin")
        fast_ratio=$(awk -v c="$cpp_ms" -v f="$fast_ms" 'BEGIN {
            if (c == 0) print "inf"; else printf "%.2fx", f / c }')
    else
        fast_ms="-"
        fast_ratio="-"
    fi
    printf "%-20s %-9s %-9s %-9s %-9s %-9s\n" \
        "$b" "$cpp_ms" "$pr_ms" "$fast_ms" "$pr_ratio" "$fast_ratio"
done
