#!/usr/bin/env bash
# costmodel/run.sh — runs the kernel cost model, its CPython twin and the
# structure lab, and prints one markdown report.
#
#   costmodel/run.sh [build-dir] [python]
#
# Every number is measured in-process, in steady state (child contexts,
# adaptive heap), best of several batches: start-up and imports never enter.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-$HERE/../build_release}"
PY="${2:-python3}"
[[ -d "$BUILD/bin" ]] && BUILD="$BUILD/bin"
OUT="$(mktemp -d)"

PROTOCORE_ADAPTIVE_HEAP=1 "$BUILD/costmodel_kernel" > "$OUT/kernel.txt"
"$PY" "$HERE/cpython_costmodel.py" > "$OUT/cpython.txt"
"$BUILD/costmodel_list_lab" > "$OUT/lab.txt"
"$BUILD/costmodel_string_lab" > "$OUT/string_lab.txt"

"$PY" "$HERE/report.py" "$OUT" "$("$PY" -V 2>&1)"