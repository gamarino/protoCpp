#!/usr/bin/env bash
# costmodel/run.sh — runs the kernel cost model, its two CPython twins (C API
# and interpreter), the list-operation model and the structure labs, and prints
# one markdown report.
#
#   costmodel/run.sh [build-dir] [python]
#
# Every number is measured in-process (start-up and imports never enter), as
# the median of several batches, on the wall clock and on process CPU time.
# Guards against the biases a single run carries:
#   * ROUNDS rounds (COSTMODEL_ROUNDS, default 5); the report gives the median
#     of the rounds and their spread. The order of the comparison programs
#     rotates every round, so no program always runs first (cold) or last.
#   * Every round loads a fresh copy of libprotoCore (LD_LIBRARY_PATH beats the
#     binaries' RUNPATH): the same library bytes can run measurably slower from
#     one file instance than from another.
#   * Quiet gate: before every program, waits up to COSTMODEL_WAIT seconds
#     (default 600) until the whole machine is less than COSTMODEL_MAX_BUSY
#     percent busy (default 10) over a 5-second window of /proc/stat, and stops
#     if it never is, unless COSTMODEL_ALLOW_LOAD=1. CPU use, not the load
#     average: on a ZFS root, kernel threads in D state keep the load average
#     above 2 on an idle machine. Busy % and load are recorded per program;
#     the report flags runs that started busy.
#   * Programs run one at a time. Any program that fails its own result check
#     exits non-zero and stops the run: no partial report.
#   * The CPython C-API twin is compiled here against the interpreter given, so
#     both CPython columns measure the same CPython.
#   * The environment (CPU, frequency policy, kernel, versions, heap mode, the
#     protoCore library actually loaded) goes into the report.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-$HERE/../build_release}"
PY="${2:-python3}"
ROUNDS="${COSTMODEL_ROUNDS:-5}"
MAX_BUSY="${COSTMODEL_MAX_BUSY:-10}"
WAIT="${COSTMODEL_WAIT:-600}"
HEAP="${COSTMODEL_HEAP:-adaptive}"           # adaptive | default
[[ -d "$BUILD/bin" ]] && BUILD="$BUILD/bin"
BUILD="$(cd "$BUILD" && pwd)"
for b in costmodel_kernel costmodel_list_ops_kernel costmodel_list_ops_lab costmodel_list_lab costmodel_string_lab; do
    [[ -x "$BUILD/$b" ]] || { echo "missing $BUILD/$b: build the costmodel targets first" >&2; exit 1; }
done
OUT="$(mktemp -d "${TMPDIR:-/tmp}/costmodel.XXXXXX")"
echo "run directory: $OUT" >&2

load1() { cut -d' ' -f1 /proc/loadavg; }
over() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > b) }'; }
# busy: percent of all CPUs busy over a 5 s window (idle and iowait count as idle)
busy() {
    local a b
    a=($(head -1 /proc/stat)); sleep 5; b=($(head -1 /proc/stat))
    awk -v a="${a[*]}" -v b="${b[*]}" 'BEGIN {
        n = split(a, x, " "); split(b, y, " "); t = 0
        for (i = 2; i <= n; i++) t += y[i] - x[i]
        idle = (y[5] - x[5]) + (y[6] - x[6])
        printf "%.1f", (t > 0 ? 100 * (t - idle) / t : 0) }'
}

# ---- the C-API twin, built against the interpreter being measured
INC="$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("INCLUDEPY"))')"
LIBDIR="$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR"))')"
LDVER="$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("LDVERSION"))')"
CAPI="$OUT/capi_costmodel"
${CC:-cc} -O3 -DNDEBUG -Wall -I"$INC" -I"$HERE" "$HERE/capi_costmodel.c" \
    -L"$LIBDIR" -lpython"$LDVER" -Wl,-rpath,"$LIBDIR" -o "$CAPI"

# ---- the protoCore library the binaries resolve, copied fresh per round
PCLIB="$(ldd "$BUILD/costmodel_kernel" | awk '/libprotoCore/ { print $3 }')"
[[ -f "$PCLIB" ]] || { echo "libprotoCore not resolved for costmodel_kernel" >&2; exit 1; }
PCSONAME="$(basename "$PCLIB")"

