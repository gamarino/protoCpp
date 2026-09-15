# protoCpp benchmark results

**Measurement date: 2026-06-15.** This is a dated snapshot. All ratios and geomeans below were recomputed from the raw timings on 2026-09-15.

## Setup

- **Hardware**: AMD Ryzen 5 5500U (6 cores, 12 threads, laptop class), Linux x86_64.
- **C++ columns** (C++ floor, protoCpp, protoCpp + fast path): `benchmarks/bench.sh` in this repository, wall clock around each process, no warm-up runs, median of 5 runs per binary, built by this repository's `CMakeLists.txt` with g++ `-O3 -DNDEBUG` (no PGO).
- **protopy, protopyc and CPython columns**: the protoPython benchmark harness (`benchmarks/run_benchmarks.py`, wall clock around each process, 2 warm-up runs, then the median of 5 runs), as recorded in the protoPython report [`2026-06-15-sprint-3.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-sprint-3.md), committed with protoPython [`10ae9596`](https://github.com/gamarino/protoPython/commit/10ae9596). protopy and protopyc link against the same `libprotoCore.so` as protoCpp.
- **CPython**: the system `python3` invoked by the harness (`CPYTHON_BIN=python3`). The source report does not record the CPython version, nor whether that build has the GIL enabled.

## Raw timings (ms)

| benchmark | N | C++ floor (ms) | protoCpp (ms) | protoCpp + fast path (ms) | protopy (ms) | protopyc (ms) | CPython (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 10 M | 8.13 | 126.42 | 73.31 | 241.04 † § | 226.17 † § | 52.99 † § |
| `call_recursion` | fib(25) | 4.01 | 51.45 | 21.16 | 95.83 | 58.36 | 53.35 |
| `attr_lookup` | 5 M | 9.69 | 315.93 | – | ~3 500 ‡ | 89.14 † | 618 ‡ |
| `list_append_loop` | 10 K | 4.69 | 22.17 | – | 221.32 | 200.99 | 44.10 |
| `str_concat_loop` | 2 K | 5.57 | 21.55 | – | 212.52 | 311.14 | 51.41 |
| `multithread_cpu` | 4 threads × 2 M | 6.85 | 50.36 | 41.10 | 861.62 | 25.13 (invalid) | 721.67 |

The N column gives the problem size of the C++ columns. The Python columns use the same N unless marked:

† Measured by the protoPython harness at **N = 100,000**, not at the N of the C++ columns. The harness runs each script without arguments and does not set `BENCH_N`, so `int_sum_loop.py` and `attr_lookup.py` use their default N = 100,000. These values are not compared with the C++ columns below.

‡ Measured separately at N = 5,000,000 (`attr_lookup.py` takes N as its first argument). The CPython value was first recorded in protoCpp commit `b8813b0` and the protopy value in protoCpp commit `7740024`; neither appears in any protoPython report, and the run count is not recorded. Both values are approximate.

§ `int_sum_loop.py` gained `import os` (to read `BENCH_N`) in protoPython commit [`432c0621`](https://github.com/gamarino/protoPython/commit/432c0621), between the `2026-06-15-sprint-2.md` and `2026-06-15-sprint-3.md` reports; it is the only benchmark script that changed between them. At the same N, protopy `int_sum_loop` went from 24.78 ms to 241.04 ms and its peak RSS from 23.1 MB to 58.1 MB (protopyc: 21.62 → 226.17 ms), while `range_iterate`, which already imported `os`, stayed flat (193.91 → 190.35 ms). This indicates that module loading dominates these values, so the row is excluded from the conclusions below.

**Invalid:** the protopyc `multithread_cpu` value does not measure the workload. The value (25.13 ms) is below protoCpp's own fast-path time for the same work (41.10 ms) and matches the failure mode documented in protoPython's [`2026-06-15-sprint8-4way-honest.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-sprint8-4way-honest.md), where protopyc skipped `main()` in scripts that import `_thread`. It is kept for the record and excluded from every ratio in this document; the geomean table below shows the protopyc geomean with and without it.

**Start-up and harnesses:** every value is whole-process wall time. In the source report, CPython's `startup_empty` median is 64.75 ms, longer than its `call_recursion` (53.35 ms), `list_append_loop` (44.10 ms) and `str_concat_loop` (51.41 ms) medians, so on those rows the CPython value is dominated by interpreter start-up rather than by the workload. Of the CPython values taken from that report, only `multithread_cpu` (721.67 ms) is dominated by the work. The protoCpp timings include ProtoSpace start-up and shutdown, and no empty-program baseline was measured for the C++ binaries. The C++ columns (`benchmarks/bench.sh`, no warm-up runs) and the Python columns (protoPython harness, 2 warm-up runs) come from different harnesses and separate runs.

`int_sum_loop` and `attr_lookup` run at 10 M and 5 M iterations in C++, and their C++ baselines contain `asm volatile` barriers. At N = 100,000, `-O3` folded both loops into a constant, so the timings measured little more than process startup. The barriers and the larger N keep the loop body in the measurement. The other benchmarks use the same N as the protoPython scripts' defaults.

## Ratios

Each ratio is the first column's time divided by the second's: below 1× the first is faster, above 1× it is slower. "n/c" means not comparable (different N).

| benchmark | protoCpp / C++ | fast path / C++ | protoCpp / CPython | fast path / CPython | protopy / CPython | protopyc / CPython | protopyc / protopy |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 15.55× | 9.02× | n/c | n/c | 4.55× § | 4.27× § | 0.94× § |
| `call_recursion` | 12.83× | 5.28× | 0.96× | 0.40× | 1.80× | 1.09× | 0.61× |
| `attr_lookup` | 32.60× | – | 0.51× | – | ≈5.66× | n/c | n/c |
| `list_append_loop` | 4.73× | – | 0.50× | – | 5.02× | 4.56× | 0.91× |
| `str_concat_loop` | 3.87× | – | 0.42× | – | 4.13× | 6.05× | 1.46× |
| `multithread_cpu` | 7.35× | 6.00× | 0.07× | 0.06× | 1.19× | invalid | invalid |

Reading the columns (all ratios compare whole-process wall times; see the start-up note above):

- **protoCpp / C++** ranges from 3.87× to 32.60× in whole-process wall time, including ProtoSpace start-up. It combines the cost of doing the same work through protoCore's public API (tagged values, attribute lookups, persistent collections, managed allocation) rather than with plain C++ data structures with the cost of ProtoSpace start-up and shutdown; these measurements do not separate the two. `attr_lookup` is the highest: each read calls `ProtoObject::getAttribute` across the shared-library boundary, where the C++ floor loads a struct field.
- **protoCpp / CPython**: in whole-process wall time, protoCpp finishes before CPython on all five benchmarks with a CPython measurement at the same N:
  - `call_recursion`: 1.04× faster
  - `attr_lookup`: 1.96× faster
  - `list_append_loop`: 1.99× faster
  - `str_concat_loop`: 2.39× faster
  - `multithread_cpu`: 14.33× faster. protoCpp runs four native threads; the CPython build's threading mode is not recorded.

  `int_sum_loop` has no CPython value at the same N. On `call_recursion`, `list_append_loop` and `str_concat_loop`, the CPython value is dominated by interpreter start-up.