case "$HEAP" in
    adaptive) HEAPENV=(PROTOCORE_ADAPTIVE_HEAP=1) ;;
    default)  HEAPENV=() ;;
    *) echo "COSTMODEL_HEAP must be adaptive or default" >&2; exit 1 ;;
esac

# ---- environment
{
    echo "date=$(date -Is)"
    echo "host_kernel=$(uname -r)"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
    echo "cpus=$(nproc)"
    echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
    echo "epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null || echo n/a)"
    echo "boost=$(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo n/a)"
    echo "python=$("$PY" -c 'import sys; print(sys.version.replace(chr(10), " "))')"
    echo "python_gil=$("$PY" -c 'import sys; print(int(sys._is_gil_enabled()) if hasattr(sys, "_is_gil_enabled") else 1)')"
    echo "protocore_lib=$(readlink -f "$PCLIB")"
    echo "protocore_lib_sha256=$(sha256sum "$PCLIB" | cut -d' ' -f1)"
    echo "heap=$HEAP"
    echo "rounds=$ROUNDS"
    echo "max_busy=$MAX_BUSY"
    echo "cxx=$(${CXX:-c++} --version | head -1)"
    echo "cc=$(${CC:-cc} --version | head -1)"
} > "$OUT/env.txt"

# gate: sets BUSY to the last busy figure measured
gate() {
    local waited=0
    while BUSY="$(busy)"; over "$BUSY" "$MAX_BUSY"; do
        if (( waited >= WAIT )); then
            if [[ "${COSTMODEL_ALLOW_LOAD:-0}" == 1 ]]; then
                echo "machine ${BUSY}% busy, above $MAX_BUSY%; continuing (COSTMODEL_ALLOW_LOAD=1)" >&2
                return
            fi
            echo "machine stayed above $MAX_BUSY% busy for ${WAIT}s; stopping (COSTMODEL_ALLOW_LOAD=1 to override)" >&2
            exit 3
        fi
        waited=$((waited + 5))
    done
}

# run <round> <name> <command...>: stdout to <name>.<round>.txt, load recorded
run() {
    local r="$1" name="$2"; shift 2
    gate
    local before; before="$(load1)"
    echo "round $r: $name (busy $BUSY%, load $before)" >&2
    if ! "$@" > "$OUT/$name.$r.txt"; then
        echo "$name failed in round $r (see $OUT/$name.$r.txt)" >&2; exit 4
    fi
    echo "LOAD name=$name round=$r busy=$BUSY before=$before after=$(load1)" >> "$OUT/load.txt"
}

COMPARE=(kernel capi interp)
for ((r = 1; r <= ROUNDS; r++)); do
    LIBR="$OUT/lib.$r"; mkdir -p "$LIBR"; cp "$PCLIB" "$LIBR/$PCSONAME"
    PCENV=(env LD_LIBRARY_PATH="$LIBR" "${HEAPENV[@]}")
    # the binaries must load this round's copy, not the build tree's
    if ! "${PCENV[@]}" ldd "$BUILD/costmodel_kernel" | grep -q "$LIBR/$PCSONAME"; then
        echo "round $r: fresh libprotoCore copy not picked up" >&2; exit 1
    fi
    for ((k = 0; k < ${#COMPARE[@]}; k++)); do
        prog="${COMPARE[$(( (k + r) % ${#COMPARE[@]} ))]}"
        case "$prog" in
            kernel) run "$r" kernel "${PCENV[@]}" "$BUILD/costmodel_kernel" ;;
            capi)   run "$r" capi "$CAPI" ;;
            interp) run "$r" interp "$PY" -I "$HERE/cpython_costmodel.py" ;;
        esac
    done
    run "$r" list_ops "${PCENV[@]}" "$BUILD/costmodel_list_ops_kernel"
    run "$r" list_ops_lab "$BUILD/costmodel_list_ops_lab"
    run "$r" lab "$BUILD/costmodel_list_lab"
    run "$r" string_lab "$BUILD/costmodel_string_lab"
    rm -rf "$LIBR"
done

"$PY" -I "$HERE/report.py" "$OUT"