- **Fast path**: the inline SmallInt helpers from `protoCore.h` cut protoCpp wall time by 42.0% on `int_sum_loop` (126.42 → 73.31 ms), 58.9% on `call_recursion` (51.45 → 21.16 ms) and 18.4% on `multithread_cpu` (50.36 → 41.10 ms). The fast-path `call_recursion` process takes 21.16 ms against CPython's 53.35 ms (0.40×), a CPython value dominated by interpreter start-up.
- **protopy / CPython**: excluding `int_sum_loop` (§), the interpreter is slower than CPython on all five remaining rows, from 1.19× (`multithread_cpu`) to ≈5.66× (`attr_lookup` at N = 5 M).
- **protopyc / CPython**: excluding `int_sum_loop` (§), the ahead-of-time compiler is slower than CPython on all three valid, comparable rows, from 1.09× (`call_recursion`) to 6.05× (`str_concat_loop`). At the harness size (N = 100,000), the source report gives `attr_lookup` 1.70× (89.14 ms against 52.56 ms).
- **protopyc / protopy**: excluding `int_sum_loop` (§), it ranges from 0.61× to 1.46×. protopyc is faster than protopy on `call_recursion` and `list_append_loop`, and slower on `str_concat_loop`.

At these sizes the Python timings include interpreter start-up and, for scripts that import `os`, module loading, so this table does not show where the Python runtimes' remaining cost lies.

## protoPython harness geomeans

The protoPython harness suite also contains `startup_empty`, `range_iterate`, `memory_pressure` and five pyperformance-derived benchmarks. The table gives geomeans relative to CPython, recomputed from each report's table, with `memory_pressure` excluded (see below).

| protoPython report | protopy (n = 13) | protopyc as reported (n = 12) | protopyc without `multithread_cpu` (n = 11) |
|---|---:|---:|---:|
| [`2026-05-24-perf-final.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-05-24-perf-final.md) | 5.72× | 3.17× | 3.29× |
| [`2026-06-15-post-optimisation.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-post-optimisation.md) | 4.49× | 1.97× | 2.90× |
| [`2026-06-15-sprint-2.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-sprint-2.md) | 4.10× | 2.72× | 2.84× |
| [`2026-06-15-sprint-3.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-sprint-3.md) (source of the tables above) | 3.99× | 2.25× | 3.28× |

The reports label both geomeans n = 13. The protopyc geomean covers 12 rows because `startup_empty` has no protopyc value. The last column drops `multithread_cpu` from every report so that the series does not depend on that row. In `2026-06-15-sprint-3.md` that row is the invalid value (25.13 ms). The value in `2026-06-15-post-optimisation.md` (30.85 ms) is of the same magnitude. The "as reported" column includes that row, which pulls the protopyc geomean down. The series is still not fully like-for-like: `int_sum_loop.py` changed between `2026-06-15-sprint-2.md` and `2026-06-15-sprint-3.md` (§).

`memory_pressure` is reported for information only and does not take part in any geomean. CPython frees memory eagerly through reference counting. protoCore runs a concurrent tracing collector that defers reclamation until the working set forces it. On that workload the protoPython wall time is dominated by collection scheduling under stress, so its ratio does not reflect steady-state throughput.

### Changes to protoPython between these reports

1. Commits `82a5dd08`..`ac4a2505` (before the post-optimisation report). In release builds, diagnostic checks become compile-time constants. `libprotoPython` is compiled with `-ftls-model=initial-exec`. The `ContextScope` stack buffer grows from 64 to 256 slots. Argument lists for non-fast-path calls are allocated in one step. A dispatch-loop investigation is recorded with no change. Summary: [`docs/2026-06-15-final-comparison.md`](https://github.com/gamarino/protoPython/blob/main/docs/2026-06-15-final-comparison.md).
2. Commits `1230389e`..`0d06e617` (before `2026-06-15-sprint-2.md`). `OP_LIST_APPEND` uses pointer-identity discrimination. A polymorphic inline cache is added on the `LOAD_METHOD` slow path. `str + str` is implemented with `ProtoString` rope `appendLast`. Between the two reports, protopy `list_append_loop` drops 34.3% (322.58 → 212.09 ms) and `str_concat_loop` drops 47.1% (354.29 → 187.36 ms).
3. Commit `10ae9596` (recorded in `2026-06-15-sprint-3.md`). A per-thread object-to-type cache is added in `PythonEnvironment::getType`. Relative to `2026-06-15-sprint-2.md`, protopy `attr_lookup` at N = 100,000 drops 61.5% (210.94 → 81.23 ms).

A kernel-level mutable list, which would target the list-append cost, has been proposed and deferred; see [`docs/2026-06-15-step-6-list-mutable-deferred.md`](https://github.com/gamarino/protoPython/blob/main/docs/2026-06-15-step-6-list-mutable-deferred.md).

## pyperformance-derived benchmarks

These rows come from the same protoPython report. protoCpp has no C++ ports of them.

| benchmark | CPython (ms) | protopy (ms) | protopyc (ms) | protopy / CPython | protopyc / CPython |
|---|---:|---:|---:|---:|---:|
| `pyperf_richards_lite` | 51.81 | 105.01 | 43.48 | 2.03× | 0.84× |
| `pyperf_fib` | 127.17 | 989.07 | 249.05 | 7.78× | 1.96× |
| `pyperf_sieve` | 41.92 | 362.63 | 145.43 | 8.65× | 3.47× |
| `pyperf_nqueens` | 86.11 | 2337.51 | 487.92 | 27.15× | 5.67× |
| `pyperf_binary_trees` | 77.37 | 1827.31 | 995.41 | 23.62× | 12.87× |

This run did not check protopyc's timings against the computed results. The later protoPython report [`2026-06-15-sprint8-4way-honest.md`](https://github.com/gamarino/protoPython/blob/main/benchmarks/reports/2026-06-15-sprint8-4way-honest.md) recommends that check before relying on any protopyc row.

## Reproducing

```bash
# protoCore (built once)
cd protoCore && cmake -B build_release -S . && cmake --build build_release --target protoCore

# protoCpp: C++ floor, protoCpp and fast-path columns
cd ../protoCpp && cmake -B build_release -S . && cmake --build build_release
./benchmarks/bench.sh

# protoPython harness: protopy, protopyc and CPython columns
cd ../protoPython
cmake -B build-lto -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-lto -j
PROTOPY_BIN=$PWD/build-lto/src/runtime/protopy \
PROTOPYC_BIN=$PWD/build-lto/src/compiler/protopyc \
RUN_MODULE_BIN=$PWD/build-lto/test/compiler/run_module \
CPYTHON_BIN=python3 \
python3 benchmarks/run_benchmarks.py --output benchmarks/reports/$(date +%Y-%m-%d).md
```

The `attr_lookup` values at N = 5,000,000 come from separate runs of `attr_lookup.py` with `5000000` as its first argument. `int_sum_loop.py` reads its N from the `BENCH_N` environment variable. Setting `BENCH_N=10000000` gives a measurement at the size of the C++ columns; this table does not include one.

Absolute numbers depend on hardware. Ratios transfer better, but they also vary with CPU, compiler and interpreter build.

## Caveats

1. The C++ floor is hand-written C++: `std::vector::push_back`, `std::string::operator+`, raw `int64_t`. protoCpp gives up part of that speed for what a shared kernel provides: garbage collection, structural sharing, atomic mutability and GIL-free threads. Apart from ProtoSpace start-up and shutdown, which the whole-process timings also include, the protoCpp / C++ ratio is the price of those features.
2. On laptop CPUs, thermal throttling during sustained four-thread loops makes `multithread_cpu` sensitive. Its protopyc value is invalid (see above).
3. The CPython version and build are not recorded. Other CPython builds (free-threading, PGO/LTO), or PyPy, would give different numbers.
4. Every value is a single median. No run-to-run variance was recorded. Re-run on your own hardware before drawing conclusions.
5. Every value is whole-process wall time, and the C++ and Python columns come from different harnesses and separate runs. On short rows the timings are dominated by interpreter or ProtoSpace start-up rather than by the workload (see the start-up note above).
